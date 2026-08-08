#!/usr/bin/env python3
"""Call a Pixoo64 notification, reaction, or overlay-clear API over Wi-Fi.

Set PIXOO_HOST and PIXOO_API_ENCRYPTION_KEY, or pass --host and use a
permission-restricted --api-encryption-key-file. The key is never read from
repository files, accepted on the command line, or printed.

Examples:
  PIXOO_HOST=display.local PIXOO_API_ENCRYPTION_KEY=... tools/notify-pixoo.py "Message text"
  tools/notify-pixoo.py --host display.local --api-encryption-key-file ~/.config/pixoo/key --reaction laughing
  tools/notify-pixoo.py --host display.local --api-encryption-key-file ~/.config/pixoo/key --clear
"""

from __future__ import annotations

import argparse
import asyncio
import os
import stat
import sys
from pathlib import Path

REACTIONS = (
    "laughing", "love", "crying", "angry", "poop", "approve", "disapprove",
    "celebrate", "thinking", "surprised", "fire", "eyes",
)
SEVERITIES = ("info", "success", "warning", "error")
SOUNDS = (
    "chirp", "success", "pling1", "pling2", "pling3", "pling4", "alarm1",
    "alarm2", "alarm3",
)


def positive_int(value: str) -> int:
    try:
        number = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if number < 0:
        raise argparse.ArgumentTypeError("must be zero or greater")
    return number


def tcp_port(value: str) -> int:
    try:
        number = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if not 1 <= number <= 65535:
        raise argparse.ArgumentTypeError("must be between 1 and 65535")
    return number


def read_encryption_key(path: Path) -> str:
    """Read a key from a private regular file without a path replacement race."""
    try:
        expected = path.lstat()
    except OSError as exc:
        raise ValueError(f"cannot access encryption key file {path}") from exc
    if not stat.S_ISREG(expected.st_mode):
        raise ValueError("encryption key file must be a regular file")

    fd: int | None = None
    try:
        fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        actual = os.fstat(fd)
        if (actual.st_dev, actual.st_ino) != (expected.st_dev, expected.st_ino):
            raise ValueError("encryption key file changed while opening")
        if not stat.S_ISREG(actual.st_mode):
            raise ValueError("encryption key file must be a regular file")
        if actual.st_mode & (stat.S_IRWXG | stat.S_IRWXO):
            raise ValueError("encryption key file must not grant group or other permissions")
        with os.fdopen(fd, encoding="utf-8") as handle:
            fd = None
            key = handle.read().strip()
    except OSError as exc:
        raise ValueError(f"cannot read encryption key file {path}") from exc
    finally:
        if fd is not None:
            os.close(fd)
    if not key:
        raise ValueError("encryption key file is empty")
    return key


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host", default=os.environ.get("PIXOO_HOST"),
        help="ESPHome host (required; or set PIXOO_HOST)",
    )
    parser.add_argument(
        "--api-encryption-key-file", type=Path,
        help="file containing the ESPHome API encryption key (must not be accessible to group or other users)",
    )
    parser.add_argument("--port", type=tcp_port, default=6053, help="ESPHome API port (default: 6053)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--reaction", choices=REACTIONS, help="show a named reaction")
    mode.add_argument("--clear", action="store_true", help="clear the overlay queue")
    parser.add_argument("message", nargs="?", help="notification text")
    parser.add_argument("severity", nargs="?", choices=SEVERITIES, default="info")
    parser.add_argument("duration", nargs="?", type=positive_int, default=4)
    parser.add_argument("sound", nargs="?", choices=SOUNDS)
    return parser


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = build_parser()
    argv = sys.argv[1:] if argv is None else argv
    if any(arg == "--api-encryption-key" or arg.startswith("--api-encryption-key=") for arg in argv):
        parser.error("--api-encryption-key is not supported; use PIXOO_API_ENCRYPTION_KEY or --api-encryption-key-file")
    args = parser.parse_args(argv)
    if not args.host:
        parser.error("--host is required (or set PIXOO_HOST)")
    if args.api_encryption_key_file:
        try:
            args.api_encryption_key = read_encryption_key(args.api_encryption_key_file)
        except ValueError as exc:
            parser.error(str(exc))
    else:
        args.api_encryption_key = os.environ.get("PIXOO_API_ENCRYPTION_KEY")
    if not args.api_encryption_key:
        parser.error("PIXOO_API_ENCRYPTION_KEY or --api-encryption-key-file is required")
    if (args.reaction or args.clear) and args.message is not None:
        parser.error("message arguments cannot be combined with --reaction or --clear")
    if not args.reaction and not args.clear and args.message is None:
        parser.error("provide a message, --reaction NAME, or --clear")
    return args


def service_request(args: argparse.Namespace) -> tuple[str, dict[str, object]]:
    if args.clear:
        return "clear_overlay_queue", {}
    if args.reaction:
        return "reaction", {"reaction": args.reaction}
    return "notify", {
        "message": args.message,
        "severity": args.severity,
        "duration": args.duration,
        "sound": args.sound or "",
    }


async def send(args: argparse.Namespace) -> None:
    try:
        from aioesphomeapi import APIClient
    except ImportError as exc:
        raise SystemExit(
            "notify-pixoo.py requires aioesphomeapi; install the public tool dependencies"
        ) from exc

    service_name, data = service_request(args)
    cli = APIClient(args.host, args.port, password="", noise_psk=args.api_encryption_key)
    try:
        await cli.connect(login=True)
        _, services = await cli.list_entities_services()
        target = next((service for service in services if service.name == service_name), None)
        if target is None:
            available = ", ".join(service.name for service in services)
            raise SystemExit(f"service '{service_name}' not found; available: {available}")
        await cli.execute_service(target, data)
        print(f"sent {service_name}")
    finally:
        await cli.disconnect()


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    asyncio.run(send(args))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
