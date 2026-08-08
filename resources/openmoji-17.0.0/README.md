# OpenMoji 17.0.0 reaction artwork

The SVG files in this directory are the official OpenMoji color artwork from the
exact `17.0.0` tag:

https://github.com/hfg-gmuend/openmoji/tree/17.0.0/color/svg

All emojis designed by OpenMoji — the open-source emoji and icon project.
OpenMoji is licensed under Creative Commons Attribution-ShareAlike 4.0
International (CC BY-SA 4.0): https://creativecommons.org/licenses/by-sa/4.0/
The complete license is in `LICENSE.txt`.

The firmware uses these mappings:

| Reaction | Unicode | Source |
|---|---|---|
| laughing | 😂 U+1F602 | `1F602.svg` |
| love | ❤️ U+2764 U+FE0F | `2764.svg` |
| crying | 😭 U+1F62D | `1F62D.svg` |
| angry | 😡 U+1F621 | `1F621.svg` |
| poop | 💩 U+1F4A9 | `1F4A9.svg` |
| approve | 👍 U+1F44D | `1F44D.svg` |
| disapprove | 👎 U+1F44E | `1F44E.svg` |
| celebrate | 🎉 U+1F389 | `1F389.svg` |
| thinking | 🤔 U+1F914 | `1F914.svg` |
| surprised | 😮 U+1F62E | `1F62E.svg` |
| fire | 🔥 U+1F525 | `1F525.svg` |
| eyes | 👀 U+1F440 | `1F440.svg` |

`tools/gen-reaction-art.py` modifies the source artwork by resampling it into
48×48 anti-aliased, palette-compressed C++ data. The firmware further adapts it
with animated transforms over a blurred background. These adapted artwork forms
remain under CC BY-SA 4.0.
