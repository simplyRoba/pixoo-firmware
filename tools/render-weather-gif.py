#!/usr/bin/env python3
"""Render the landscape weather face as animated GIFs, one per condition.

The tool compiles the same public render-test pipeline used by the project and
writes finished GIFs below local/weather-gif/ (git-ignored). Its generated YAML,
ESPHome build directory, and PNG frames are temporary and are always removed.

Run:
    .venv/bin/python tools/render-weather-gif.py
    .venv/bin/python tools/render-weather-gif.py --conditions rainy,snowy --seconds 8
    .venv/bin/python tools/render-weather-gif.py --night
    .venv/bin/python tools/render-weather-gif.py --daycycle
"""
from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parent.parent
TEST_DIR = REPO / "esphome/tests/render_test"
BUILD_NAME = "pixoo64-weather-gif"
OUT_DIR = REPO / "local/weather-gif"

CONDITIONS = [
    "sunny", "partlycloudy", "cloudy", "fog", "drizzle", "freezing-drizzle",
    "rainy", "pouring", "freezing-rain", "snowy", "snow-grains", "thunderstorm",
    "hail-thunderstorm",
]
# Public fixture: Berlin city center (52.5200 N, 13.4050 E).
BERLIN_CITY_CENTER_LATITUDE = 52.5200
BERLIN_CITY_CENTER_LONGITUDE = 13.4050
NOON_EPOCH = 1616421900
NIGHT_EPOCH = 1616460000
DAYCYCLE_START = 1616367600
DAYCYCLE_END = 1616454000

YAML_PREAMBLE = """
external_components:
  - source:
      type: local
      path: ../../components
    components: [pixoo64_content]
  - source:
      type: local
      path: components
    components: [pixoo64_render_test]

font:
  - file: ../../fonts/TomThumb.bdf
    id: font_5
    glyphs: ' !"#$%&''()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~°'
  - file: ../../fonts/PixelOperator8.ttf
    id: font_8
    size: 8
    glyphs: ' !"#$%&''()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~°'
  - file: ../../fonts/PixelOperator.ttf
    id: font_16
    size: 16
    glyphs: ' !"#$%&''()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~°'

text:
  - platform: template
    id: gif_text
    mode: text
    optimistic: true
    initial_value: "Hello"
"""


class Shot:
    """One dashboard plus the animation times to capture from it."""

    def __init__(self, name: str, condition: str, epoch: int, temperature: int, times_ms: list[int]):
        self.name = name
        self.condition = condition
        self.epoch = epoch
        self.temperature = temperature
        self.times_ms = times_ms

    def snapshot(self, index: int) -> str:
        return f"{self.name}_{index:04d}"


def build_yaml(shots: list[Shot], output_dir: Path) -> str:
    source = (TEST_DIR / "render_test.yaml").read_text(encoding="utf-8")
    head = source[: source.index("\nhost:\n")]
    head = head.replace("name: pixoo64-render-test", f"name: {BUILD_NAME}")
    out = [head, "\nhost:\n", YAML_PREAMBLE, "\npixoo64_render_test:\n"]
    for shot in shots:
        out.append(
            f"""  - id: src_{shot.name}
    condition: {shot.condition}
    temperature: {shot.temperature}
    apparent_temperature: {shot.temperature}
    humidity: 60
    high: {shot.temperature + 3}
    low: {shot.temperature - 5}
    start_hour: 14
    forecast_temperatures: [{shot.temperature}, {shot.temperature}, {shot.temperature}]
    location:
      latitude: {BERLIN_CITY_CENTER_LATITUDE}
      longitude: {BERLIN_CITY_CENTER_LONGITUDE}
"""
        )
    out.extend([
        "\npixoo64_content:\n",
        "  id: gif_content\n",
        f"  default_dashboard: {shots[0].name}\n",
        "  notification_font: font_8\n",
        "  firmware_update_title_font: font_16\n",
        "  firmware_update_detail_font: font_5\n",
        "  dashboards:\n",
    ])
    for shot in shots:
        out.append(
            f"""    - platform: weather
      id: dash_{shot.name}
      dashboard_id: {shot.name}
      face: landscape
      font_small: font_5
      font_big: font_16
      fixed_time: {shot.epoch}
      source: src_{shot.name}
"""
        )
    out.extend([
        "\ndisplay:\n",
        "  - platform: pixoo64_render_test\n",
        "    id: gif_display\n",
        "    content_controller: gif_content\n",
        "    animation_frames:\n",
    ])
    for shot in shots:
        for index, now_ms in enumerate(shot.times_ms):
            out.append(f"      - dashboard: {shot.name}\n")
            out.append(f"        now_ms: {now_ms}\n")
            out.append(f"        snapshot: {shot.snapshot(index)}\n")
    out.append(f"    output_dir: {output_dir}\n")
    return "".join(out)


def load_frames(paths: list[Path], scale: int) -> list["Image.Image"]:
    from PIL import Image

    frames = []
    for path in paths:
        with Image.open(path) as image:
            frame = image.convert("RGB")
        if scale != 1:
            frame = frame.resize((64 * scale, 64 * scale), Image.Resampling.NEAREST)
        frames.append(frame)
    return frames


def resolve_esphome(env: dict[str, str] | None = None) -> str:
    """Resolve ESPHome from ESPHOME or the active PATH."""
    env = os.environ if env is None else env
    command = env.get("ESPHOME") or "esphome"
    executable = shutil.which(command, path=env.get("PATH"))
    if executable is None:
        source = "ESPHOME" if env.get("ESPHOME") else "PATH"
        raise FileNotFoundError(f"could not find esphome via {source}; set ESPHOME or add esphome to PATH")
    return executable


def build_binary(build_root: Path) -> Path:
    return build_root / BUILD_NAME / ".pioenvs" / BUILD_NAME / "program"


def finite_positive_float(value: str) -> float:
    try:
        number = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return number


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--conditions", default="all", help="comma-separated condition list, or 'all'")
    parser.add_argument("--seconds", type=finite_positive_float, default=6.0, help="finite animation seconds per GIF (default 6)")
    parser.add_argument("--fps", type=int, default=20, help="frames per second (default 20)")
    parser.add_argument("--scale", type=int, default=4, help="pixel scale of the GIF (default 4 -> 256x256)")
    parser.add_argument("--night", action="store_true", help="render the night scene instead of midday")
    parser.add_argument("--daycycle", action="store_true", help="emit only a sunny 24-hour sun/Moon sweep GIF")
    args = parser.parse_args(argv)
    if args.fps <= 0:
        parser.error("--fps must be positive")
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    if args.scale <= 0:
        parser.error("--scale must be positive")
    if round(1000 / args.fps) <= 0:
        parser.error("--fps is too high for millisecond animation times")
    return args


def make_shots(args: argparse.Namespace, times: list[int]) -> tuple[list[Shot], dict[str, list[str]]]:
    shots: list[Shot] = []
    if args.daycycle:
        for index in range(len(times)):
            epoch = DAYCYCLE_START + int((DAYCYCLE_END - DAYCYCLE_START) * index / (len(times) - 1))
            shots.append(Shot(f"daycycle_{index:04d}", "sunny", epoch, 18, [times[index]]))
        return shots, {"daycycle": [shot.snapshot(0) for shot in shots]}

    if args.conditions == "all":
        conditions = CONDITIONS
    else:
        conditions = [condition.strip() for condition in args.conditions.split(",") if condition.strip()]
        unknown = [condition for condition in conditions if condition not in CONDITIONS]
        if unknown:
            raise SystemExit(f"unknown condition(s): {', '.join(unknown)}\nknown: {', '.join(CONDITIONS)}")
        if not conditions:
            raise SystemExit("provide at least one condition")
    epoch = NIGHT_EPOCH if args.night else NOON_EPOCH
    temperature = -3 if args.night else 18
    suffix = "_night" if args.night else ""
    gifs: dict[str, list[str]] = {}
    for condition in conditions:
        name = condition.replace("-", "_")
        shots.append(Shot(name, condition, epoch, temperature, times))
        gifs[name + suffix] = [f"{name}_{index:04d}" for index in range(len(times))]
    return shots, gifs


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    step_ms = int(round(1000 / args.fps))
    count = max(2, int(round(args.seconds * args.fps)))
    times = [index * step_ms for index in range(count)]
    shots, gifs = make_shots(args, times)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    generated_yaml: Path | None = None
    try:
        try:
            esphome = resolve_esphome()
        except FileNotFoundError as exc:
            raise SystemExit(str(exc)) from exc
        with (
            tempfile.TemporaryDirectory(prefix="weather-gif-frames-", dir=OUT_DIR) as frame_dir,
            tempfile.TemporaryDirectory(prefix="weather-gif-build-") as build_dir,
        ):
            frames_dir = Path(frame_dir)
            build_root = Path(build_dir)
            with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", suffix=".yaml", prefix=".weather-gif-", dir=TEST_DIR, delete=False
            ) as handle:
                generated_yaml = Path(handle.name)
                handle.write(build_yaml(shots, frames_dir))

            compile_env = dict(os.environ, ESPHOME_BUILD_PATH=str(build_root))
            print(f"compiling {len(shots)} dashboard(s)...")
            result = subprocess.run([esphome, "compile", str(generated_yaml)], capture_output=True, text=True, env=compile_env)
            if result.returncode != 0:
                sys.stderr.write((result.stdout + result.stderr)[-4000:])
                raise SystemExit("compile failed")

            total = sum(len(shot.times_ms) for shot in shots)
            print(f"rendering {total} frames...")
            result = subprocess.run(
                [str(build_binary(build_root))], capture_output=True, text=True,
                env=dict(os.environ, PIXOO_UPDATE_SNAPSHOTS="1"),
            )
            if result.returncode != 0:
                sys.stderr.write((result.stdout + result.stderr)[-4000:])
                raise SystemExit("render failed")

            written = []
            for gif_name, snapshots in gifs.items():
                paths = [frames_dir / f"{snapshot}.png" for snapshot in snapshots]
                frames = load_frames(paths, args.scale)
                gif_path = OUT_DIR / f"{gif_name}.gif"
                save_options: dict[str, object] = {
                    "save_all": True, "append_images": frames[1:], "duration": step_ms, "optimize": False,
                }
                if not args.daycycle:
                    save_options["loop"] = 0
                try:
                    frames[0].save(gif_path, **save_options)
                finally:
                    for frame in frames:
                        frame.close()
                written.append(gif_path)

            for path in written:
                print(f"wrote {path.relative_to(REPO)} ({path.stat().st_size // 1024} KiB)")
    finally:
        if generated_yaml is not None:
            generated_yaml.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
