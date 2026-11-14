import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEST = ROOT / "esphome/tests/render_worker_test"


class RenderWorkerTest(unittest.TestCase):
    def _run_header_test(self, name):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / name
            source = TEST / f"{name}.cpp"
            command = ["c++", "-std=c++17", "-pthread",
                       "-I", str(ROOT / "esphome/components/pixoo64"),
                       "-I", str(ROOT / "pixoo_app/include"),
                       str(source), "-o", str(binary)]
            subprocess.run(command, check=True, cwd=ROOT, timeout=60)
            subprocess.run([str(binary)], check=True, cwd=ROOT, timeout=20)

    def test_queue_ownership(self):
        self._run_header_test("render_worker_queue_test")

    def test_bridge_values(self):
        self._run_header_test("render_worker_bridge_test")

    def test_threaded_worker(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "render_worker_test"
            includes = [TEST / "include", ROOT / "esphome/tests/spi_dma_test/include",
                        ROOT / "esphome/components/pixoo64", ROOT / "pixoo_protocol/include",
                        ROOT / "pixoo_app/include"]
            sources = [TEST / "render_worker_test.cpp",
                       ROOT / "esphome/components/pixoo64/render_worker.cpp",
                       ROOT / "pixoo_app/src/firmware_app.cpp",
                       ROOT / "pixoo_app/src/app_state.cpp",
                       ROOT / "pixoo_app/src/pixoo_output.cpp"]
            command = ["c++", "-std=c++17", "-pthread"]
            for include in includes:
                command += ["-I", str(include)]
            command += [str(source) for source in sources] + ["-o", str(binary)]
            subprocess.run(command, check=True, cwd=ROOT, timeout=60)
            subprocess.run([str(binary)], check=True, cwd=ROOT, timeout=20)


if __name__ == "__main__":
    unittest.main()
