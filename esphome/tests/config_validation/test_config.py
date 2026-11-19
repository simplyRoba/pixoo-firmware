#!/usr/bin/env python3
"""ESPHome configuration regressions for the Pixoo64 board profile."""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import freetype
import yaml


ROOT = Path(__file__).resolve().parents[3]


class EspHomeConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory()
        cls.fixture = Path(cls.tempdir.name) / "repo"
        shutil.copytree(
            ROOT,
            cls.fixture,
            ignore=shutil.ignore_patterns(
                ".git",
                ".venv",
                "local",
                ".env",
                ".env.*",
                "secrets.yaml",
                ".esphome",
                ".pio",
                ".pytest_cache",
                ".mypy_cache",
                "__pycache__",
                "*.pyc",
            ),
        )
        shutil.copyfile(
            cls.fixture / "esphome" / "secrets.example.yaml",
            cls.fixture / "esphome" / "secrets.yaml",
        )

    @classmethod
    def tearDownClass(cls):
        cls.tempdir.cleanup()

    def run_config(self, repo: Path, config="esphome/pixoo64.yaml"):
        return subprocess.run(
            [
                sys.executable,
                "-m",
                "esphome",
                "config",
                config,
            ],
            cwd=repo,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )

    def test_production_config_is_valid(self):
        result = self.run_config(self.fixture)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_dashboard_legacy_options_are_explicit(self):
        script = """
from pathlib import Path
from esphome.core import CORE
from esphome.config import read_config
CORE.config_path = Path('esphome/pixoo64.yaml').resolve()
assert read_config({}) is not None
from esphome.components.pixoo64.select import DASHBOARD_SCHEMA
import esphome.config_validation as cv
base = dict(name='No migration', options=['text', 'ddp'], initial_option='text')
assert 'legacy_options' not in DASHBOARD_SCHEMA(base)
assert DASHBOARD_SCHEMA(dict(base, name='Explicit migration', legacy_options=['ddp', 'text', 'removed']))['legacy_options'] == ['ddp', 'text', 'removed']
for index, changes in enumerate((dict(legacy_options=['ddp', 'ddp']), dict(options=['text', 'text']), dict(initial_option='missing'))):
    try:
        DASHBOARD_SCHEMA(dict(base, **changes, name=f'Invalid {index}'))
    except cv.Invalid:
        pass
    else:
        raise AssertionError(changes)
"""
        result = subprocess.run(
            [sys.executable, "-c", script], cwd=self.fixture,
            text=True, capture_output=True, timeout=60, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "compile", "--only-generate", "esphome/pixoo64.yaml"],
            cwd=self.fixture, text=True, capture_output=True, timeout=60, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        generated = (self.fixture / "esphome/.esphome/build/pixoo64/src/main.cpp").read_text()
        self.assertRegex(generated, r'panel_dashboard->set_legacy_options\(\{.*?"stopwatch".*?"timer".*?"ddp".*?\}\);')

    def test_dashboard_ids_use_utf8_byte_limits(self):
        script = """
from pathlib import Path
from esphome.core import CORE
from esphome.config import read_config
CORE.config_path = Path('esphome/pixoo64.yaml').resolve()
assert read_config({}) is not None
from esphome.components.pixoo64.select import DASHBOARD_SCHEMA
import esphome.config_validation as cv
for field in ('options', 'legacy_options'):
    for value, valid in [('é' * 15, True), ('é' * 16, False), ('x' * 31, True), ('x' * 32, False)]:
        config = dict(name=f'{field} {len(value)} {value[0]}', options=['text'], initial_option='text')
        config[field] = [value]
        if field == 'options':
            config['initial_option'] = value
        try:
            validated = DASHBOARD_SCHEMA(config)
        except cv.Invalid:
            assert not valid, (field, value)
        else:
            assert valid, (field, value)
            assert validated[field] == [value]
"""
        result = subprocess.run(
            [sys.executable, "-c", script], cwd=self.fixture,
            text=True, capture_output=True, timeout=60, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ddp_codegen(self):
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "compile", "--only-generate", "esphome/pixoo64.yaml"],
            cwd=self.fixture, text=True, capture_output=True, timeout=60, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        generated = (self.fixture / "esphome/.esphome/build/pixoo64/src/main.cpp").read_text()
        self.assertIn("new(ddp_dashboard) pixoo64::dashboard::DdpDashboard();", generated)
        self.assertRegex(generated, r"App\.register_component_\(ddp_dashboard, \d+\);")
        self.assertIn("ddp_dashboard->set_frame_interval_ms(33);", generated)
        self.assertIn("pixoo_firmware->set_frame_metrics_window_ms(30000);", generated)
        self.assertIn("panel_content_controller->set_update_interval(4294967295UL);", generated)
        self.assertIn("panel_content_controller->add_dashboard(ddp_dashboard);", generated)
        sdkconfig = (self.fixture / "esphome/.esphome/build/pixoo64/"
                     "sdkconfig.pixoo64.esphomeinternal").read_text()
        self.assertIn("CONFIG_LWIP_UDP_RECVMBOX_SIZE=32\n", sdkconfig)

    def test_ddp_registers_only_its_configured_udp_socket(self):
        script = (
            "from pathlib import Path; from esphome.core import CORE; "
            "from esphome.config import read_config; "
            "from esphome.components.socket import get_socket_counts; "
            "CORE.config_path = Path('esphome/pixoo64.yaml').resolve(); "
            "assert read_config({}) is not None; "
            "print(get_socket_counts().udp_details)"
        )
        with tempfile.TemporaryDirectory() as tempdir:
            repo = Path(tempdir) / "repo"
            shutil.copytree(self.fixture, repo)
            for configured in (True, False):
                with self.subTest(configured=configured):
                    if not configured:
                        path = repo / "esphome/pixoo64.yaml"
                        text = path.read_text()
                        text = re.sub(r"    - platform: ddp\n(?:      .*\n)+", "", text)
                        path.write_text(text.replace("      - ddp\n", ""))
                    result = subprocess.run(
                        [sys.executable, "-c", script], cwd=repo,
                        text=True, capture_output=True, timeout=60, check=False,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual("pixoo64_content.ddp=1" in result.stdout, configured)

    def test_now_playing_codegen_pins_libjpeg_and_disables_allocator_wrappers(self):
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "compile", "--only-generate", "esphome/pixoo64.yaml"],
            cwd=self.fixture, text=True, capture_output=True, timeout=60, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        build = self.fixture / "esphome/.esphome/build/pixoo64"
        manifest = yaml.safe_load((build / "src/idf_component.yml").read_text())
        self.assertEqual(
            manifest["dependencies"]["espressif/libjpeg-turbo"]["version"],
            "==3.2.0~1",
        )
        self.assertIn(
            "CONFIG_LIBJPEG_TURBO_ALLOC_PREFER_SPIRAM=n",
            (build / "sdkconfig.pixoo64.esphomeinternal").read_text(),
        )

    def test_now_playing_api_session_callback_codegen(self):
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "compile", "--only-generate", "esphome/pixoo64.yaml"],
            cwd=self.fixture, text=True, capture_output=True, timeout=60, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        generated = (self.fixture / "esphome/.esphome/build/pixoo64/src/main.cpp").read_text()
        self.assertIn("panel_now_playing->OnApiClientConnected(client_info);", generated)
        self.assertIn("panel_api->get_client_connected_trigger()", generated)

    def test_pixel_operator_glyph_inventory_matches_fonts_and_sanitizer(self):
        inventory_path = self.fixture / "esphome/fonts/pixel_operator_glyphs.yaml"
        inventory = yaml.safe_load(inventory_path.read_text(encoding="utf-8"))

        def flatten(value):
            if isinstance(value, str):
                return list(value)
            if isinstance(value, list):
                return [character for item in value for character in flatten(item)]
            self.fail(f"glyph inventory contains {type(value).__name__}, not a string or list")

        glyphs = flatten(inventory)
        self.assertEqual(len(glyphs), 238)
        self.assertEqual(len(set(glyphs)), 238)
        self.assertTrue(set("ÄÖÜäöüß").issubset(glyphs))
        expected_codepoints = {ord(character) for character in glyphs}

        for font_name in ("PixelOperator.ttf", "PixelOperator8.ttf"):
            face = freetype.Face(str(inventory_path.parent / font_name))
            codepoints = set()
            codepoint, glyph_index = face.get_first_char()
            while glyph_index:
                codepoints.add(codepoint)
                codepoint, glyph_index = face.get_next_char(codepoint, glyph_index)
            self.assertSetEqual(codepoints, expected_codepoints, font_name)

        sanitizer = (
            self.fixture / "pixoo_content/src/dashboard/now_playing/now_playing_text.cpp"
        ).read_text(encoding="utf-8")
        match = re.search(
            r"ranges\[\]\s*=\s*\{(?P<body>.*?)\n\s*\};",
            sanitizer,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(match, "could not find now-playing sanitizer ranges")
        range_pattern = re.compile(r"\{\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*\}")
        range_body = match.group("body")
        ranges = [(int(first, 16), int(last, 16)) for first, last in range_pattern.findall(range_body)]
        self.assertTrue(ranges, "now-playing sanitizer has no ranges")
        self.assertRegex(range_pattern.sub("", range_body), r"^[\s,]*$")
        sanitizer_codepoints = {
            codepoint
            for first, last in ranges
            for codepoint in range(first, last + 1)
        }
        self.assertSetEqual(sanitizer_codepoints, expected_codepoints)

    def test_render_test_config_is_valid(self):
        result = self.run_config(self.fixture, "esphome/tests/render_test/render_test.yaml")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_render_fixture_rejects_unpaired_artwork_content_identity(self):
        with tempfile.TemporaryDirectory() as tempdir:
            repo = Path(tempdir) / "repo"
            shutil.copytree(self.fixture, repo)
            render_path = repo / "esphome/tests/render_test/render_test.yaml"
            text = render_path.read_text(encoding="utf-8")
            anchor = "        artwork_identity: 5001\n"
            self.assertIn(anchor, text)
            render_path.write_text(
                text.replace(
                    anchor, anchor + "        artwork_content_identity: 5002\n", 1
                ),
                encoding="utf-8",
            )
            result = self.run_config(
                repo, "esphome/tests/render_test/render_test.yaml"
            )
            output = result.stdout + result.stderr
            self.assertNotEqual(result.returncode, 0, output)
            self.assertIn(
                "artwork_content_identity and artwork_content_revision must appear together",
                output,
            )

    def test_timing_codegen(self):
        actions = ["stopwatch_start", "timer_start", "timing_pause", "timing_resume",
                   "timing_stop", "timing_minimize", "timing_show"]
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "compile", "--only-generate", "esphome/pixoo64.yaml"],
            cwd=self.fixture, text=True, capture_output=True, timeout=60, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        generated = (self.fixture / "esphome/.esphome/build/pixoo64/src/main.cpp").read_text()
        for statement in (
            "pixoo64::DashboardSelect();", "pixoo64::TimingPauseAction<>",
            "pixoo_firmware->set_timer_duration(timing_timer_duration);",
            "pixoo_firmware->set_timing_mode(", "pixoo_firmware->set_timing_state(",
            "pixoo_firmware->set_timing_visible(",
            "pixoo_firmware->set_stopwatch_elapsed(", "pixoo_firmware->set_timer_remaining(",
        ):
            self.assertIn(statement, generated)
        self.assertEqual(generated.count("pixoo_firmware->set_timing_button("), 7)
        visible = re.search(r"pixoo_firmware->set_timing_visible\((\w+)\);", generated)
        self.assertIsNotNone(visible)
        self.assertRegex(generated, rf'App\.register_binary_sensor\({visible.group(1)}, (?:ESPHOME_PSTR\()?"Timing Visible"')
        for index, action in enumerate(actions):
            with self.subTest(action=action):
                binding = re.search(
                    rf"pixoo_firmware->set_timing_button\({index}, (\w+)\);", generated)
                self.assertIsNotNone(binding)
                name = action.replace("_", " ").title()
                self.assertRegex(generated, rf'App\.register_button\({binding.group(1)}, (?:ESPHOME_PSTR\()?"{name}"')
                command = "".join(word.title() for word in action.split("_"))
                if action != "timer_start":
                    self.assertIn(f"pixoo64::{command}Action<>(pixoo_firmware);", generated)

        self.assertNotIn("SelectDashboard(x)", generated)

    def test_timing_controls_and_visible_required(self):
        for action in ("stopwatch_start", "timer_start", "timing_pause", "timing_resume",
                       "timing_stop", "timing_minimize", "timing_show", "visible"):
            with self.subTest(action=action), tempfile.TemporaryDirectory() as tempdir:
                repo = Path(tempdir) / "repo"
                shutil.copytree(self.fixture, repo, ignore=shutil.ignore_patterns(".esphome"))
                path = repo / "esphome/pixoo64.yaml"
                name = "Timing Visible" if action == "visible" else action.replace("_", " ").title()
                path.write_text(path.read_text().replace(
                    f"    {action}:\n      name: {name}\n", "", 1))
                result = self.run_config(repo)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"'{action}' is a required option", result.stdout + result.stderr)

    def test_timing_action_validation(self):
        cases = [
            (f"        - pixoo64.{old}: pixoo_firmware\n", "Unable to find action")
            for old in ("stopwatch_reset", "timer_set", "timer_reset", "timing_start",
                        *(f"{kind}_{verb}" for kind in ("stopwatch", "timer")
                          for verb in ("pause", "resume", "stop", "minimize", "show")))
        ]
        cases += [
            ("        - pixoo64.timer_start:\n            id: pixoo_firmware\n", "'duration_seconds' is a required option"),
            ("        - pixoo64.timer_start:\n            id: pixoo_firmware\n            duration_ms: 1000\n", "duration_ms"),
            ("        - pixoo64.timer_start:\n            id: pixoo_firmware\n            duration_seconds: 0\n", "at least 1"),
            ("        - pixoo64.timer_start:\n            id: pixoo_firmware\n            duration_seconds: 6000\n", "at most 5999"),
            ("        - pixoo64.timer_start:\n            id: pixoo_firmware\n            duration_seconds: 1.5\n", "integer"),
        ]
        cases += [
            (f"        - pixoo64.{action}:\n            id: pixoo_firmware\n"
             "            duration_seconds: 300\n", "duration_seconds")
            for action in ("stopwatch_start", "timing_pause", "timing_resume", "timing_stop",
                           "timing_minimize", "timing_show")
        ]
        for replacement, message in cases:
            with self.subTest(replacement=replacement), tempfile.TemporaryDirectory() as tempdir:
                repo = Path(tempdir) / "repo"
                shutil.copytree(self.fixture, repo, ignore=shutil.ignore_patterns(".esphome"))
                path = repo / "esphome/pixoo64.yaml"
                path.write_text(path.read_text().replace(
                    "        - pixoo64.timing_pause: pixoo_firmware\n", replacement, 1))
                result = self.run_config(repo)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stdout + result.stderr)
        for duration in ("1", "5999", "!lambda return 300;"):
            with self.subTest(duration=duration), tempfile.TemporaryDirectory() as tempdir:
                repo = Path(tempdir) / "repo"
                shutil.copytree(self.fixture, repo, ignore=shutil.ignore_patterns(".esphome"))
                path = repo / "esphome/pixoo64.yaml"
                path.write_text(path.read_text().replace(
                    "        - pixoo64.timing_pause: pixoo_firmware\n",
                    "        - pixoo64.timer_start:\n            id: pixoo_firmware\n"
                    f"            duration_seconds: {duration}\n", 1))
                result = subprocess.run(
                    [sys.executable, "-m", "esphome", "compile", "--only-generate", "esphome/pixoo64.yaml"],
                    cwd=repo, text=True, capture_output=True, timeout=60, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                generated = (repo / "esphome/.esphome/build/pixoo64/src/main.cpp").read_text()
                expected = "300" if duration.startswith("!lambda") else duration
                self.assertRegex(generated, rf"->set_duration_seconds\(\[\]\(\) -> int32_t \{{[^}}]*return {expected};")

    def test_timing_rejects_per_kind_control_keys(self):
        for kind in ("stopwatch", "timer"):
            for verb in ("pause", "resume", "stop", "minimize", "show"):
                old = f"{kind}_{verb}"
                with self.subTest(key=old), tempfile.TemporaryDirectory() as tempdir:
                    repo = Path(tempdir) / "repo"
                    shutil.copytree(self.fixture, repo, ignore=shutil.ignore_patterns(".esphome"))
                    path = repo / "esphome/pixoo64.yaml"
                    anchor = "    timer_duration: timing_timer_duration\n"
                    path.write_text(path.read_text().replace(
                        anchor, anchor + f"    {old}:\n      name: Old Control\n", 1))
                    result = self.run_config(repo)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(f"[{old}] is an invalid option", result.stdout + result.stderr)

    def test_shadow_clock_codegen(self):
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "compile", "--only-generate", "esphome/pixoo64.yaml"],
            cwd=self.fixture, text=True, capture_output=True, timeout=60, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        generated = (self.fixture / "esphome/.esphome/build/pixoo64/src/main.cpp").read_text()
        for statement in (
            "new(clock_shadow_dashboard) pixoo64::dashboard::ShadowClockDashboard();",
            'clock_shadow_dashboard->set_id("clock_shadow");',
            "clock_shadow_dashboard->set_frame_interval_ms(40);",
            "clock_shadow_dashboard->set_time(panel_time);",
            "panel_content_controller->add_dashboard(clock_shadow_dashboard);",
        ):
            self.assertIn(statement, generated)

    def test_final_validators_reject_invalid_board_wiring(self):
        cases = (
            (
                "duplicate DDP dashboard",
                "esphome/pixoo64.yaml",
                "      dashboard_id: ddp\n",
                "      dashboard_id: ddp\n"
                "    - platform: ddp\n"
                "      id: ddp_dashboard_other\n"
                "      dashboard_id: ddp_other\n",
                "at most one ddp dashboard may be configured",
            ),
            (
                "DDP select catalog",
                "esphome/pixoo64.yaml",
                "      - ddp\n",
                "",
                "dashboard select options must exactly match the selected renderer dashboard IDs",
            ),
            (
                "DDP frame interval",
                "esphome/pixoo64.yaml",
                "      dashboard_id: ddp\n",
                "      dashboard_id: ddp\n      frame_interval: 0ms\n",
                "frame_interval must be at least 1 ms",
            ),
            (
                "DDP rejects unsupported port setting",
                "esphome/pixoo64.yaml",
                "      dashboard_id: ddp\n",
                "      dashboard_id: ddp\n      port: 4048\n",
                "[port] is an invalid option",
            ),
            (
                "solar brightness missing sun calculator",
                "esphome/pixoo64.yaml",
                "    sun: panel_sun\n\n# Dashboard rendering and content sources.",
                "\n# Dashboard rendering and content sources.",
                "'sun' is a required option",
            ),
            (
                "shared panel transport redirect policy",
                "esphome/pixoo64.yaml",
                "  follow_redirects: false\n",
                "  follow_redirects: true\n",
                "shared panel_http must use timeout 5s, follow_redirects false, and verify_ssl false",
            ),
            (
                "shared panel transport name",
                "esphome/pixoo64.yaml",
                "  id: panel_http\n",
                "  id: other_http\n",
                "Couldn't find ID 'panel_http'",
            ),
            (
                "now playing mismatched gate",
                "esphome/pixoo64.yaml",
                "    http_gate: panel_http_gate\n    clock: panel_time\n",
                "    http_gate: panel_other_gate\n    clock: panel_time\n",
                "Couldn't find ID 'panel_other_gate'",
            ),
            (
                "weather missing shared gate",
                "esphome/pixoo64.yaml",
                "    http_gate: panel_http_gate\n    latitude: location_latitude\n",
                "    latitude: location_latitude\n",
                "'http_gate' is a required option",
            ),
            (
                "now playing home assistant states",
                "esphome/pixoo64.yaml",
                "  homeassistant_states: true\n",
                "  homeassistant_states: false\n",
                "now_playing api_server must select an api with homeassistant_states: true",
            ),
            (
                "now playing wrong API reference",
                "esphome/pixoo64.yaml",
                "    api_server: panel_api\n",
                "    api_server: pixoo_firmware\n",
                "doesn't inherit from api::APIServer",
            ),
            (
                "now playing source",
                "esphome/pixoo64.yaml",
                """  now_playing:
    id: panel_now_playing
    api_server: panel_api
    http_request_id: panel_http
    http_gate: panel_http_gate
    clock: panel_time
""",
                "",
                "Couldn't find ID 'panel_now_playing'",
            ),
            (
                "now playing dashboard source type",
                "esphome/pixoo64.yaml",
                "      source: panel_now_playing\n      font: font_8\n",
                "      source: weather_source\n      font: font_8\n",
                "doesn't inherit from pixoo::now_playing::NowPlayingSource",
            ),
            (
                "now playing dashboard registration",
                "esphome/pixoo64.yaml",
                """    - platform: now_playing
      id: now_playing_dashboard
      dashboard_id: now_playing
      frame_interval: 33ms
      source: panel_now_playing
      font: font_8

""",
                "",
                "the configured source requires one dashboard_id: now_playing",
            ),
            (
                "duplicate now playing dashboard",
                "esphome/pixoo64.yaml",
                """    - platform: now_playing
      id: now_playing_dashboard
      dashboard_id: now_playing
      frame_interval: 33ms
      source: panel_now_playing
      font: font_8
""",
                """    - platform: now_playing
      id: now_playing_dashboard
      dashboard_id: now_playing
      frame_interval: 33ms
      source: panel_now_playing
      font: font_8
    - platform: now_playing
      id: now_playing_dashboard_other
      dashboard_id: now_playing_other
      frame_interval: 33ms
      source: panel_now_playing
      font: font_8
""",
                "at most one now_playing dashboard may be configured",
            ),
            (
                "now playing Pixel Operator font",
                "esphome/pixoo64.yaml",
                "      source: panel_now_playing\n      font: font_8\n",
                "      source: panel_now_playing\n      font: font_16\n",
                "now_playing font must be the 8px PixelOperator8 font with the complete 238-glyph inventory",
            ),
            (
                "now playing actions",
                "esphome/pixoo64.yaml",
                """    - action: now_playing_clear
      then:
        - pixoo64.now_playing_clear: panel_now_playing
""",
                "",
                "now_playing requires configure and clear API actions",
            ),
            (
                "panel power output",
                "esphome/hardware/pixoo64_rev1.yaml",
                "    channel: 8\n",
                "    channel: 7\n",
                "panel power_output must be an uninverted, full-range LEDC output on GPIO22, channel 8, at 1kHz",
            ),
            (
                "equalizer microphone wiring",
                "esphome/pixoo64.yaml",
                """  microphone:
    id: panel_microphone_adapter
    microphone_id: panel_mic
    enable_switch: mic_enable
""",
                "",
                "microphone wiring is required for an equalizer dashboard",
            ),
            (
                "dashboard select IDs",
                "esphome/pixoo64.yaml",
                "      - equalizer_bars\n",
                "",
                "dashboard select options must exactly match the selected renderer dashboard IDs",
            ),
            (
                "dashboard select default",
                "esphome/pixoo64.yaml",
                "    initial_option: clock_analog\n",
                "    initial_option: text\n",
                "dashboard select initial_option must match the renderer default_dashboard",
            ),
            (
                "removed timing dashboard schema",
                "esphome/pixoo64.yaml",
                "    - platform: ddp\n",
                "    - platform: timing\n",
                "Unknown value 'timing'",
            ),
            (
                "timer duration input required",
                "esphome/pixoo64.yaml",
                "    timer_duration: timing_timer_duration\n",
                "",
                "'timer_duration' is a required option",
            ),
            (
                "invalid clock face",
                "esphome/pixoo64.yaml",
                "      face: shadow\n",
                "      face: led\n",
                "Unknown value 'led'",
            ),
            (
                "shadow clock missing face",
                "esphome/pixoo64.yaml",
                "      face: shadow\n",
                "",
                "'face' is a required option",
            ),
            (
                "shadow clock select catalog",
                "esphome/pixoo64.yaml",
                "      - clock_shadow\n",
                "",
                "dashboard select options must exactly match the selected renderer dashboard IDs",
            ),
            (
                "shadow clock time source type",
                "esphome/pixoo64.yaml",
                "      face: shadow\n      time_id: panel_time\n",
                "      face: shadow\n      time_id: panel_content_controller\n",
                "doesn't inherit from time::RealTimeClock",
            ),
            (
                "microphone stream format",
                "esphome/hardware/pixoo64_rev1.yaml",
                '  pixoo64_rev1_microphone_sample_rate: "32000"\n',
                '  pixoo64_rev1_microphone_sample_rate: "16000"\n',
                "microphone source must be I2S audio at 32kHz, 32-bit, left channel, with APLL enabled",
            ),
            (
                "frame metrics window",
                "esphome/monitoring.yaml",
                "    window: 30s\n",
                "    window: 5000000s\n",
                "frame metrics window must not exceed 2147483647 milliseconds",
            ),
            (
                "frame metrics schedule",
                "esphome/pixoo64.yaml",
                """pixoo64:
  id: pixoo_firmware
  update_interval: 5ms
""",
                """pixoo64:
  id: pixoo_firmware
  update_interval: never
""",
                "frame metrics require a finite update_interval",
            ),
            (
                "dashboard frame interval",
                "esphome/pixoo64.yaml",
                """      dashboard_id: text
      frame_interval: 33ms
""",
                """      dashboard_id: text
      frame_interval: 0ms
""",
                "frame_interval must be at least 1 ms",
            ),
            (
                "GPIO34 active-low button",
                "esphome/buttons.yaml",
                """  - platform: gpio
    id: button_brightness
    internal: true
    pin:
      number: ${pixoo64_rev1_button_brightness_pin}
      mode:
        input: true
      inverted: ${pixoo64_rev1_button_inverted}
""",
                """  - platform: gpio
    id: button_brightness
    internal: true
    pin:
      number: ${pixoo64_rev1_button_brightness_pin}
      mode:
        input: true
      inverted: false
""",
                "GPIO34 panel button must be configured active-low",
            ),
            (
                "GPIO35 active-low button",
                "esphome/buttons.yaml",
                """  - platform: gpio
    id: button_power
    internal: true
    pin:
      number: ${pixoo64_rev1_button_power_pin}
      mode:
        input: true
      inverted: ${pixoo64_rev1_button_inverted}
""",
                """  - platform: gpio
    id: button_power
    internal: true
    pin:
      number: ${pixoo64_rev1_button_power_pin}
      mode:
        input: true
      inverted: false
""",
                "GPIO35 panel button must be configured active-low",
            ),
        )
        for name, relative_path, old, new, expected_error in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tempdir:
                repo = Path(tempdir) / "repo"
                shutil.copytree(self.fixture, repo)
                path = repo / relative_path
                text = path.read_text(encoding="utf-8")
                self.assertEqual(text.count(old), 1, f"ambiguous fixture mutation: {name}")
                path.write_text(text.replace(old, new), encoding="utf-8")

                result = self.run_config(repo)
                output = result.stdout + result.stderr
                self.assertNotEqual(result.returncode, 0, output)
                self.assertIn(expected_error, output)


if __name__ == "__main__":
    unittest.main()
