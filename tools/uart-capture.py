#!/usr/bin/env python3
"""Passively capture UART output until it falls silent after receiving data.

Example:
  tools/uart-capture.py --port <serial-port> --output local/uart/boot-log.txt
"""
from __future__ import annotations

import argparse
import os
import sys
import time

WAIT_FOR_FIRST_BYTE = 120  # seconds to wait before giving up entirely
IDLE_AFTER_DATA = 30  # seconds of silence after real data before exit
MIN_REAL_BYTES = 8  # ignore noise until we've seen at least this many bytes


def positive_baud(value: str) -> int:
    try:
        baud = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("baud must be an integer") from exc
    if baud <= 0:
        raise argparse.ArgumentTypeError("baud must be positive")
    return baud


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial device path or port name")
    parser.add_argument("--output", required=True, help="file to receive captured bytes")
    parser.add_argument("--baud", type=positive_baud, default=115200, help="baud rate (default: 115200)")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("uart-capture.py requires pyserial") from exc

    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    print(f"[uart-capture] {args.port} @ {args.baud} -> {args.output}", flush=True)
    print(
        f"[uart-capture] waiting up to {WAIT_FOR_FIRST_BYTE}s for data — power-cycle the device now",
        flush=True,
    )

    with serial.Serial(args.port, args.baud, timeout=1, rtscts=False, dsrdtr=False) as ser:
        ser.setRTS(False)
        ser.setDTR(False)
        with open(args.output, "wb") as output:
            total = 0
            start = time.time()
            last_data = None
            while True:
                try:
                    chunk = ser.read(256)
                except serial.SerialException as exc:
                    if total:
                        print(
                            f"\n[uart-capture] serial device disconnected after {total} bytes: {exc}",
                            flush=True,
                        )
                        break
                    raise
                if chunk:
                    output.write(chunk)
                    output.flush()
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
                    total += len(chunk)
                    if total >= MIN_REAL_BYTES:
                        last_data = time.time()
                    continue
                now = time.time()
                if last_data and now - last_data > IDLE_AFTER_DATA:
                    print(f"\n[uart-capture] done. {total} bytes saved to {args.output}", flush=True)
                    break
                if not last_data and now - start > WAIT_FOR_FIRST_BYTE:
                    print(
                        f"\n[uart-capture] no data after {WAIT_FOR_FIRST_BYTE}s. Check wiring.",
                        flush=True,
                    )
                    break
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
