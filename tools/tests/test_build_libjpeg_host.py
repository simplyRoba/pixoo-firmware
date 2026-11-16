"""Pinned native JPEG source download and extraction checks."""
import hashlib
import importlib.util
import io
from pathlib import Path
import shlex
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
    def test_build_flags_address_archive_content_and_quote_paths(self):
        with tempfile.TemporaryDirectory(prefix="jpeg test ") as directory:
            root = Path(directory)
            prefix = root / "install"
            library_dir = prefix / "lib"
            library_dir.mkdir(parents=True)
            archive = library_dir / "libjpeg.a"
            compiler = ("ccache", "clang", "-DNAME=a b")
            with patch.object(helper, "build", return_value=prefix) as build:
                archive.write_bytes(b"first archive")
                flags = helper.build_flags(root, compiler)
                build.assert_called_with(root, compiler)
                digest = hashlib.sha256(b"first archive").hexdigest()
                name = f"pixoo_jpeg_{digest}"
                copied = library_dir / f"lib{name}.a"
                self.assertEqual(copied.read_bytes(), b"first archive")
                self.assertEqual(shlex.split(flags[0]), [f"-I{prefix / 'include'}"])
                self.assertEqual(shlex.split(flags[1]), [f"-L{library_dir}"])
                self.assertEqual(flags[2], f"-l{name}")
                with patch.object(Path, "write_bytes", side_effect=AssertionError("unnecessary copy")):
                    self.assertEqual(helper.build_flags(root, compiler), flags)
                archive.write_bytes(b"changed archive")
                changed = helper.build_flags(root, compiler)
                new_digest = hashlib.sha256(b"changed archive").hexdigest()
                self.assertEqual(changed[2], f"-lpixoo_jpeg_{new_digest}")
                self.assertNotEqual(changed[2], flags[2])
                self.assertEqual((library_dir / f"libpixoo_jpeg_{new_digest}.a").read_bytes(), b"changed archive")

    def test_build_forwards_compiler_argv_to_cmake(self):
        with tempfile.TemporaryDirectory(prefix="jpeg build ") as directory:
            root = Path(directory)
            (root / f"libjpeg-turbo-{helper.VERSION}").mkdir()
            compiler = ("/path with spaces/ccache", "clang", "-DNAME=a b")
            with patch.object(helper.shutil, "which", return_value="cmake"), patch.object(
                helper, "download_archive"
            ), patch.object(helper.subprocess, "run") as run:
                prefix = helper.build(root, compiler)
                changed = helper.build(root, (*compiler, "-DOTHER=1"))
            self.assertNotEqual(prefix, changed)
            self.assertEqual(prefix.name, "install")
            self.assertEqual(prefix.parent.parent, root)
            command = run.call_args_list[0].args[0]
            self.assertEqual(command[command.index("-B") + 1], str(prefix.parent))
            self.assertIn(f"-DCMAKE_INSTALL_PREFIX={prefix}", command)
            self.assertIn(f"-DCMAKE_C_COMPILER={compiler[0]}", command)
            self.assertIn(f"-DCMAKE_C_COMPILER_ARG1={shlex.join(compiler[1:])}", command)
            self.assertEqual(run.call_count, 6)

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
