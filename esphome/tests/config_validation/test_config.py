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

        references = (
            ("esphome/pixoo64.yaml", "fonts/pixel_operator_glyphs.yaml"),
            ("esphome/tests/render_test/render_test.yaml", "../../fonts/pixel_operator_glyphs.yaml"),
            ("tools/render-weather-gif.py", "../../fonts/pixel_operator_glyphs.yaml"),
        )
        for relative_path, include_path in references:
            text = (self.fixture / relative_path).read_text(encoding="utf-8")
            self.assertEqual(
                text.count(f"glyphs: !include {include_path}"),
                2,
                relative_path,
            )

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

    def test_now_playing_dashboard_and_render_fixture_are_complete(self):
        production = (self.fixture / "esphome/pixoo64.yaml").read_text(
            encoding="utf-8"
        )
        self.assertEqual(production.count("    - platform: now_playing\n"), 1)
        self.assertEqual(production.count("      dashboard_id: now_playing\n"), 1)
        self.assertEqual(production.count("      source: panel_now_playing\n"), 1)
        self.assertEqual(production.count("      font: font_8\n"), 1)
        self.assertIn("      - now_playing\n", production)
        self.assertIn("  default_dashboard: clock_analog\n", production)
        for source in (
            "now_playing_data.cpp",
            "now_playing_text.cpp",
            "now_playing_art.cpp",
            "now_playing_timing.cpp",
            "metadata_policy.cpp",
        ):
            self.assertIn(source, production)

        render_path = "esphome/tests/render_test/render_test.yaml"
        render = (self.fixture / render_path).read_text(encoding="utf-8")
        self.assertEqual(render.count("    - platform: now_playing\n"), 1)
        self.assertIn("  - id: src_now_playing\n", render)
        self.assertIn("    now_playing_source: src_now_playing\n", render)
        render_display = (
            self.fixture
            / "esphome/tests/render_test/components/pixoo64_render_test/display.py"
        ).read_text(encoding="utf-8")
        self.assertIn("cv.Optional(CONF_NOW_PLAYING_SOURCE)", render_display)
        self.assertIn('title: "ÄÖÜ äöü ß"\n', render)
        self.assertIn('title: "Signal 🚀 Lost"\n', render)
        for source in (
            "now_playing_data.cpp",
            "now_playing_text.cpp",
            "now_playing_art.cpp",
            "now_playing_timing.cpp",
            "metadata_policy.cpp",
        ):
            self.assertIn(source, render)
        result = self.run_config(self.fixture, render_path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_render_fixture_rejects_unpaired_artwork_content_identity(self):
        with tempfile.TemporaryDirectory() as tempdir:
            repo = Path(tempdir) / "repo"
            shutil.copytree(self.fixture, repo)
            render_path = repo / "esphome/tests/render_test/render_test.yaml"
            text = render_path.read_text(encoding="utf-8")
            old = (
                "        artwork_content_identity: 5002\n"
                "        artwork_content_revision: 2\n"
            )
            self.assertIn(old, text)
            render_path.write_text(
                text.replace(
                    old, "        artwork_content_identity: 5002\n", 1
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

    def test_now_playing_uses_project_decoder_libraries(self):
        text = (
            self.fixture / "esphome/components/pixoo64/__init__.py"
        ).read_text(encoding="utf-8")
        self.assertIn('"JPEGDEC",\n            "1.8.4"', text)
        self.assertIn('cg.add_library("pngle", "1.1.0")', text)
        self.assertIn('cg.add_build_flag("-DUSE_PIXOO64_NOW_PLAYING")', text)
        self.assertIn('cg.add_build_flag("-DPNGLE_NO_GAMMA_CORRECTION")', text)
        self.assertIn('cg.add_build_flag("-Wl,--wrap=calloc")', text)
        self.assertNotIn("from esphome.components import runtime_image", text)
        self.assertNotIn('"runtime_image",', text)

        decoder = (
            self.fixture / "esphome/components/pixoo64/artwork_decoder.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn(
            '#include "artwork_decoder.h"\n\n#ifdef USE_PIXOO64_NOW_PLAYING',
            decoder,
        )
        for forbidden in ("App.feed_wdt", "ESP_LOG", "runtime_image"):
            self.assertNotIn(forbidden, decoder)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", decoder)
        self.assertIn("kDecoderYieldIntervalPixels = 1024", decoder)
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(1))", decoder)

        source = (
            self.fixture
            / "esphome/components/pixoo64/home_assistant_media_source.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn(
            '#include "home_assistant_media_source.h"\n\n#ifdef USE_PIXOO64_NOW_PLAYING',
            source,
        )
        self.assertNotIn("MALLOC_CAP_INTERNAL", source)
        self.assertIn(
            ': fetch_worker(std::move(fetch_job), 12288, 0, 0, false,\n'
            '                     "pixoo_art_http")',
            source,
        )
        self.assertIn(
            'decode_worker(std::move(decode_job), 12288, 0, 1, false,\n'
            '                      "pixoo_art_dec")',
            source,
        )
        self.assertIn(
            "kArtworkIdentityDomain, resolved.data(), resolved.size()", source
        )
        self.assertIn(
            '"Artwork HTTP task: task=pixoo_art_http core=0 priority=0 "',
            source,
        )
        self.assertIn(
            '"Artwork decode task: task=pixoo_art_dec core=1 priority=0 "',
            source,
        )
        self.assertIn("kArtworkReadChunkBytes = 512", source)
        self.assertIn("kArtworkReadChunkBytes,", source)
        reader = (
            self.fixture / "esphome/components/pixoo64/http_body_reader.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("std::min(remaining, options.chunk_bytes)", reader)
        self.assertIn("container->end();\n  lease.Release();", reader)
        self.assertNotIn("http_read_fully", reader)
        self.assertIn("published_encoded", source)
        self.assertIn("artwork::EncodedBodiesEqual", source)
        self.assertIn('ESP_LOGD(TAG, "artwork reused")', source)
        self.assertNotIn("vTaskDelay", source)
        self.assertNotIn("YieldArtworkWorker", source)
        for diagnostic in (
            "artwork eligibility",
            "artwork reference",
            "artwork job %u queued",
            "artwork job %u HTTP start",
            "artwork job %u HTTP finish",
            "artwork job %u HTTP handoff",
            "artwork job %u decode start",
            "artwork job %u decode finish",
            "artwork job %u main completion",
            "artwork renderer copy",
            "ArtworkJobDiagnostics",
            "ArtworkFetchResult",
            "CurrentTaskName",
            "CurrentTaskCore",
            "CurrentTaskPriority",
            "CurrentStackHighWater",
            "next_job_sequence",
            "main_job_active",
            "copy_started_us",
        ):
            self.assertNotIn(diagnostic, source)
        log_calls = re.findall(r"ESP_LOG(?:CONFIG|[A-Z]+)\(.*?\);", source, re.S)
        self.assertGreater(len(log_calls), 0)
        for log_call in log_calls:
            for secret_value in (
                "request.url",
                "desired_artwork_url_",
                "record_.entity_id",
                "home_assistant_url",
                "artwork_identity",
            ):
                self.assertNotIn(secret_value, log_call)

        weather = (
            self.fixture / "esphome/components/pixoo64/open_meteo.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("weather worker start", weather)
        self.assertNotIn("weather worker finish", weather)

    def test_production_config_uses_one_shared_panel_http_gate(self):
        text = (self.fixture / "esphome/pixoo64.yaml").read_text(encoding="utf-8")
        self.assertEqual(text.count("http_request:\n"), 1)
        self.assertEqual(text.count("  id: panel_http\n"), 1)
        self.assertEqual(text.count("http_gate:\n"), 1)
        self.assertEqual(text.count("id: panel_http_gate\n"), 1)
        self.assertEqual(text.count("http_request_id: panel_http\n"), 2)
        self.assertEqual(text.count("http_gate: panel_http_gate\n"), 2)
        self.assertIn("  timeout: 5s\n", text)
        self.assertIn("  follow_redirects: false\n", text)
        self.assertIn("  verify_ssl: false\n", text)
        self.assertIn("    http_request.idf: NONE\n", text)

    def test_production_config_has_both_timing_faces(self):
        text = (self.fixture / "esphome/pixoo64.yaml").read_text(encoding="utf-8")
        self.assertEqual(text.count("    - platform: timing\n"), 2)
        for face in ("stopwatch", "timer"):
            self.assertEqual(text.count(f"      dashboard_id: {face}\n"), 1)
            self.assertEqual(text.count(f"      face: {face}\n"), 1)

    def test_production_config_has_all_clock_faces(self):
        text = (self.fixture / "esphome/pixoo64.yaml").read_text(encoding="utf-8")
        self.assertEqual(text.count("    - platform: clock\n"), 4)
        for face in ("split_flap", "analog", "binary", "digital"):
            self.assertEqual(text.count(f"      dashboard_id: clock_{face}\n"), 1)
            self.assertEqual(text.count(f"      face: {face}\n"), 1)

    def test_production_solar_brightness_uses_runtime_location_and_clock(self):
        production = (self.fixture / "esphome/pixoo64.yaml").read_text(encoding="utf-8")
        params = (self.fixture / "esphome/config-params.yaml").read_text(encoding="utf-8")
        self.assertEqual(production.count("\nsun:\n"), 1)
        self.assertIn("  id: panel_sun\n", production)
        self.assertIn("  latitude: 0\n", production)
        self.assertIn("  longitude: 0\n", production)
        self.assertIn("  solar_brightness:\n", production)
        for value in (
            "enable_switch: solar_brightness",
            "day_brightness: solar_day_brightness",
            "night_brightness: solar_night_brightness",
            "latitude: location_latitude",
            "longitude: location_longitude",
            "sun: panel_sun",
        ):
            self.assertIn(value, production)
        self.assertEqual(params.count("name: Day Brightness"), 1)
        self.assertEqual(params.count("name: Night Brightness"), 1)
        self.assertEqual(production.count("name: Solar Brightness"), 1)
        self.assertIn("restore_mode: RESTORE_DEFAULT_OFF", production)
        component = (self.fixture / "esphome/components/pixoo64/firmware_app_component.cpp").read_text(encoding="utf-8")
        self.assertIn("solar_sun_->set_latitude(latitude)", component)
        self.assertIn("solar_sun_->set_longitude(longitude)", component)
        self.assertIn("add_remote_values_listener(this)", component)
        self.assertIn("light_publication_guard_.Expect(state)", component)
        self.assertNotIn("SyncLightFromEntity();\n", production)
        self.assertNotIn('#include "esphome/components/sun/sun.cpp"', component)

    def test_final_validators_reject_invalid_board_wiring(self):
        cases = (
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
                "stopwatch timing dashboard ID",
                "esphome/pixoo64.yaml",
                "      dashboard_id: stopwatch\n      frame_interval: 33ms\n      face: stopwatch\n",
                "      dashboard_id: stopwatch_other\n      frame_interval: 33ms\n      face: stopwatch\n",
                "the stopwatch timing dashboard_id must be stopwatch",
            ),
            (
                "timer timing dashboard ID",
                "esphome/pixoo64.yaml",
                "      dashboard_id: timer\n      frame_interval: 33ms\n      face: timer\n",
                "      dashboard_id: timer_other\n      frame_interval: 33ms\n      face: timer\n",
                "the timer timing dashboard_id must be timer",
            ),
            (
                "duplicate stopwatch timing face",
                "esphome/pixoo64.yaml",
                """    - platform: timing
      id: stopwatch_dashboard
      dashboard_id: stopwatch
      frame_interval: 33ms
      face: stopwatch
""",
                """    - platform: timing
      id: stopwatch_dashboard
      dashboard_id: stopwatch
      frame_interval: 33ms
      face: stopwatch

    - platform: timing
      id: stopwatch_dashboard_other
      dashboard_id: stopwatch_other
      frame_interval: 33ms
      face: stopwatch
""",
                "at most one stopwatch timing dashboard may be configured",
            ),
            (
                "duplicate timer timing face",
                "esphome/pixoo64.yaml",
                """    - platform: timing
      id: timer_dashboard
      dashboard_id: timer
      frame_interval: 33ms
      face: timer
""",
                """    - platform: timing
      id: timer_dashboard
      dashboard_id: timer
      frame_interval: 33ms
      face: timer

    - platform: timing
      id: timer_dashboard_other
      dashboard_id: timer_other
      frame_interval: 33ms
      face: timer
""",
                "at most one timer timing dashboard may be configured",
            ),
            (
                "missing timing face",
                "esphome/pixoo64.yaml",
                "      face: timer\n",
                "",
                "'face' is a required option",
            ),
            (
                "invalid timing face",
                "esphome/pixoo64.yaml",
                "      face: timer\n",
                "      face: countdown\n",
                "Unknown value 'countdown'",
            ),
            (
                "invalid clock face",
                "esphome/pixoo64.yaml",
                "      face: digital\n",
                "      face: led\n",
                "Unknown value 'led'",
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
                "    window: 300s\n",
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
                "render metrics schedule",
                "esphome/monitoring.yaml",
                """pixoo64_content:
  update_interval: 300s
  render_metrics:
""",
                """pixoo64_content:
  update_interval: never
  render_metrics:
""",
                "render metrics require a finite update_interval",
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
