#!/usr/bin/env python3
"""Icon gallery: hero + mini icon side by side for every condition, in a grid.

Reads the per-condition render-test frames (frames/weather_*.png), crops the
hero icon region and the first forecast-strip mini icon, and lays them out as a
labelled grid under local/render-test/. Zoomed for close inspection.
"""
import glob
import os

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAMES = os.path.join(REPO, "esphome/tests/render_test/frames")
OUT = os.path.join(REPO, "local/render-test/icon-gallery.png")
Z = 5          # zoom
COLS = 3       # condition cells per row
HERO = (0, 6, 30, 38)    # crop box of the hero icon in a 64x64 tile
MINI = (0, 44, 16, 62)   # crop box of the first forecast mini icon


def main():
    paths = sorted(glob.glob(os.path.join(FRAMES, "weather_*.png")))
    if not paths:
        raise SystemExit("no weather frames; run the render-test binary first")

    hw = (HERO[2] - HERO[0]) * Z
    hh = (HERO[3] - HERO[1]) * Z
    mw = (MINI[2] - MINI[0]) * Z
    mh = (MINI[3] - MINI[1]) * Z
    label_h = 12
    cell_w = hw + mw + 14
    cell_h = hh + label_h + 8
    rows = (len(paths) + COLS - 1) // COLS
    sheet = Image.new("RGB", (COLS * cell_w + 6, rows * cell_h + 6), (18, 18, 24))
    draw = ImageDraw.Draw(sheet)

    for i, path in enumerate(paths):
        name = os.path.basename(path)[len("weather_"):].split(".")[0]
        im = Image.open(path).convert("RGB")
        hero = im.crop(HERO).resize((hw, hh), Image.NEAREST)
        mini = im.crop(MINI).resize((mw, mh), Image.NEAREST)
        r, c = divmod(i, COLS)
        x = 6 + c * cell_w
        y = 6 + r * cell_h
        draw.text((x, y), name, fill=(210, 210, 220))
        sheet.paste(hero, (x, y + label_h))
        sheet.paste(mini, (x + hw + 8, y + label_h + (hh - mh) // 2))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT)
    print(f"wrote {OUT} ({len(paths)} conditions, {sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()
