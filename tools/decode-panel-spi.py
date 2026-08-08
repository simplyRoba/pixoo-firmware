#!/usr/bin/env python3
"""Decode the panel SPI from a gusmanb LogicAnalyzer capture into Pixoo frames.

Reads a capture exported by the RP2040/RP2350 LogicAnalyzer (gusmanb) in either
format:

- CSV: one column per channel, one row per sample, cells are 0/1, no timebase.
       Pass --samplerate (Hz) because the CSV carries no sample rate.
- LAC: JSON with `Settings.Frequency` (Hz) and per-channel `Samples` arrays.
       The sample rate is read from the file; --samplerate overrides it.

It does a standard SPI master decode (SPI mode 0: CPOL=0/CPHA=0, sample MOSI on
the rising SCLK edge, MSB-first) over one chip-select-low window per transaction,
then parses the stock Pixoo panel framing `0xAA <len LE16> <cmd> <payload> 0xBB`.
Byte order / edge / CS polarity are all overridable so the decode can be checked
against the known command set rather than assumed.

The capture should be sampled fast enough to resolve the panel's ~15 MHz SPI
clock.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator


@dataclass
class Capture:
    samplerate_hz: int | None
    channel_names: list[str]  # index = channel column order in `rows`
    rows: list[tuple[int, ...]]  # one tuple of 0/1 per sample

    def index_of(self, name: str) -> int:
        # Accept exact name, case-insensitive name, or "Channel N" / "N".
        lowered = [c.lower() for c in self.channel_names]
        key = name.strip().lower()
        if key in lowered:
            return lowered.index(key)
        # numeric: 0-based channel index directly
        if key.isdigit():
            idx = int(key)
            if 0 <= idx < len(self.channel_names):
                return idx
        raise ValueError(
            f"channel {name!r} not found; capture has: {', '.join(self.channel_names)}"
        )


def load_csv(path: Path) -> Capture:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration:
            raise ValueError("CSV is empty")
        names = [h.strip() for h in header]
        if not names:
            raise ValueError("CSV header has no columns")
        rows: list[tuple[int, ...]] = []
        for lineno, raw in enumerate(reader, start=2):
            if not raw:
                continue
            if len(raw) != len(names):
                raise ValueError(
                    f"CSV line {lineno}: {len(raw)} values, expected {len(names)}"
                )
            try:
                rows.append(tuple(1 if int(v) else 0 for v in raw))
            except ValueError as exc:
                raise ValueError(f"CSV line {lineno}: non-integer sample") from exc
    if not rows:
        raise ValueError("CSV has a header but no sample rows")
    return Capture(samplerate_hz=None, channel_names=names, rows=rows)


def load_lac(path: Path) -> Capture:
    data = json.loads(path.read_text(encoding="utf-8"))
    settings = data.get("Settings")
    if not isinstance(settings, dict):
        raise ValueError("LAC has no Settings object")
    channels = settings.get("CaptureChannels")
    if not isinstance(channels, list) or not channels:
        raise ValueError("LAC has no CaptureChannels")
    freq = settings.get("Frequency")
    samplerate = None
    if isinstance(freq, (int, float)) and math.isfinite(freq) and freq > 0:
        candidate_rate = int(freq)
        if candidate_rate > 0:
            samplerate = candidate_rate

    names: list[str] = []
    sample_columns: list[list[int]] = []
    length: int | None = None
    for pos, ch in enumerate(channels):
        if not isinstance(ch, dict):
            raise ValueError(f"LAC channel {pos} is not an object")
        name = ch.get("ChannelName") or f"Channel {ch.get('ChannelNumber', pos) + 1}"
        samples = ch.get("Samples")
        if not isinstance(samples, list):
            raise ValueError(f"LAC channel {name!r} has no Samples array")
        column = [1 if int(s) else 0 for s in samples]
        if length is None:
            length = len(column)
        elif len(column) != length:
            raise ValueError("LAC channels have mismatched Samples lengths")
        names.append(str(name))
        sample_columns.append(column)
    assert length is not None
    rows = [tuple(col[i] for col in sample_columns) for i in range(length)]
    return Capture(samplerate_hz=samplerate, channel_names=names, rows=rows)


def load_capture(path: Path) -> Capture:
    suffix = path.suffix.lower()
    if suffix == ".csv":
        return load_csv(path)
    if suffix == ".lac":
        return load_lac(path)
    # Fall back on content sniffing.
    head = path.read_bytes()[:64].lstrip()
    if head[:1] == b"{":
        return load_lac(path)
    return load_csv(path)


@dataclass
class Transaction:
    start_sample: int
    end_sample: int
    bytes_: list[int] = field(default_factory=list)
    bit_count: int = 0


def decode_spi(
    capture: Capture,
    sclk: int,
    mosi: int,
    cs: int | None,
    cs_active: int,
    clock_edge: str,
    bit_order: str,
) -> list[Transaction]:
    """Sample MOSI on the chosen SCLK edge; group bytes per CS-active window.

    If cs is None, the whole capture is one transaction.
    """
    rows = capture.rows
    transactions: list[Transaction] = []

    def new_txn(start: int) -> Transaction:
        return Transaction(start_sample=start, end_sample=start)

    active = cs_active
    current: Transaction | None = None
    bit_buffer: list[int] = []

    prev_sclk = rows[0][sclk] if rows else 0
    prev_cs = rows[0][cs] if (cs is not None and rows) else active

    def flush_byte_into(txn: Transaction) -> None:
        if len(bit_buffer) == 8:
            if bit_order == "msb":
                value = 0
                for b in bit_buffer:
                    value = (value << 1) | b
            else:  # lsb-first
                value = 0
                for i, b in enumerate(bit_buffer):
                    value |= b << i
            txn.bytes_.append(value)
            bit_buffer.clear()

    # Initialize state from sample 0.
    if cs is None:
        current = new_txn(0)
    elif rows and rows[0][cs] == active:
        current = new_txn(0)

    for index, row in enumerate(rows):
        sclk_level = row[sclk]
        cs_level = row[cs] if cs is not None else active

        # CS edge handling (skip when cs is None).
        if cs is not None:
            began = prev_cs != active and cs_level == active
            ended = prev_cs == active and cs_level != active
            if began:
                current = new_txn(index)
                bit_buffer.clear()
            if ended and current is not None:
                current.bit_count = len(current.bytes_) * 8 + len(bit_buffer)
                current.end_sample = index
                transactions.append(current)
                current = None
                bit_buffer.clear()

        # Clock edge sampling (only inside an active window).
        if current is not None and (cs is None or cs_level == active):
            rising = prev_sclk == 0 and sclk_level == 1
            falling = prev_sclk == 1 and sclk_level == 0
            edge = rising if clock_edge == "rising" else falling
            if edge:
                bit_buffer.append(row[mosi])
                flush_byte_into(current)

        prev_sclk = sclk_level
        if cs is not None:
            prev_cs = cs_level

    if current is not None:
        current.bit_count = len(current.bytes_) * 8 + len(bit_buffer)
        current.end_sample = len(rows)
        transactions.append(current)

    return transactions


def parse_frames(payload: list[int]) -> list[dict]:
    """Parse `0xAA <len LE16> <cmd> <payload> 0xBB` frames from a byte stream."""
    frames: list[dict] = []
    i = 0
    n = len(payload)
    while i < n:
        if payload[i] != 0xAA:
            i += 1
            continue
        if i + 4 > n:
            frames.append({"offset": i, "valid": False, "error": "truncated header"})
            break
        length = payload[i + 1] | (payload[i + 2] << 8)
        cmd = payload[i + 3]
        end = i + 4 + length  # index of the tail byte
        if end >= n:
            frames.append({
                "offset": i, "valid": False, "error": "truncated payload",
                "declared_len": length, "cmd": cmd,
            })
            break
        tail = payload[end]
        body = payload[i + 4:end]
        valid = tail == 0xBB
        frames.append({
            "offset": i,
            "valid": valid,
            "cmd": cmd,
            "len": length,
            "payload_len": len(body),
            "tail": tail,
            "payload": body,
        })
        i = end + 1 if valid else i + 1
    return frames


CMD_NAMES = {0x00: "full_frame_rgb", 0x10: "init", 0x21: "continuation", 0x22: "white_balance"}


def finite_positive_float(value: str) -> float:
    try:
        number = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return number


def nonnegative_int(value: str) -> int:
    try:
        number = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if number < 0:
        raise argparse.ArgumentTypeError("must be zero or greater")
    return number


def summarize_frame(frame: dict, payload_preview: int) -> dict:
    out = {k: v for k, v in frame.items() if k != "payload"}
    if "cmd" in frame:
        out["cmd_name"] = CMD_NAMES.get(frame["cmd"], "unknown")
    if "payload" in frame:
        body = frame["payload"]
        out["payload_hex_preview"] = bytes(body[:payload_preview]).hex()
        out["payload_truncated"] = len(body) > payload_preview
    return out


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input", type=Path, help="LogicAnalyzer capture (.csv or .lac)")
    p.add_argument("--sclk", default="4", help="clock channel name or 0-based index (ribbon pin 4 / GPIO25)")
    p.add_argument("--mosi", default="3", help="data channel name or 0-based index (ribbon pin 3 / GPIO33)")
    p.add_argument("--cs", default="5", help="chip-select channel; use 'none' for whole-capture (ribbon pin 5 / GPIO26)")
    p.add_argument("--cs-active", type=int, choices=(0, 1), default=0, help="active CS level (default 0 = active-low)")
    p.add_argument("--clock-edge", choices=("rising", "falling"), default="rising",
                   help="SCLK edge to sample MOSI on (default rising = SPI mode 0)")
    p.add_argument("--bit-order", choices=("msb", "lsb"), default="msb", help="bit order per byte (default msb-first)")
    p.add_argument("--samplerate", type=finite_positive_float, default=None, help="finite sample rate in Hz (required for CSV; overrides LAC)")
    p.add_argument("--payload-preview", type=nonnegative_int, default=16, help="nonnegative bytes of each frame payload to show in the summary")
    p.add_argument("--json", type=Path, help="write the full decode (incl. full payloads as hex) here")
    p.add_argument("--dump-payload", type=Path, help="write the concatenated bytes of the first valid full_frame_rgb (cmd 0x00) payload as a raw .bin (12288 bytes) for the frame renderer")
    return p


def main() -> int:
    args = build_parser().parse_args()
    try:
        capture = load_capture(args.input)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    samplerate = args.samplerate or capture.samplerate_hz
    if capture.samplerate_hz is None and args.samplerate is None:
        print("error: CSV has no sample rate; pass --samplerate", file=sys.stderr)
        return 2

    try:
        sclk = capture.index_of(args.sclk)
        mosi = capture.index_of(args.mosi)
        cs = None if args.cs.strip().lower() == "none" else capture.index_of(args.cs)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    transactions = decode_spi(capture, sclk, mosi, cs, args.cs_active,
                              args.clock_edge, args.bit_order)

    txn_reports = []
    dumped = False
    for t in transactions:
        frames = parse_frames(t.bytes_)
        report = {
            "start_sample": t.start_sample,
            "end_sample": t.end_sample,
            "duration_samples": t.end_sample - t.start_sample,
            "byte_count": len(t.bytes_),
            "bit_count": t.bit_count,
            "frames": [summarize_frame(f, args.payload_preview) for f in frames],
        }
        if samplerate:
            report["start_time_s"] = t.start_sample / samplerate
            report["duration_s"] = (t.end_sample - t.start_sample) / samplerate
        txn_reports.append(report)

        if args.dump_payload and not dumped:
            for f in frames:
                if f.get("valid") and f.get("cmd") == 0x00 and "payload" in f:
                    args.dump_payload.parent.mkdir(parents=True, exist_ok=True)
                    args.dump_payload.write_bytes(bytes(f["payload"]))
                    dumped = True
                    break

    result = {
        "format": "pixoo-panel-spi-decode-v1",
        "capture": {
            "path": str(args.input),
            "samplerate_hz": samplerate,
            "channels": capture.channel_names,
            "total_samples": len(capture.rows),
        },
        "spi": {
            "sclk": args.sclk, "mosi": args.mosi, "cs": args.cs,
            "cs_active": args.cs_active, "clock_edge": args.clock_edge,
            "bit_order": args.bit_order,
        },
        "summary": {
            "transactions": len(transactions),
            "valid_frames": sum(1 for r in txn_reports for f in r["frames"] if f.get("valid")),
            "invalid_frames": sum(1 for r in txn_reports for f in r["frames"] if not f.get("valid")),
        },
        "transactions": txn_reports,
    }

    if args.json:
        full = dict(result)
        full["transactions"] = []
        for t in transactions:
            frames = parse_frames(t.bytes_)
            full["transactions"].append({
                "start_sample": t.start_sample, "end_sample": t.end_sample,
                "byte_count": len(t.bytes_),
                "bytes_hex": bytes(t.bytes_).hex(),
                "frames": [
                    {**{k: v for k, v in f.items() if k != "payload"},
                     "cmd_name": CMD_NAMES.get(f.get("cmd", -1), "unknown") if "cmd" in f else None,
                     "payload_hex": bytes(f["payload"]).hex() if "payload" in f else None}
                    for f in frames
                ],
            })
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(full, indent=2) + "\n", encoding="utf-8")
        result["json_path"] = str(args.json)

    if args.dump_payload:
        result["dump_payload_path"] = str(args.dump_payload) if dumped else None
        result["dump_payload_written"] = dumped

    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
