"""Pinned native JPEG source download and extraction checks."""
import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "build_libjpeg_host", Path(__file__).resolve().parents[1] / "build-libjpeg-host.py"
)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class LibjpegHostBuildTest(unittest.TestCase):
    def test_checksum_rejects_corrupt_cached_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "source.tar.gz"
            archive.write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                helper.download_archive(archive)

    def test_download_verifies_before_installing_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "source.tar.gz"
            with patch.object(helper.urllib.request, "urlopen", return_value=io.BytesIO(b"corrupt")):
                with self.assertRaises(ValueError):
                    helper.download_archive(archive)
            self.assertFalse(archive.exists())
            self.assertFalse(archive.with_suffix(".download").exists())
            with patch.object(helper, "SHA256", hashlib.sha256(b"valid").hexdigest()), patch.object(
                helper.urllib.request, "urlopen", return_value=io.BytesIO(b"valid")
            ):
                helper.download_archive(archive)
                helper.download_archive(archive)
            self.assertEqual(archive.read_bytes(), b"valid")

    def test_extraction_rejects_unsafe_entries_before_writing(self):
        root = f"libjpeg-turbo-{helper.VERSION}"
        for name, kind in (
            ("/absolute", tarfile.REGTYPE),
            (f"{root}/../../escape", tarfile.REGTYPE),
            ("wrong-root/file", tarfile.REGTYPE),
            (f"{root}/link", tarfile.SYMTYPE),
            (f"{root}/hardlink", tarfile.LNKTYPE),
            (f"{root}/device", tarfile.CHRTYPE),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                archive = Path(directory) / "source.tar.gz"
                destination = Path(directory) / "output"
                destination.mkdir()
                with tarfile.open(archive, "w:gz") as output:
                    output.addfile(tarfile.TarInfo(f"{root}/valid"), io.BytesIO())
                    bad = tarfile.TarInfo(name)
                    bad.type = kind
                    output.addfile(bad)
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    helper.extract_archive(archive, destination)
                self.assertEqual(list(destination.iterdir()), [])

    def test_extracts_regular_source_file(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "source.tar.gz"
            name = f"libjpeg-turbo-{helper.VERSION}/CMakeLists.txt"
            with tarfile.open(archive, "w:gz") as output:
                member = tarfile.TarInfo(name)
                member.size = 5
                output.addfile(member, io.BytesIO(b"hello"))
            destination = Path(directory) / "output"
            helper.extract_archive(archive, destination)
            self.assertEqual((destination / name).read_bytes(), b"hello")


if __name__ == "__main__":
    unittest.main()
