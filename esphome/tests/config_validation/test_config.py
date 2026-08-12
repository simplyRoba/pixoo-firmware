#!/usr/bin/env python3
"""ESPHome configuration regressions for the Pixoo64 board profile."""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


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

    def run_config(self, repo: Path):
        return subprocess.run(
            [
                sys.executable,
                "-m",
                "esphome",
                "config",
                "esphome/pixoo64.yaml",
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

    def test_final_validators_reject_invalid_board_wiring(self):
        cases = (
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
                "stopwatch dashboard ID",
                "esphome/pixoo64.yaml",
                "      dashboard_id: stopwatch\n",
                "      dashboard_id: stopwatch_other\n",
                "the stopwatch dashboard_id must be stopwatch",
            ),
            (
                "duplicate stopwatch dashboard",
                "esphome/pixoo64.yaml",
                """    - platform: stopwatch
      id: stopwatch_dashboard
      dashboard_id: stopwatch
      frame_interval: 33ms
""",
                """    - platform: stopwatch
      id: stopwatch_dashboard
      dashboard_id: stopwatch
      frame_interval: 33ms

    - platform: stopwatch
      id: stopwatch_dashboard_other
      dashboard_id: stopwatch_other
      frame_interval: 33ms
""",
                "at most one stopwatch dashboard may be configured",
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
