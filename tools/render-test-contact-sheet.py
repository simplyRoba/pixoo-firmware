#!/usr/bin/env python3
"""Assemble the committed render-test frames into one scaled contact sheet.

Reads all frames/*.png (64x64), scales each by SCALE with nearest-neighbor,
labels it, and lays them out in a grid under local/render-test/ (a git-ignored
view composite for eyeballing every rendered dashboard at once).
"""
import glob
import os

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAMES = os.path.join(REPO, "esphome/tests/render_test/frames")
OUT = os.path.join(REPO, "local/render-test/contact-sheet.png")
SCALE = 4
COLS = 5
PAD = 6
LABEL_H = 10


def main():
    paths = sorted(
        p for p in glob.glob(os.path.join(FRAMES, "*.png"))
        if not os.path.basename(p).startswith("_")
    )
    if not paths:
        raise SystemExit("no frames; run the render-test binary first")

    tile = 64 * SCALE
    cell_w = tile + PAD
    cell_h = tile + LABEL_H + PAD
    rows = (len(paths) + COLS - 1) // COLS
    sheet = Image.new("RGB", (COLS * cell_w + PAD, rows * cell_h + PAD),
                      (24, 24, 30))
    draw = ImageDraw.Draw(sheet)

    for i, path in enumerate(paths):
        img = Image.open(path).convert("RGB").resize((tile, tile), Image.NEAREST)
        r, c = divmod(i, COLS)
        x = PAD + c * cell_w
        y = PAD + r * cell_h
        sheet.paste(img, (x, y))
        name = os.path.splitext(os.path.basename(path))[0]
        draw.text((x, y + tile + 1), name, fill=(200, 200, 210))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT)
    print(f"wrote {OUT} ({len(paths)} tiles, {sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()
