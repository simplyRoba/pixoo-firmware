#!/usr/bin/env python3
"""Build pinned libjpeg-turbo for the PlatformIO native render target."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import urllib.request

VERSION = "3.2.0"
URL = f"https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/{VERSION}/libjpeg-turbo-{VERSION}.tar.gz"
SHA256 = "6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e"
OPTIONS = {
    "ENABLE_SHARED": "OFF",
    "ENABLE_STATIC": "ON",
    "WITH_TURBOJPEG": "OFF",
    "WITH_SIMD": "OFF",
    "WITH_TOOLS": "OFF",
    "WITH_TESTS": "OFF",
    "WITH_JPEG8": "ON",
}


def verify_archive(path: Path) -> None:
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != SHA256:
        raise ValueError(f"libjpeg-turbo archive checksum mismatch: {path}")


def download_archive(path: Path) -> None:
    if path.exists():
        verify_archive(path)
        return
    temporary = path.with_suffix(".download")
    try:
        with urllib.request.urlopen(URL, timeout=60) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        verify_archive(temporary)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def extract_archive(archive: Path, destination: Path) -> None:
    """Accept only ordinary files/directories below the expected source root."""
    with tarfile.open(archive, "r:gz") as source:
        members = source.getmembers()
        for member in members:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or ".." in path.parts or
                    not path.parts or path.parts[0] != f"libjpeg-turbo-{VERSION}" or
                    not (member.isfile() or member.isdir())):
                raise ValueError(f"Unsafe libjpeg-turbo archive entry: {member.name}")
        # Explicit filtering also rejects links, devices, and paths escaping the
        # destination; validation above applies before extracting any entry.
        source.extractall(destination, members=members, filter="data")


def build(root: Path, compiler: str) -> Path:
    cmake = shutil.which("cmake")
    if cmake is None:
        raise RuntimeError("Native JPEG build requires CMake >= 3.15 on PATH (brew install cmake or apt install cmake).")
    root.mkdir(parents=True, exist_ok=True)
    archive = root / f"libjpeg-turbo-{VERSION}.tar.gz"
    download_archive(archive)
    source = root / f"libjpeg-turbo-{VERSION}"
    if not source.exists():
        staging = root / "extract"
        shutil.rmtree(staging, ignore_errors=True)
        staging.mkdir()
        try:
            extract_archive(archive, staging)
            (staging / source.name).replace(source)
        finally:
            shutil.rmtree(staging, ignore_errors=True)
    binary = root / "build"
    prefix = root / "install"
    subprocess.run([
        cmake, "-S", str(source), "-B", str(binary), "-G", "Unix Makefiles",
        "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_C_COMPILER={compiler}",
        f"-DCMAKE_INSTALL_PREFIX={prefix}", "-DCMAKE_INSTALL_LIBDIR=lib",
        *[f"-D{key}={value}" for key, value in OPTIONS.items()],
    ], check=True)
    subprocess.run([cmake, "--build", str(binary), "--parallel", str(os.cpu_count() or 2)], check=True)
    subprocess.run([cmake, "--install", str(binary)], check=True)
    return prefix


def platformio_setup(env) -> None:
    root = Path(env.subst("$BUILD_DIR")).resolve() / "libjpeg-turbo"
    compiler = env.WhereIs(env.subst("$CC"))
    if not compiler:
        raise RuntimeError("Native JPEG build requires a C compiler on PATH")
    prefix = build(root, compiler)
    env.Append(CPPPATH=[str(prefix / "include")])
    # Application objects (including the project-owned jmemnobs backend) precede
    # LIBS. Use the exact archive, never a system -ljpeg or a dynamic library.
    env.Append(LIBS=[env.File(str(prefix / "lib/libjpeg.a"))])


if "Import" in globals():
    Import("env")  # noqa: F821 -- PlatformIO/SCons supplies Import
    platformio_setup(env)  # noqa: F821
