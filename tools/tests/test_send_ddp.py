#!/usr/bin/env python3
"""Host-only packet, pacing, CLI, and loopback regressions for the DDP sender."""
from __future__ import annotations

import importlib.util
import io
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest import mock

TOOL = Path(__file__).resolve().parents[1] / "send-ddp.py"
spec = importlib.util.spec_from_file_location("send_ddp_test", TOOL)
assert spec is not None and spec.loader is not None
DDP = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = DDP
spec.loader.exec_module(DDP)


def args(*extra):
    return DDP.parse_args(["--host", "127.0.0.1", *extra])


class PacketTest(unittest.TestCase):
    def test_exact_headers_and_reconstruction(self):
        frame = bytes(range(256)) * 48
        for index in (0, 1, 14, 15, 16, 0xFFFFFFFF, 0x100000000):
            for legacy in (False, True):
                with self.subTest(index=index, legacy=legacy):
                    packets = list(DDP.packetize(frame, index, legacy))
                    self.assertEqual(len(packets), 9)
                    for chunk, packet in enumerate(packets):
                        length = 1440 if chunk < 8 else 768
                        header = struct.pack("!BBBBIH", 0x41 if chunk == 8 else 0x40,
                                             index % 15 + 1, 1 if legacy else 11,
                                             1, chunk * 1440, length)
                        self.assertEqual(packet[:10], header)
                        self.assertEqual(len(packet), length + 10)
                    self.assertEqual(b"".join(p[10:] for p in packets), frame)
        with self.assertRaises(ValueError):
            list(DDP.packetize(b"short", 0))

    def test_render_brightness_marker_and_orientation(self):
        for index in (0, 1, 2, 0x80000000, 0xFFFFFFFF, 0x100000000):
            frame = DDP.render_frame(index)
            self.assertEqual(len(frame), 12288)
            self.assertLessEqual(max(frame), 64)
            self.assertEqual(frame, DDP.render_frame(index))
            for bit in range(32):
                level = 64 if index & (1 << (31 - bit)) else 4
                for y in range(4):
                    for x in range(bit * 2, bit * 2 + 2):
                        pixel = frame[(y * 64 + x) * 3:(y * 64 + x) * 3 + 3]
                        self.assertEqual(max(pixel), level)
            corners = [frame[p:p + 3] for p in (0, 63 * 3, 63 * 64 * 3, 4095 * 3)]
            self.assertEqual(len(set(corners)), 4)
        self.assertNotEqual(DDP.render_frame(0)[:768], DDP.render_frame(1)[:768])
        self.assertNotEqual(DDP.render_frame(0)[768:], DDP.render_frame(1)[768:])


class ArgumentTest(unittest.TestCase):
    def test_defaults_environment_and_overrides(self):
        with mock.patch.dict(os.environ, {"PIXOO_HOST": "example.local"}):
            parsed = DDP.parse_args([])
            self.assertEqual(parsed.host, "example.local")
            self.assertEqual(args().host, "127.0.0.1")
        self.assertEqual((parsed.port, parsed.fps, parsed.seconds), (4048, 30, 30))
        self.assertIsNone(parsed.pause_at)
        self.assertIsNone(parsed.overload_fps)
        self.assertFalse(parsed.legacy_type)
        self.assertTrue(args("--legacy-type").legacy_type)
        args("--port", "65535", "--fps", "1000", "--seconds", "0.01")
        args("--pause-at", "0", "--overload-at", "0", "--overload-fps", "1")

    def test_invalid_values_and_event_timing(self):
        with redirect_stderr(io.StringIO()):
            with mock.patch.dict(os.environ, {}, clear=True), self.assertRaises(SystemExit):
                DDP.parse_args([])
            for flag in ("--fps", "--seconds", "--pause-at", "--pause-seconds",
                         "--overload-fps", "--overload-at", "--overload-seconds"):
                for value in ("nan", "inf", "-inf", "bad", "-1"):
                    with self.subTest(flag=flag, value=value), self.assertRaises(SystemExit):
                        args(f"{flag}={value}")
            for flag in ("--fps", "--seconds", "--pause-seconds", "--overload-fps",
                         "--overload-seconds"):
                with self.subTest(flag=flag), self.assertRaises(SystemExit):
                    args(flag, "0")
            for value in ("0", "65536", "-1", "1.5", "nan", "inf"):
                with self.subTest(port=value), self.assertRaises(SystemExit):
                    args("--port", value)
            for flag in ("--fps", "--overload-fps"):
                with self.assertRaises(SystemExit):
                    args(flag, "1001")
            for options in (("--pause-at", "30"),
                            ("--overload-fps", "40", "--overload-at", "30")):
                with self.assertRaises(SystemExit):
                    args(*options)


class FakeClock:
    def __init__(self):
        self.now = 0.0
        self.packets = []
        self.reports = []
        self.delay = 0.0

    def clock(self):
        return self.now

    def sleep(self, seconds):
        assert seconds > 0
        self.now += seconds

    def send(self, packet):
        self.packets.append((self.now, packet))
        if packet[0] == 0x41:
            self.now += self.delay

    def report(self, stats, elapsed, final=False):
        self.reports.append((stats.frames, stats.datagrams, elapsed, final))

    def run(self, *options):
        return DDP.run(args(*options), self.send, clock=self.clock,
                       sleep=self.sleep, report=self.report)

    def frames(self):
        return [(t, p[1]) for t, p in self.packets if p[0] == 0x41]


class ScheduleTest(unittest.TestCase):
    def test_finite_normal_pacing_and_five_second_reports(self):
        clock = FakeClock()
        stats = clock.run("--fps", "2", "--seconds", "11")
        self.assertEqual(stats.frames, 22)
        self.assertEqual(stats.datagrams, 198)
        self.assertEqual(stats.skipped_slots, 0)
        self.assertEqual([t for t, _ in clock.frames()], [i / 2 for i in range(22)])
        self.assertEqual([r[2] for r in clock.reports], [5, 10, 11])
        self.assertEqual([r[3] for r in clock.reports], [False, False, True])

    def test_frame_generation_fits_requested_period_without_drift(self):
        clock = FakeClock()
        def render(index):
            clock.now += 0.02
            return bytes(DDP.FRAME_BYTES)
        with mock.patch.object(DDP, "render_frame", side_effect=render):
            stats = clock.run("--fps", "10", "--seconds", "1")
        self.assertEqual(stats.frames, 10)
        self.assertEqual(stats.datagrams, 90)
        self.assertEqual(stats.skipped_slots, 0)
        for actual, expected in zip([t for t, _ in clock.frames()],
                                    [0.02 + i * 0.1 for i in range(10)]):
            self.assertAlmostEqual(actual, expected)
        self.assertTrue(all(t < 1 for t, _ in clock.packets))

    def test_slow_generation_skips_slots_without_bursts(self):
        clock = FakeClock()
        def render(index):
            clock.now += 0.26
            return bytes(DDP.FRAME_BYTES)
        with mock.patch.object(DDP, "render_frame", side_effect=render):
            stats = clock.run("--fps", "10", "--seconds", "1")
        self.assertEqual(stats.frames, 3)
        self.assertGreater(stats.skipped_slots, 0)
        for actual, expected in zip([t for t, _ in clock.frames()], [0.26, 0.52, 0.78]):
            self.assertAlmostEqual(actual, expected)
        self.assertTrue(all(t < 1 for t, _ in clock.packets))

    def test_pause_boundary_discards_slow_generation(self):
        clock = FakeClock()
        def render(index):
            clock.now += 0.16
            return bytes(DDP.FRAME_BYTES)
        with mock.patch.object(DDP, "render_frame", side_effect=render):
            stats = clock.run("--fps", "10", "--seconds", "0.7",
                              "--pause-at", "0.1", "--pause-seconds", "0.2")
        self.assertEqual(stats.frames, 2)
        self.assertFalse(any(0.1 <= t < 0.3 for t, _ in clock.packets))
        for actual, expected in zip([t for t, _ in clock.frames()], [0.46, 0.62]):
            self.assertAlmostEqual(actual, expected)

    def test_late_sends_skip_slots_without_bursts(self):
        clock = FakeClock()
        clock.delay = 0.26
        stats = clock.run("--fps", "10", "--seconds", "1")
        times = [t for t, _ in clock.frames()]
        self.assertEqual(stats.frames, 4)
        self.assertGreater(stats.skipped_slots, 0)
        self.assertTrue(all(b - a >= 0.26 - 1e-9 for a, b in zip(times, times[1:])))
        self.assertLessEqual(clock.now, 1.04 + 1e-9)

    def test_late_wakeup_does_not_compress_following_frame_interval(self):
        clock = FakeClock()
        sleeps = 0
        def oversleep_once(seconds):
            nonlocal sleeps
            sleeps += 1
            clock.sleep(seconds + (0.099 if sleeps == 1 else 0))
        DDP.run(args("--fps", "10", "--seconds", "0.5"), clock.send,
                clock=clock.clock, sleep=oversleep_once, report=clock.report)
        times = [t for t, _ in clock.frames()]
        self.assertTrue(all(b - a >= 0.1 - 1e-9 for a, b in zip(times, times[1:])))

    def test_pause_boundary_interrupts_slow_frame(self):
        clock = FakeClock()
        def slow_send(packet):
            clock.send(packet)
            clock.now += 0.02
        stats = DDP.run(args("--fps", "10", "--seconds", "0.7",
                             "--pause-at", "0.1", "--pause-seconds", "0.2"),
                        slow_send, clock=clock.clock, sleep=clock.sleep, report=clock.report)
        self.assertFalse(any(0.1 <= t < 0.3 for t, _ in clock.packets))
        self.assertGreater(stats.datagrams, 0)
        self.assertEqual(clock.reports[-1][3], True)

    def test_pause_overload_overlap_and_return_to_normal(self):
        clock = FakeClock()
        stats = clock.run("--fps", "2", "--seconds", "4",
                          "--pause-at", "1", "--pause-seconds", "1",
                          "--overload-fps", "4", "--overload-at", "0.5",
                          "--overload-seconds", "2")
        self.assertEqual([t for t, _ in clock.frames()],
                         [0, 0.5, 0.75, 2, 2.25, 2.75, 3.25, 3.75])
        self.assertEqual([seq for _, seq in clock.frames()], list(range(1, 9)))
        self.assertFalse(any(1 <= t < 2 for t, _ in clock.packets))
        self.assertEqual(stats.frames, 8)
        self.assertEqual(clock.now, 4)

    def test_equal_rate_off_grid_boundaries_do_not_add_frames(self):
        clock = FakeClock()
        stats = clock.run("--fps", "2", "--seconds", "0.5",
                          "--overload-fps", "2", "--overload-at", "0.1",
                          "--overload-seconds", "0.1")
        self.assertEqual(clock.frames(), [(0, 1)])
        self.assertEqual((stats.frames, stats.datagrams), (1, 9))

    def test_off_grid_return_uses_normal_rate_interval(self):
        clock = FakeClock()
        stats = clock.run("--fps", "2", "--seconds", "2",
                          "--overload-fps", "4", "--overload-at", "0.1",
                          "--overload-seconds", "0.5")
        times = [t for t, _ in clock.frames()]
        self.assertEqual(times, [0, 0.25, 0.5, 1, 1.5])
        self.assertTrue(all(b - a >= 0.5 for a, b in zip(times[2:], times[3:])))
        self.assertEqual((stats.frames, stats.datagrams), (5, 45))

    def test_pause_to_end_and_interrupt_error_final_summary(self):
        clock = FakeClock()
        stats = clock.run("--fps", "2", "--seconds", "3",
                          "--pause-at", "0", "--pause-seconds", "5")
        self.assertEqual(stats.frames, 0)
        self.assertEqual(clock.now, 3)
        for exception in (KeyboardInterrupt(), OSError("test send failure")):
            clock = FakeClock()
            def fail(packet):
                raise exception
            with self.assertRaises(type(exception)):
                DDP.run(args("--seconds", "1"), fail, clock=clock.clock,
                        sleep=clock.sleep, report=clock.report)
            self.assertTrue(clock.reports[-1][3])

    def test_cli_error_and_socket_close(self):
        for exception, code in ((OSError("test send failure"), 1), (KeyboardInterrupt(), 0)):
            with mock.patch.object(DDP.socket, "getaddrinfo", return_value=[
                    (socket.AF_INET, socket.SOCK_DGRAM, 17, "", ("127.0.0.1", 4048))]), \
                    mock.patch.object(DDP.socket, "socket") as sock, \
                    redirect_stderr(io.StringIO()) as err, redirect_stdout(io.StringIO()) as out:
                sock.return_value.__enter__.return_value.sendto.side_effect = exception
                self.assertEqual(DDP.main(["--host", "127.0.0.1"]), code)
                sock.return_value.__exit__.assert_called_once()
                self.assertIn("final:", out.getvalue())
                if code:
                    self.assertIn("test send failure", err.getvalue())
                    self.assertNotIn("Traceback", err.getvalue())

    def test_send_timeout_reports_and_closes_socket(self):
        with mock.patch.object(DDP.socket, "getaddrinfo", return_value=[
                (socket.AF_INET, socket.SOCK_DGRAM, 17, "", ("127.0.0.1", 4048))]), \
                mock.patch.object(DDP.socket, "socket") as sock, \
                redirect_stderr(io.StringIO()) as err, redirect_stdout(io.StringIO()) as out:
            sender = sock.return_value.__enter__.return_value
            sender.sendto.side_effect = socket.timeout("send timed out")
            self.assertEqual(DDP.main(["--host", "127.0.0.1"]), 1)
            sender.settimeout.assert_called_once_with(0.25)
            sock.return_value.__exit__.assert_called_once()
            self.assertIn("local_sent_frames=0 local_sent_datagrams=0", out.getvalue())
            self.assertIn("final:", out.getvalue())
            self.assertIn("send timed out", err.getvalue())
            self.assertNotIn("Traceback", err.getvalue())


class LoopbackTest(unittest.TestCase):
    def test_finite_localhost_udp_cli(self):
        packets = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
            receiver.bind(("127.0.0.1", 0))
            receiver.settimeout(0.1)
            stop = threading.Event()
            def receive():
                while not stop.is_set():
                    try:
                        packets.append(receiver.recvfrom(2048)[0])
                    except socket.timeout:
                        continue
            thread = threading.Thread(target=receive)
            thread.start()
            try:
                result = subprocess.run([sys.executable, str(TOOL), "--host", "127.0.0.1",
                                         "--port", str(receiver.getsockname()[1]),
                                         "--fps", "5", "--seconds", "0.45"],
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                final = re.search(r"^final: .*local_sent_frames=(\d+) "
                                  r"local_sent_datagrams=(\d+) .*actual_fps=",
                                  result.stdout, re.MULTILINE)
                self.assertIsNotNone(final, result.stdout)
                frames, datagrams = map(int, final.groups())
                drain_end = time.monotonic() + 1
                while len(packets) < datagrams and time.monotonic() < drain_end:
                    stop.wait(0.01)
            finally:
                stop.set()
                thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
        self.assertLessEqual(frames, 3)
        self.assertEqual(datagrams // 9, frames)
        self.assertLessEqual(datagrams, 27)
        self.assertEqual(len(packets), datagrams)
        for index in range(frames + bool(datagrams % 9)):
            expected = list(DDP.packetize(DDP.render_frame(index), index))
            chunk_count = min(9, datagrams - index * 9)
            self.assertEqual(packets[index * 9:index * 9 + chunk_count],
                             expected[:chunk_count])


if __name__ == "__main__":
    unittest.main()
