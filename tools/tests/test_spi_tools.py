#!/usr/bin/env python3
"""Regressions for the LogicAnalyzer SPI decoder and the frame renderer."""
from __future__ import annotations

import importlib.util
import io
import json
import struct
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stderr
from unittest import mock
from pathlib import Path
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[2]


def load_tool(module_name: str, filename: str):
    spec = importlib.util.spec_from_file_location(module_name, ROOT / "tools" / filename)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


SPI = load_tool("decode_panel_spi_test", "decode-panel-spi.py")
RENDER = load_tool("render_panel_frame_test", "render-panel-frame.py")
SRREAD = load_tool("read_sr_capture_test", "read-sr-capture.py")
NOTIFY = load_tool("notify_pixoo_test", "notify-pixoo.py")
UART = load_tool("uart_capture_test", "uart-capture.py")
WEATHER = load_tool("render_weather_gif_test", "render-weather-gif.py")


def spi_rows_for_bytes(byte_values, *, idle_high_cs=True):
    """Build (cs, sclk, mosi) sample rows that clock the given bytes MSB-first.

    SPI mode 0: clock idles low; each bit is presented then a rising edge
    samples it. CS is active-low, wrapped around the whole byte stream.
    """
    rows = []
    # idle before CS: cs=1, sclk=0, mosi=0
    rows.append((1, 0, 0))
    # assert CS
    for value in byte_values:
        for bit_index in range(8):
            bit = (value >> (7 - bit_index)) & 1
            # present bit with clock low, then rising edge samples it
            rows.append((0, 0, bit))
            rows.append((0, 1, bit))
    # deassert CS
    rows.append((1, 0, 0))
    return rows


class SpiDecodeTest(unittest.TestCase):
    def make_capture(self, byte_values):
        rows = spi_rows_for_bytes(byte_values)
        # channel order: cs=0, sclk=1, mosi=2
        return SPI.Capture(samplerate_hz=100_000_000,
                           channel_names=["CS", "SCLK", "MOSI"], rows=rows)

    def test_decodes_bytes_msb_first_rising_edge(self):
        cap = self.make_capture([0xAA, 0x55, 0x00, 0xFF])
        txns = SPI.decode_spi(cap, sclk=1, mosi=2, cs=0, cs_active=0,
                              clock_edge="rising", bit_order="msb")
        self.assertEqual(len(txns), 1)
        self.assertEqual(txns[0].bytes_, [0xAA, 0x55, 0x00, 0xFF])

    def test_parses_pixoo_frame(self):
        # AA 04 00 10 42 23 05 06 BB  (the captured boot frame)
        frame = [0xAA, 0x04, 0x00, 0x10, 0x42, 0x23, 0x05, 0x06, 0xBB]
        cap = self.make_capture(frame)
        txns = SPI.decode_spi(cap, 1, 2, 0, 0, "rising", "msb")
        frames = SPI.parse_frames(txns[0].bytes_)
        self.assertEqual(len(frames), 1)
        f = frames[0]
        self.assertTrue(f["valid"])
        self.assertEqual(f["cmd"], 0x10)
        self.assertEqual(f["len"], 4)
        self.assertEqual(f["payload"], [0x42, 0x23, 0x05, 0x06])
        self.assertEqual(f["tail"], 0xBB)

    def test_full_frame_command(self):
        payload = list(range(256)) * 48  # 12288 bytes
        self.assertEqual(len(payload), 12288)
        frame = [0xAA, 0x00, 0x30] + [0x00] + payload + [0xBB]
        frames = SPI.parse_frames(frame)
        self.assertEqual(len(frames), 1)
        self.assertTrue(frames[0]["valid"])
        self.assertEqual(frames[0]["cmd"], 0x00)
        self.assertEqual(frames[0]["len"], 0x3000)
        self.assertEqual(len(frames[0]["payload"]), 12288)

    def test_invalid_tail_flagged(self):
        frame = [0xAA, 0x01, 0x00, 0x10, 0x00, 0xCC]  # wrong tail
        frames = SPI.parse_frames(frame)
        self.assertFalse(frames[0]["valid"])

    def test_lsb_first(self):
        cap = self.make_capture([0x01])  # MSB-first bits: 00000001
        txns = SPI.decode_spi(cap, 1, 2, 0, 0, "rising", "lsb")
        # reading those same bits LSB-first gives 0x80
        self.assertEqual(txns[0].bytes_, [0x80])

    def test_csv_roundtrip(self):
        rows = spi_rows_for_bytes([0xAA, 0xBB])
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "cap.csv"
            with path.open("w", encoding="utf-8") as h:
                h.write("CS,SCLK,MOSI\n")
                for cs, sclk, mosi in rows:
                    h.write(f"{cs},{sclk},{mosi}\n")
            cap = SPI.load_capture(path)
        self.assertIsNone(cap.samplerate_hz)  # CSV has no timebase
        self.assertEqual(cap.channel_names, ["CS", "SCLK", "MOSI"])
        txns = SPI.decode_spi(cap, cap.index_of("SCLK"), cap.index_of("MOSI"),
                              cap.index_of("CS"), 0, "rising", "msb")
        self.assertEqual(txns[0].bytes_, [0xAA, 0xBB])

    def test_lac_roundtrip_reads_samplerate(self):
        rows = spi_rows_for_bytes([0x10])
        columns = list(zip(*rows))  # cs, sclk, mosi columns
        lac = {
            "Settings": {
                "Frequency": 100_000_000,
                "CaptureChannels": [
                    {"ChannelNumber": 0, "ChannelName": "CS", "Samples": list(columns[0])},
                    {"ChannelNumber": 1, "ChannelName": "SCLK", "Samples": list(columns[1])},
                    {"ChannelNumber": 2, "ChannelName": "MOSI", "Samples": list(columns[2])},
                ],
            }
        }
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "cap.lac"
            path.write_text(json.dumps(lac), encoding="utf-8")
            cap = SPI.load_capture(path)
        self.assertEqual(cap.samplerate_hz, 100_000_000)
        txns = SPI.decode_spi(cap, cap.index_of("SCLK"), cap.index_of("MOSI"),
                              cap.index_of("CS"), 0, "rising", "msb")
        self.assertEqual(txns[0].bytes_, [0x10])

    def test_lac_nonfinite_samplerate_is_not_used(self):
        lac = {
            "Settings": {
                "Frequency": float("inf"),
                "CaptureChannels": [{"ChannelNumber": 0, "Samples": [0]}],
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.lac"
            path.write_text(json.dumps(lac), encoding="utf-8")
            capture = SPI.load_lac(path)
        self.assertIsNone(capture.samplerate_hz)

    def test_index_of_numeric_and_name(self):
        cap = SPI.Capture(None, ["CS", "SCLK", "MOSI"], [(0, 0, 0)])
        self.assertEqual(cap.index_of("2"), 2)
        self.assertEqual(cap.index_of("mosi"), 2)
        with self.assertRaises(ValueError):
            cap.index_of("nope")

    def test_cli_rejects_invalid_samplerate_and_payload_preview(self):
        parser = SPI.build_parser()
        for samplerate in ("0", "-1", "nan", "inf"):
            with self.subTest(samplerate=samplerate), self.assertRaises(SystemExit):
                parser.parse_args(["capture.csv", "--samplerate", samplerate])
        with self.assertRaises(SystemExit):
            parser.parse_args(["capture.csv", "--payload-preview", "-1"])
        args = parser.parse_args(["capture.csv", "--samplerate", "100000000", "--payload-preview", "0"])
        self.assertEqual(args.samplerate, 100000000)
        self.assertEqual(args.payload_preview, 0)


def read_png(path: Path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    width = height = None
    idat = b""
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
            assert bit_depth == 8 and color_type == 2  # 8-bit RGB
        elif tag == b"IDAT":
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    # strip the per-scanline filter byte (all filter 0)
    stride = width * 3 + 1
    rows = []
    for y in range(height):
        line = raw[y * stride:(y + 1) * stride]
        assert line[0] == 0
        rows.append(line[1:])
    return width, height, rows


class RenderTest(unittest.TestCase):
    def test_load_payload_rejects_wrong_size(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "bad.bin"
            path.write_bytes(b"\x00" * 10)
            with self.assertRaises(ValueError):
                RENDER.load_payload(path)

    def test_first_pixel_top_left_row_major(self):
        payload = bytearray(RENDER.PAYLOAD_BYTES)
        payload[0:3] = b"\x0a\x14\x1e"  # pixel(0,0) = (10,20,30)
        grid = RENDER.to_grid(bytes(payload), "row", "top-left", "rgb")
        self.assertEqual(grid[0][0], (10, 20, 30))

    def test_origin_bottom_right(self):
        payload = bytearray(RENDER.PAYLOAD_BYTES)
        payload[0:3] = b"\x01\x02\x03"  # first pixel
        grid = RENDER.to_grid(bytes(payload), "row", "bottom-right", "rgb")
        self.assertEqual(grid[63][63], (1, 2, 3))

    def test_channel_order_bgr(self):
        payload = bytearray(RENDER.PAYLOAD_BYTES)
        payload[0:3] = b"\x01\x02\x03"  # bytes are B,G,R
        grid = RENDER.to_grid(bytes(payload), "row", "top-left", "bgr")
        self.assertEqual(grid[0][0], (3, 2, 1))  # -> R,G,B

    def test_writes_valid_png(self):
        payload = bytes(RENDER.PAYLOAD_BYTES)
        grid = RENDER.to_grid(payload, "row", "top-left", "rgb")
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "frame.png"
            RENDER.write_png(out, grid, scale=2)
            w, h, rows = read_png(out)
        self.assertEqual((w, h), (128, 128))  # 64*2
        self.assertEqual(len(rows), 128)


class SrReaderTest(unittest.TestCase):
    def test_reads_synthetic_capture(self):
        metadata = """[device 1]
samplerate = 24 MHz
unitsize = 1
total probes = 2
probe1 = CS
probe2 = SCLK
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.sr"
            with ZipFile(path, "w") as archive:
                archive.writestr("metadata", metadata)
                archive.writestr("logic-1-1", bytes([0b00, 0b01, 0b11, 0b10]))
            capture = SRREAD.open_capture(path)
            try:
                report = SRREAD.analyze(capture, None, 0, 0)
            finally:
                capture.close()
        self.assertEqual(report["format"], "sr-capture-summary-v1")
        self.assertEqual(report["samplerate_hz"], 24_000_000)
        self.assertEqual(report["total_samples"], 4)
        self.assertEqual(report["channels"][0]["rising_edges"], 1)
        self.assertEqual(report["channels"][1]["rising_edges"], 1)

    def test_parse_rate(self):
        self.assertEqual(SRREAD.parse_rate("24 MHz"), 24_000_000)
        self.assertEqual(SRREAD.parse_rate("12000000"), 12_000_000)
        with self.assertRaises(ValueError):
            SRREAD.parse_rate("bogus")


class PublicCliTest(unittest.TestCase):
    def test_notify_port_must_be_a_valid_tcp_port(self):
        parser = NOTIFY.build_parser()
        for port in ("0", "-1", "65536", "not-a-number"):
            with self.subTest(port=port), self.assertRaises(SystemExit):
                parser.parse_args(["--port", port])
        self.assertEqual(parser.parse_args(["--port", "1"]).port, 1)
        self.assertEqual(parser.parse_args(["--port", "65535"]).port, 65535)
        self.assertEqual(parser.parse_args([]).port, 6053)

    def test_notify_requires_explicit_connection_details(self):
        with self.assertRaises(SystemExit):
            NOTIFY.parse_args(["hello"])
        with mock.patch.dict("os.environ", {"PIXOO_API_ENCRYPTION_KEY": "test-key"}, clear=True):
            args = NOTIFY.parse_args(["--host", "display.local", "--reaction", "laughing"])
        self.assertEqual(NOTIFY.service_request(args), ("reaction", {"reaction": "laughing"}))
        with mock.patch.dict("os.environ", {"PIXOO_API_ENCRYPTION_KEY": "test-key"}, clear=True):
            args = NOTIFY.parse_args(["--host", "display.local", "hello"])
        self.assertEqual(NOTIFY.service_request(args), ("notify", {
            "message": "hello", "title": "", "severity": "info",
            "duration": 4, "sound": "",
        }))
        with mock.patch.dict("os.environ", {"PIXOO_API_ENCRYPTION_KEY": "test-key"}, clear=True):
            args = NOTIFY.parse_args([
                "--host", "display.local", "--title", "System", "hello",
            ])
        self.assertEqual(NOTIFY.service_request(args), ("notify", {
            "message": "hello", "title": "System", "severity": "info",
            "duration": 4, "sound": "",
        }))
        for mode in (["--reaction", "laughing"], ["--clear"]):
            with self.subTest(mode=mode), mock.patch.dict(
                "os.environ", {"PIXOO_API_ENCRYPTION_KEY": "test-key"}, clear=True
            ), self.assertRaises(SystemExit):
                NOTIFY.parse_args([
                    "--host", "display.local", "--title", "System", *mode,
                ])

    def test_notify_reads_only_private_key_files_and_rejects_cli_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            key_file = Path(directory) / "pixoo.key"
            key_file.write_text("test-key\n", encoding="utf-8")
            key_file.chmod(0o600)
            args = NOTIFY.parse_args([
                "--host", "display.local", "--api-encryption-key-file", str(key_file), "hello",
            ])
            self.assertEqual(args.api_encryption_key, "test-key")
            key_file.chmod(0o644)
            with self.assertRaises(SystemExit):
                NOTIFY.parse_args(["--host", "display.local", "--api-encryption-key-file", str(key_file), "hello"])
        stderr = io.StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit):
            NOTIFY.parse_args(["--host", "display.local", "--api-encryption-key", "test-key", "hello"])
        self.assertNotIn("test-key", stderr.getvalue())

    def test_weather_cli_and_executable_resolution(self):
        with self.assertRaises(SystemExit):
            WEATHER.parse_args(["--fps", "0"])
        for seconds in ("0", "nan", "inf"):
            with self.subTest(seconds=seconds), self.assertRaises(SystemExit):
                WEATHER.parse_args(["--seconds", seconds])
        self.assertEqual(WEATHER.parse_args(["--fps", "20"]).fps, 20)
        with mock.patch.object(WEATHER.shutil, "which", return_value="/tmp/esphome") as which:
            self.assertEqual(WEATHER.resolve_esphome({"ESPHOME": "custom-esphome", "PATH": "/bin"}), "/tmp/esphome")
        which.assert_called_once_with("custom-esphome", path="/bin")
        self.assertEqual(
            WEATHER.build_binary(Path("/tmp/build-root")),
            Path("/tmp/build-root") / WEATHER.BUILD_NAME / ".pioenvs" / WEATHER.BUILD_NAME / "program",
        )
        generated = WEATHER.build_yaml(
            [WEATHER.Shot("sunny", "sunny", WEATHER.NOON_EPOCH, 20, [0])],
            Path("/tmp/weather-frames"),
        )
        self.assertIn("    panel_text: gif_text\n", generated)
        self.assertIn("    animation_only: true\n", generated)
        self.assertNotIn("    now_playing_source:", generated)

    def test_render_test_view_requires_explicit_update(self):
        script = (ROOT / "tools" / "render-test-view.sh").read_text(encoding="utf-8")
        self.assertIn("--update", script)
        self.assertIn("(unset PIXOO_UPDATE_SNAPSHOTS; \"$bin\")", script)
        self.assertIn("PIXOO_UPDATE_SNAPSHOTS=1 \"$bin\"", script)

    def test_uart_requires_paths_and_validates_baud(self):
        parser = UART.build_parser()
        with self.assertRaises(SystemExit):
            parser.parse_args([])
        with self.assertRaises(SystemExit):
            parser.parse_args(["--port", "serial0", "--output", "local/log.bin", "--baud", "0"])
        args = parser.parse_args(["--port", "serial0", "--output", "local/log.bin", "--baud", "230400"])
        self.assertEqual(args.baud, 230400)


if __name__ == "__main__":
    unittest.main()
