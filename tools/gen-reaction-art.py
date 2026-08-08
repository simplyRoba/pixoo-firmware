#!/usr/bin/env python3
"""Generate compact 48x48 reaction artwork from checked-in OpenMoji SVGs.

The source SVGs are rasterized at 192x192, then downsampled with Pillow's
Lanczos filter for anti-aliased 48x48 output. Each output pixel is stored as
RGB565 plus 8-bit alpha; runtime rendering performs bilinear sampling.

Dependencies:
  python -m pip install cairosvg Pillow

Run from anywhere:
  .venv/bin/python tools/gen-reaction-art.py

The command exits with a specific dependency or missing-source error and writes
esphome/components/pixoo64_content/reaction/reaction_art.h. Pass `--check` to
verify that the checked-in header is current without modifying it.
"""

from __future__ import annotations

import argparse
import hashlib
import io
from pathlib import Path
import sys

try:
    import cairosvg
except ImportError as exc:
    raise SystemExit(
        "gen-reaction-art.py requires CairoSVG; install with "
        "'python -m pip install cairosvg Pillow'"
    ) from exc

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "gen-reaction-art.py requires Pillow; install with "
        "'python -m pip install cairosvg Pillow'"
    ) from exc

ART_SIZE = 48
SUPERSAMPLE = 4
RASTER_SIZE = ART_SIZE * SUPERSAMPLE

# Order is the stable numeric order of pixoo::Reaction.
REACTIONS = (
    ("laughing", "1F602.svg"),
    ("love", "2764.svg"),
    ("crying", "1F62D.svg"),
    ("angry", "1F621.svg"),
    ("poop", "1F4A9.svg"),
    ("approve", "1F44D.svg"),
    ("disapprove", "1F44E.svg"),
    ("celebrate", "1F389.svg"),
    ("thinking", "1F914.svg"),
    ("surprised", "1F62E.svg"),
    ("fire", "1F525.svg"),
    ("eyes", "1F440.svg"),
)


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def rasterize(path: Path) -> tuple[list[int], list[int], str]:
    source = path.read_bytes()
    png = cairosvg.svg2png(
        bytestring=source,
        output_width=RASTER_SIZE,
        output_height=RASTER_SIZE,
    )
    with Image.open(io.BytesIO(png)) as image:
        rgba = image.convert("RGBA").resize(
            (ART_SIZE, ART_SIZE), Image.Resampling.LANCZOS
        )
        alpha = rgba.getchannel("A").tobytes()
        indexed = rgba.convert("RGB").quantize(
            colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE
        )
        raw_palette = indexed.getpalette()[: 64 * 3]
        palette = [
            rgb565(*raw_palette[offset : offset + 3])
            for offset in range(0, len(raw_palette), 3)
        ]
        palette.extend([0] * (64 - len(palette)))
        pixels: list[int] = []
        for index, coverage in zip(indexed.tobytes(), alpha):
            pixels.extend((index, coverage))
    return palette, pixels, hashlib.sha256(source).hexdigest()[:12]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true", help="verify generated output is current"
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    source_dir = repo / "resources" / "openmoji-17.0.0"
    output = (
        repo
        / "esphome"
        / "components"
        / "pixoo64_content"
        / "reaction"
        / "reaction_art.h"
    )

    missing = [filename for _, filename in REACTIONS if not (source_dir / filename).is_file()]
    if missing:
        raise SystemExit(
            f"missing OpenMoji 17.0.0 source SVGs in {source_dir}: "
            + ", ".join(missing)
        )

    art = []
    hashes = []
    for name, filename in REACTIONS:
        palette, pixels, digest = rasterize(source_dir / filename)
        art.append((name, palette, pixels))
        hashes.append((filename, digest))

    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace esphome::pixoo64::content::reaction_art {",
        "",
        "// OpenMoji 17.0.0 artwork, CC BY-SA 4.0. Rasterized at 4x and",
        "// Lanczos-downsampled by tools/gen-reaction-art.py. Each pixel is a",
        "// 64-color palette index followed by 8-bit alpha; palette entries are RGB565.",
        f"constexpr int kArtSize = {ART_SIZE};",
        "constexpr int kPaletteSize = 64;",
        f"constexpr int kReactionCount = {len(REACTIONS)};",
        "",
        "constexpr uint16_t kPalette[kReactionCount][kPaletteSize] = {",
    ]
    for (name, palette, _), (filename, digest) in zip(art, hashes):
        lines.append(f"    // {name}: {filename}, source sha256 {digest}")
        lines.append("    {")
        for offset in range(0, len(palette), 8):
            row = ", ".join(f"0x{value:04X}" for value in palette[offset : offset + 8])
            lines.append(f"        {row},")
        lines.append("    },")
    lines += [
        "};",
        "",
        "constexpr uint8_t kPixels[kReactionCount][kArtSize * kArtSize * 2] = {",
    ]
    for (name, _, pixels), (filename, digest) in zip(art, hashes):
        lines.append(f"    // {name}: {filename}, source sha256 {digest}")
        lines.append("    {")
        for offset in range(0, len(pixels), 24):
            row = ", ".join(f"0x{value:02X}" for value in pixels[offset : offset + 24])
            lines.append(f"        {row},")
        lines.append("    },")
    lines += [
        "};",
        "",
        "}  // namespace esphome::pixoo64::content::reaction_art",
        "",
    ]
    generated = "\n".join(lines)
    if args.check:
        if not output.is_file() or output.read_text(encoding="utf-8") != generated:
            raise SystemExit(f"generated reaction artwork is stale: {output}")
        print(f"up to date: {output}")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generated, encoding="utf-8", newline="\n")
    print(f"wrote {output}")


if __name__ == "__main__":
    main()
