#!/usr/bin/env python3
"""Read a sigrok/PulseView `.sr` capture and report its structure.

This is a generic, model-free reader for any user-supplied `.sr` capture. It
only parses the sigrok container: sample rate, channel (probe) names, total
sample count, and per-channel edge/level statistics, optionally a raw sample
slice. It does NOT interpret transactions or decode any protocol; use
`tools/decode-panel-spi.py` with a faster analyzer export for SPI decoding.

The `.sr` format is a zip with a `metadata` INI ([device 1]: samplerate,
unitsize, total probes, probeN names) plus `logic-1-N` chunks of little-endian
packed samples (`unitsize` bytes per sample, one bit per probe).
"""
from __future__ import annotations

import argparse
import configparser
import io
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator
from zipfile import BadZipFile, ZipFile

CHUNK_RE = re.compile(r"^logic-1-(\d+)$")


@dataclass
class Capture:
    path: Path
    samplerate_hz: int
    unitsize: int
    total_probes: int
    probe_bits: dict[str, int]
    chunk_names: list[str]
    archive: ZipFile

    def samples(self) -> Iterator[int]:
        remainder = b""
        for name in self.chunk_names:
            with self.archive.open(name) as member:
                while block := member.read(1024 * 1024):
                    payload = remainder + block
                    whole = len(payload) - (len(payload) % self.unitsize)
                    if self.unitsize == 1:
                        yield from payload[:whole]
                    else:
                        view = memoryview(payload)
                        for offset in range(0, whole, self.unitsize):
                            yield int.from_bytes(view[offset:offset + self.unitsize], "little")
                    remainder = payload[whole:]
        if remainder:
            raise ValueError("logic chunks end with a partial sample")

    def close(self) -> None:
        self.archive.close()


def parse_rate(value: str) -> int:
    match = re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*([kKmMgG]?)(?:Hz)?\s*", value)
    if not match:
        raise ValueError(f"unsupported samplerate {value!r}")
    multiplier = {"": 1, "k": 1_000, "m": 1_000_000, "g": 1_000_000_000}[match.group(2).lower()]
    rate = float(match.group(1)) * multiplier
    if not rate.is_integer() or rate <= 0:
        raise ValueError(f"invalid samplerate {value!r}")
    return int(rate)


def open_capture(path: Path) -> Capture:
    archive = ZipFile(path)
    try:
        metadata = archive.read("metadata").decode("utf-8")
    except Exception:
        archive.close()
        raise
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    try:
        parser.read_file(io.StringIO(metadata))
    except configparser.Error:
        archive.close()
        raise
    if not parser.has_section("device 1"):
        archive.close()
        raise ValueError("metadata has no [device 1] section")
    device = parser["device 1"]
    try:
        unitsize = int(device["unitsize"])
        total_probes = int(device["total probes"])
        samplerate_hz = parse_rate(device["samplerate"])
    except KeyError as exc:
        archive.close()
        raise ValueError(f"metadata missing {exc.args[0]!r}") from exc
    if unitsize < 1:
        archive.close()
        raise ValueError("unitsize must be positive")
    if total_probes < 1 or total_probes > unitsize * 8:
        archive.close()
        raise ValueError("invalid total probes for unitsize")

    probe_bits: dict[str, int] = {}
    for bit in range(total_probes):
        key = f"probe{bit + 1}"
        if key not in device:
            archive.close()
            raise ValueError(f"metadata missing {key!r}")
        name = device[key].strip()
        if not name:
            archive.close()
            raise ValueError(f"metadata has an empty {key!r}")
        if name.lower() in probe_bits:
            archive.close()
            raise ValueError(f"duplicate probe name {name!r}")
        probe_bits[name.lower()] = bit

    numbered = []
    for name in archive.namelist():
        match = CHUNK_RE.match(name)
        if match:
            numbered.append((int(match.group(1)), name))
    if not numbered:
        archive.close()
        raise ValueError("session has no logic-1-N chunks")
    numbered.sort()
    suffixes = [number for number, _ in numbered]
    if len(set(suffixes)) != len(suffixes):
        archive.close()
        raise ValueError("session has duplicate logic-1-N chunk numbers")
    if suffixes != list(range(suffixes[0], suffixes[-1] + 1)):
        archive.close()
        raise ValueError("logic-1-N chunk numbering has a gap")
    return Capture(path, samplerate_hz, unitsize, total_probes, probe_bits,
                   [name for _, name in numbered], archive)


def level(sample: int, bit: int) -> int:
    return (sample >> bit) & 1


def analyze(capture: Capture, slice_probe: int | None, slice_start: int, slice_len: int) -> dict:
    bits = sorted(capture.probe_bits.values())
    names = {bit: name for name, bit in capture.probe_bits.items()}
    prev = None
    total = 0
    rises = {bit: 0 for bit in bits}
    falls = {bit: 0 for bit in bits}
    ones = {bit: 0 for bit in bits}
    slice_values: list[int] = []
    for index, sample in enumerate(capture.samples()):
        total = index + 1
        for bit in bits:
            lv = level(sample, bit)
            ones[bit] += lv
            if prev is not None:
                p = level(prev, bit)
                if p == 0 and lv == 1:
                    rises[bit] += 1
                elif p == 1 and lv == 0:
                    falls[bit] += 1
        if slice_probe is not None and slice_start <= index < slice_start + slice_len:
            slice_values.append(level(sample, slice_probe))
        prev = sample

    channels = []
    for bit in bits:
        channels.append({
            "name": names[bit],
            "bit": bit,
            "one_samples": ones[bit],
            "rising_edges": rises[bit],
            "falling_edges": falls[bit],
        })
    report = {
        "format": "sr-capture-summary-v1",
        "path": str(capture.path),
        "samplerate_hz": capture.samplerate_hz,
        "unitsize_bytes": capture.unitsize,
        "total_probes": capture.total_probes,
        "total_samples": total,
        "duration_s": total / capture.samplerate_hz,
        "channels": channels,
    }
    if slice_probe is not None:
        report["slice"] = {
            "probe_name": names.get(slice_probe, str(slice_probe)),
            "start": slice_start,
            "length": len(slice_values),
            "levels": "".join(str(v) for v in slice_values),
        }
    return report


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input", type=Path, help="sigrok .sr session")
    p.add_argument("--slice-probe", help="probe name to dump a raw 0/1 slice from (default: none)")
    p.add_argument("--slice-start", type=int, default=0, help="first sample index of the slice (default 0)")
    p.add_argument("--slice-len", type=int, default=256, help="slice length in samples (default 256)")
    return p


def main() -> int:
    args = build_parser().parse_args()
    try:
        capture = open_capture(args.input)
        try:
            slice_probe = None
            if args.slice_probe is not None:
                key = args.slice_probe.lower()
                if key not in capture.probe_bits:
                    choices = ", ".join(sorted(capture.probe_bits))
                    raise ValueError(f"probe {args.slice_probe!r} not found; capture has: {choices}")
                slice_probe = capture.probe_bits[key]
            report = analyze(capture, slice_probe, args.slice_start, args.slice_len)
        finally:
            capture.close()
    except (OSError, ValueError, KeyError, BadZipFile, configparser.Error, UnicodeDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
