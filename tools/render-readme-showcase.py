#!/usr/bin/env python3
"""Build the README's Pixoo64 showcase from five committed renderer snapshots."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageFilter

REPO = Path(__file__).resolve().parent.parent
FRAMES = REPO / "esphome/tests/render_test/frames"
OUTPUT = REPO / "docs/images/readme-showcase.png"
SNAPSHOTS = (
    "weather_landscape_day.png",
    "now_playing_showcase.png",
    "equalizer_waveform_color.png",
    "notify_warning_analog.png",
    "reaction_celebrate_weather.png",
)
PANEL_SIZE = 64
PITCH = 6
LED_SIZE = 5
BEZEL = 8
GRID_SIZE = PANEL_SIZE * PITCH
OUTER_SIZE = GRID_SIZE + BEZEL * 2
CANVAS_SIZE = (1440, 1000)
TOP_Y = 60
BOTTOM_Y = 540
TOP_X = (90, 520, 950)
BOTTOM_X = (305, 735)


def read_snapshot(name: str) -> Image.Image:
    path = FRAMES / name
    try:
        with Image.open(path) as source:
            if source.size != (PANEL_SIZE, PANEL_SIZE):
                raise ValueError(
                    f"{path}: expected {PANEL_SIZE}x{PANEL_SIZE}, got {source.size}"
                )
            if source.mode != "RGB":
                raise ValueError(f"{path}: expected RGB mode, got {source.mode}")
            return source.copy()
    except FileNotFoundError as error:
        raise ValueError(f"missing required snapshot: {path}") from error


def led_panel(source: Image.Image) -> Image.Image:
    """Render each source pixel as one square LED without resampling it."""
    panel = Image.new("RGB", (OUTER_SIZE, OUTER_SIZE), (1, 2, 4))
    grid = Image.new("RGB", (GRID_SIZE, GRID_SIZE), (5, 7, 11))
    pixels = source.load()
    for y in range(PANEL_SIZE):
        for x in range(PANEL_SIZE):
            led = Image.new("RGB", (LED_SIZE, LED_SIZE), pixels[x, y])
            grid.paste(led, (x * PITCH, y * PITCH))
    panel.paste(grid, (BEZEL, BEZEL))
    return panel


def ambient_canvas() -> Image.Image:
    canvas = Image.new("RGB", CANVAS_SIZE)
    pixels = canvas.load()
    width, height = CANVAS_SIZE
    for y in range(height):
        level = y / (height - 1)
        base = (19 + int(5 * level), 24 + int(6 * level), 33 + int(10 * level))
        for x in range(width):
            distance = abs(x - width / 2) / (width / 2)
            lift = int(4 * max(0.0, 1.0 - distance) * (1.0 - level * 0.45))
            pixels[x, y] = (base[0] + lift, base[1] + lift, base[2] + lift)
    return canvas


def compose() -> Image.Image:
    sources = [read_snapshot(name) for name in SNAPSHOTS]
    canvas = ambient_canvas()
    positions = tuple(zip(TOP_X, (TOP_Y,) * 3)) + tuple(zip(BOTTOM_X, (BOTTOM_Y,) * 2))
    for source, position in zip(sources, positions, strict=True):
        x, y = position
        shadow = Image.new("RGBA", (OUTER_SIZE + 24, OUTER_SIZE + 24), (0, 0, 0, 0))
        shadow.paste((0, 0, 0, 115), (12, 14, OUTER_SIZE + 12, OUTER_SIZE + 14))
        shadow = shadow.filter(ImageFilter.GaussianBlur(7))
        canvas.paste(shadow, (x - 12, y - 12), shadow)
        canvas.paste(led_panel(source), position)
    return canvas


def images_match(left: Image.Image, right: Image.Image) -> bool:
    return left.mode == right.mode and left.size == right.size and left.tobytes() == right.tobytes()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="verify the tracked PNG matches generated pixels"
    )
    args = parser.parse_args()
    generated = compose()
    if args.check:
        try:
            with Image.open(OUTPUT) as tracked:
                tracked.load()
                if not images_match(tracked, generated):
                    print(f"README showcase differs: {OUTPUT}", file=sys.stderr)
                    return 1
        except FileNotFoundError:
            print(f"missing README showcase: {OUTPUT}", file=sys.stderr)
            return 1
        print(f"README showcase is current: {OUTPUT}")
        return 0
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    generated.save(OUTPUT, format="PNG", optimize=True)
    print(f"wrote {OUTPUT} ({generated.width}x{generated.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
