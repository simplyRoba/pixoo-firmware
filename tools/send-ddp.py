#!/usr/bin/env python3
"""Send a finite, low-brightness 64x64 RGB DDP test pattern over IPv4 UDP.

The top four rows show a 32-bit counter in 2x4 cells, most-significant bit
first. Corner colors identify orientation. Summaries count local sends only,
not delivery or receiver FPS. Pauses count toward the run's wall time.

Examples:
  tools/send-ddp.py --host display.local --seconds 30
  PIXOO_HOST=display.local tools/send-ddp.py --pause-at 5 --pause-seconds 2
  tools/send-ddp.py --host display.local --overload-fps 120 --overload-at 10
"""
from __future__ import annotations

import argparse
import math
import os
import socket
import struct
import sys
import time
from dataclasses import dataclass

FRAME_BYTES = 64 * 64 * 3
CHUNK_BYTES = 1440
SEND_TIMEOUT_SECONDS = 0.25


def packetize(frame: bytes, index: int, legacy: bool = False):
    """Yield nine DDP datagrams for one row-major RGB frame."""
    if len(frame) != FRAME_BYTES:
        raise ValueError(f"frame must contain {FRAME_BYTES} bytes")
    for offset in range(0, FRAME_BYTES, CHUNK_BYTES):
        payload = frame[offset:offset + CHUNK_BYTES]
        flags = 0x41 if offset + len(payload) == FRAME_BYTES else 0x40
        yield struct.pack("!BBBBIH", flags, index % 15 + 1,
                          0x01 if legacy else 0x0B, 1, offset, len(payload)) + payload


def render_frame(index: int) -> bytes:
    """Render a moving stripe/gradient and the low 32 bits of the counter."""
    frame = bytearray(FRAME_BYTES)
    counter = index & 0xFFFFFFFF
    for y in range(64):
        for x in range(64):
            color = (x // 2, y // 2, 8)
            if (x - index) % 64 < 3:
                color = (48, 32, 16)
            if y < 4:
                level = 64 if counter & (1 << (31 - x // 2)) else 4
                color = (level, level, level)
                if x < 4:
                    color = (level, 0, 0)
                elif x >= 60:
                    color = (0, level, 0)
            elif y >= 60 and x < 4:
                color = (0, 0, 48)
            elif y >= 60 and x >= 60:
                color = (48, 48, 0)
            offset = (y * 64 + x) * 3
            frame[offset:offset + 3] = bytes(color)
    return bytes(frame)


def finite_number(value: str) -> float:
    try:
        number = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a finite number") from exc
    if not math.isfinite(number):
        raise argparse.ArgumentTypeError("must be a finite number")
    return number


def positive(value: str) -> float:
    number = finite_number(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def nonnegative(value: str) -> float:
    number = finite_number(value)
    if number < 0:
        raise argparse.ArgumentTypeError("must be zero or greater")
    return number


def fps(value: str) -> float:
    number = positive(value)
    if number > 1000:
        raise argparse.ArgumentTypeError("must not exceed 1000")
    return number


def port(value: str) -> int:
    try:
        number = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if not 1 <= number <= 65535:
        raise argparse.ArgumentTypeError("must be between 1 and 65535")
    return number


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=os.environ.get("PIXOO_HOST"),
                        help="destination host (required; or set PIXOO_HOST)")
    parser.add_argument("--port", type=port, default=4048)
    parser.add_argument("--fps", type=fps, default=30.0)
    parser.add_argument("--seconds", type=positive, default=30.0)
    parser.add_argument("--pause-at", type=nonnegative,
                        help="pause once at this elapsed time; omitted disables pause")
    parser.add_argument("--pause-seconds", type=positive, default=2.0)
    parser.add_argument("--overload-fps", type=fps,
                        help="temporary send rate; omitted disables overload")
    parser.add_argument("--overload-at", type=nonnegative, default=10.0)
    parser.add_argument("--overload-seconds", type=positive, default=2.0)
    parser.add_argument("--legacy-type", action="store_true",
                        help="use DDP type 0x01 instead of RGB888 type 0x0B")
    return parser


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not args.host or not args.host.strip():
        parser.error("--host is required (or set PIXOO_HOST)")
    if args.pause_at is not None and args.pause_at >= args.seconds:
        parser.error("--pause-at must be less than --seconds")
    if args.overload_fps is not None and args.overload_at >= args.seconds:
        parser.error("--overload-at must be less than --seconds when overload is enabled")
    return args


@dataclass
class Stats:
    frames: int = 0
    datagrams: int = 0
    skipped_slots: int = 0


def summarize(stats: Stats, elapsed: float, final: bool = False) -> None:
    actual_fps = stats.frames / elapsed if elapsed > 0 else 0.0
    print(f"{'final' if final else 'summary'}: elapsed={elapsed:.3f}s "
          f"local_sent_frames={stats.frames} local_sent_datagrams={stats.datagrams} "
          f"actual_fps={actual_fps:.2f} skipped_scheduled_slots={stats.skipped_slots}",
          flush=True)


def run(args, send, *, clock=time.monotonic, sleep=time.sleep,
        report=summarize) -> Stats:
    """Pace local sends; injected functions support deterministic host tests."""
    stats = Stats()
    start = clock()
    end = start + args.seconds
    events = []
    if args.pause_at is not None:
        events.append((args.pause_at, args.pause_at + args.pause_seconds, None))
    if args.overload_fps is not None:
        events.append((args.overload_at, args.overload_at + args.overload_seconds,
                       args.overload_fps))
    boundaries = sorted({0.0, args.seconds} |
                        {min(t, args.seconds) for event in events for t in event[:2]})
    next_report = start + 5.0
    last_frame_started = None
    try:
        for left, right in zip(boundaries, boundaries[1:]):
            active = [rate for begin, finish, rate in events if begin <= left < finish]
            rate = None if None in active else (active[0] if active else args.fps)
            phase_end = start + right
            deadline = max(start + left, clock())
            interval = 1.0 / rate if rate is not None else None
            if interval is not None and last_frame_started is not None:
                deadline = max(deadline, last_frame_started + interval)
            while True:
                now = clock()
                if now >= phase_end:
                    break
                if now >= next_report:
                    report(stats, now - start)
                    next_report += (math.floor((now - next_report) / 5.0) + 1) * 5.0
                wake = min(phase_end, next_report,
                           deadline if interval is not None else phase_end)
                if now < wake:
                    sleep(wake - now)
                    continue
                if interval is None:
                    continue
                # Keep only the latest due slot; never replay missed deadlines.
                missed = math.floor(max(0.0, now - deadline) / interval)
                stats.skipped_slots += missed
                deadline += missed * interval
                frame_started_at = clock()
                last_frame_started = frame_started_at
                frame = render_frame(stats.frames)
                if clock() >= min(end, phase_end):
                    break
                for packet in packetize(frame, stats.frames, args.legacy_type):
                    if clock() >= phase_end:
                        break
                    send(packet)
                    stats.datagrams += 1
                else:
                    stats.frames += 1
                deadline = max(deadline + interval, frame_started_at + interval)
    finally:
        report(stats, clock() - start, final=True)
    return stats


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        address = socket.getaddrinfo(args.host, args.port, socket.AF_INET,
                                     socket.SOCK_DGRAM)[0][4]
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(SEND_TIMEOUT_SECONDS)
            run(args, lambda packet: sock.sendto(packet, address))
    except KeyboardInterrupt:
        return 0
    except OSError as exc:
        print(f"send-ddp.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
