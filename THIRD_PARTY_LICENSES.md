# Third-party license notices

This notice covers third-party artwork and fonts checked into this repository
and libjpeg-turbo linked into firmware and host render binaries. Their terms are
separate from the AGPL-3.0-or-later license for original project material.

## libjpeg-turbo 3.2.0

This software is based in part on the work of the Independent JPEG Group.

- **Material:** the libjpeg API library used for progressive artwork decoding,
  statically linked into the firmware and host render target. The TurboJPEG API
  and upstream command-line programs are not used.
- **Source:** upstream [libjpeg-turbo 3.2.0](https://github.com/libjpeg-turbo/libjpeg-turbo/tree/3.2.0),
  obtained on target through the pinned managed `espressif/libjpeg-turbo`
  component `==3.2.0~1` and on host from the checksummed upstream release archive.
- **License:** the IJG License applies to the libjpeg API library. Upstream's
  `LICENSE.md` also contains the Modified (3-clause) BSD License for the build/test
  system, TurboJPEG API, and associated programs, and describes component-license
  boundaries.
- **Retained notices:** complete, unaltered upstream
  [`LICENSE.md`](resources/libjpeg-turbo-3.2.0/LICENSE.md) and
  [`README.ijg`](resources/libjpeg-turbo-3.2.0/README.ijg), copied from the pinned
  managed component's `libjpeg-turbo/` source directory and identical to those in
  the host 3.2.0 archive. The latter includes the IJG copyright, warranty,
  liability, and distribution terms. Include this attribution with binary
  distributions and preserve applicable upstream notices with source
  distributions.
- **Integration:** project code supplies the memory backend in place of
  upstream `jmemnobs`; it does not modify the upstream libjpeg source files.
  Downloaded source and build outputs remain in generated `.esphome` trees.
  `resources/libjpeg-turbo-3.2.0/` contains only redistributable upstream notices,
  not library source, private images, or device captures.

## OpenMoji 17.0.0 color artwork

- **Material:** the following 12 SVG inputs in `resources/openmoji-17.0.0/`:
  `1F602.svg`, `2764.svg`, `1F62D.svg`, `1F621.svg`, `1F4A9.svg`, `1F44D.svg`,
  `1F44E.svg`, `1F389.svg`, `1F914.svg`, `1F62E.svg`, `1F525.svg`, and
  `1F440.svg`.
- **Source:** the official OpenMoji color artwork, version 17.0.0, exact commit
  [`f9fc506a3f913be9897ab0181d611d4c910a4104`](https://github.com/hfg-gmuend/openmoji/tree/f9fc506a3f913be9897ab0181d611d4c910a4104/color/svg).
- **Attribution:** All emojis designed by OpenMoji — the open-source emoji and
  icon project.
- **License:** [Creative Commons Attribution-ShareAlike 4.0 International (CC
  BY-SA 4.0)](https://creativecommons.org/licenses/by-sa/4.0/). The complete
  legal code is retained in `resources/openmoji-17.0.0/LICENSE.txt`.
- **Adaptation:** `esphome/components/pixoo64_content/reaction/reaction_art.h`
  is adapted material generated from these SVGs by
  `tools/gen-reaction-art.py` as 48×48 anti-aliased, palette-compressed C++ data.
  `docs/images/readme-showcase.png` incorporates the celebration reaction render
  derived from that material; it is covered by this attribution and CC BY-SA 4.0,
  including the ShareAlike requirement.

The reaction-to-source mapping and additional attribution are retained in
`resources/openmoji-17.0.0/README.md`.

## Pixel Operator fonts

- **Material:** `esphome/fonts/PixelOperator.ttf` and
  `esphome/fonts/PixelOperator8.ttf`.
- **Attribution:** Pixel Operator by Jayvee Enaguas/HarvettFox96.
- **Embedded metadata:** states Creative Commons Zero (CC0) 1.0 and
  `(c) 2009-2018`.
- **License:** CC0 1.0 Universal. The complete legal code is retained in
  `esphome/fonts/PixelOperator-LICENSE.txt`.
- **Local SHA-256:** `PixelOperator.ttf` =
  `8d805274eaf227855147153182a96a86ff395ddbc7d2d378095af8831b764a3e`;
  `PixelOperator8.ttf` =
  `5cccb9ef6cf18977b6e5721d49a1a6e78dd6a6f1c4f69537470f7dc1dc829ffc`.

## Tom Thumb (Fixed4x6) font

- **Material:** `esphome/fonts/TomThumb.bdf`.
- **License:** the local BDF metadata declares MIT. The full MIT license text is
  retained in `esphome/fonts/TomThumb-LICENSE.txt`.
- **Source and attribution:** Robey Pointer,
  <https://robey.lag.net/2010/01/23/tiny-monospace-font.html>.
- **Local SHA-256:**
  `d2c8c15de5ca83fcaef7cadc06d8db578c082507fa3da8a8f698d026fdda2b14`.

Dependencies installed from `requirements.txt` are distributed separately and
are not enumerated by this notice.
