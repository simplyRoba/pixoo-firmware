import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEST = ROOT / "esphome/tests/spi_dma_test"


class SpiDmaTransportTest(unittest.TestCase):
    def test_async_transport(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "spi_dma_test"
            command = ["c++", "-std=c++17"]
            command += ["-I", str(TEST / "include"),
                        "-I", str(ROOT / "esphome/components/pixoo64"),
                        "-I", str(ROOT / "pixoo_protocol/include"), "-I", str(ROOT / "pixoo_app/include"),
                        str(TEST / "transport_test.cpp"), str(ROOT / "esphome/components/pixoo64/pixoo64.cpp"),
                        str(ROOT / "pixoo_protocol/src/pixoo_frame.cpp"),
                        str(ROOT / "pixoo_protocol/src/pixoo_uart.cpp"),
                        str(ROOT / "pixoo_app/src/pixoo_output.cpp"), "-o", str(binary)]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
