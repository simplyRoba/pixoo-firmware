#!/usr/bin/env python3
"""Render a 64x64 Pixoo RGB frame payload to a PNG for visual inspection.

Input is the 12288-byte (64*64*3) RGB payload of a full-frame push (cmd 0x00),
e.g. the file written by `decode-panel-spi.py --dump-payload`. The point is to
turn "is the pixel byte order right?" into a glance: a correct order shows the
captured image; a wrong one shows scrambled/again-shifted colors.

Pixel order is fully configurable here so a capture can be checked without
assuming a layout:
  --order   row / column major
  --origin  where pixel (0,0) sits (top-left default)
  --channels channel byte order within a pixel (rgb default)

No third-party dependencies: PNG is written via stdlib zlib.
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

WIDTH = 64
HEIGHT = 64
CHANNELS = 3
PAYLOAD_BYTES = WIDTH * HEIGHT * CHANNELS


def write_png(path: Path, pixels: list[list[tuple[int, int, int]]], scale: int) -> None:
    """pixels[y][x] = (r,g,b). Nearest-neighbour upscaled by `scale`."""
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    raw = bytearray()
    for row in pixels:
        for _ in range(scale):
            raw.append(0)  # PNG filter type 0 (none) per scanline
            for (r, g, b) in row:
                raw.extend((r, g, b) * scale)
    compressed = zlib.compress(bytes(raw), 9)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width * scale, height * scale, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", compressed)
           + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def load_payload(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != PAYLOAD_BYTES:
        raise ValueError(
            f"expected {PAYLOAD_BYTES} bytes (64*64*3), got {len(data)}"
        )
    return data


def to_grid(payload: bytes, order: str, origin: str, channels: str) -> list[list[tuple[int, int, int]]]:
    ci = {c: channels.index(c) for c in "rgb"}
    grid = [[(0, 0, 0)] * WIDTH for _ in range(HEIGHT)]
    for pixel_index in range(WIDTH * HEIGHT):
        base = pixel_index * CHANNELS
        triplet = payload[base:base + CHANNELS]
        r = triplet[ci["r"]]
        g = triplet[ci["g"]]
        b = triplet[ci["b"]]

        if order == "row":
            x = pixel_index % WIDTH
            y = pixel_index // WIDTH
        else:  # column-major
            y = pixel_index % HEIGHT
            x = pixel_index // HEIGHT

        if "right" in origin:
            x = WIDTH - 1 - x
        if "bottom" in origin:
            y = HEIGHT - 1 - y
        grid[y][x] = (r, g, b)
    return grid


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input", type=Path, help="raw 12288-byte RGB payload (.bin)")
    p.add_argument("output", type=Path, help="PNG to write")
    p.add_argument("--order", choices=("row", "column"), default="row", help="pixel scan order (default row-major)")
    p.add_argument("--origin", choices=("top-left", "top-right", "bottom-left", "bottom-right"),
                   default="top-left", help="where pixel (0,0) is placed (default top-left)")
    p.add_argument("--channels", default="rgb", help="channel byte order within a pixel (default rgb; e.g. bgr, grb)")
    p.add_argument("--scale", type=int, default=8, help="upscale factor for visibility (default 8 -> 512x512)")
    return p


def main() -> int:
    args = build_parser().parse_args()
    if sorted(args.channels.lower()) != ["b", "g", "r"]:
        print("error: --channels must be a permutation of r,g,b", file=sys.stderr)
        return 2
    if args.scale < 1:
        print("error: --scale must be >= 1", file=sys.stderr)
        return 2
    try:
        payload = load_payload(args.input)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    grid = to_grid(payload, args.order, args.origin, args.channels.lower())
    write_png(args.output, grid, args.scale)
    print(f"wrote {args.output} ({WIDTH * args.scale}x{HEIGHT * args.scale}, "
          f"order={args.order}, origin={args.origin}, channels={args.channels.lower()})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
