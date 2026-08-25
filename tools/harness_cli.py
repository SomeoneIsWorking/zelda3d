#!/usr/bin/env python3
"""Command-line interface for driving the embedded OoT3D harness."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Iterable

from harness_gameplay import boot_to_gameplay
from harness_process import spawn
from harness_transport import Harness


def _send_complete(harness: Harness, command: str) -> list[str]:
    """Read the complete wire response without duplicating command-shape policy."""
    return harness.send_multiline(command)


def cmd_send(args: argparse.Namespace, harness: Harness) -> int:
    lines = _send_complete(harness, args.line)
    print(*lines, sep="\n")
    return int(lines[-1].startswith("err "))


def cmd_repl(_args: argparse.Namespace, harness: Harness) -> int:
    print(
        "harness repl — 'quit' to exit. Streaming commands ('actors', "
        "'compare', etc) read through their protocol terminator.",
        file=sys.stderr,
    )
    while True:
        try:
            line = input("> ")
        except (EOFError, KeyboardInterrupt):
            break
        stripped = line.strip()
        if not stripped:
            continue
        if stripped == "quit":
            break
        print(*_send_complete(harness, stripped), sep="\n")
    return 0


def cmd_warp(args: argparse.Namespace, harness: Harness) -> int:
    before = harness.send("scene")
    if not boot_to_gameplay(
        harness, entrance=args.entrance, settle_frames=args.settle_frames
    ):
        raise RuntimeError("warp failed — see harness diagnostics above")
    print(f"[harness] scene {before} -> {harness.send('scene')}", file=sys.stderr)
    return 0


def cmd_boot_to_play(args: argparse.Namespace, harness: Harness) -> int:
    if not boot_to_gameplay(harness, entrance=args.entrance):
        raise RuntimeError("boot-to-play failed — see harness diagnostics above")
    print("[harness] boot-to-play: ok", file=sys.stderr)
    return 0


def cmd_peek(args: argparse.Namespace, harness: Harness) -> int:
    remaining = args.n
    address = args.va
    while remaining > 0:
        chunk_size = min(remaining, 4096)
        response = harness.send(f"mem 0x{address:08x} {chunk_size}")
        if not response.startswith("ok "):
            print(response)
            return 1
        blob = bytes.fromhex(response[3:])
        for offset in range(0, len(blob), 16):
            row = blob[offset : offset + 16]
            hexadecimal = " ".join(f"{byte:02x}" for byte in row)
            ascii_text = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in row)
            print(f"0x{address + offset:08x}  {hexadecimal:<47}  {ascii_text}")
        address += chunk_size
        remaining -= chunk_size
    return 0


def create_parser(description: str) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--save-state", help="loadstate this file after boot")
    subparsers = parser.add_subparsers(dest="cmd", required=True)

    repl = subparsers.add_parser("repl", help="interactive REPL over the wire protocol")
    repl.set_defaults(func=cmd_repl)

    send = subparsers.add_parser("send", help="send one command, print response")
    send.add_argument("line")
    send.set_defaults(func=cmd_send)

    warp = subparsers.add_parser("warp", help="reach gameplay, then warp to entrance")
    warp.add_argument("entrance", type=lambda value: int(value, 0))
    warp.add_argument(
        "--settle-frames",
        type=int,
        default=180,
        help="frames to run after the warp (default 180)",
    )
    warp.set_defaults(func=cmd_warp)

    boot = subparsers.add_parser(
        "boot-to-play", help="loadstate/capture a gameplay state, then optionally warp"
    )
    boot.add_argument("--entrance", type=lambda value: int(value, 0), default=None)
    boot.set_defaults(func=cmd_boot_to_play)

    peek = subparsers.add_parser("peek", help="hex-dump <n> bytes at <va>")
    peek.add_argument("va", type=lambda value: int(value, 0))
    peek.add_argument("n", type=int)
    peek.set_defaults(func=cmd_peek)
    return parser


def main(argv: Iterable[str], description: str = __doc__) -> int:
    args = create_parser(description).parse_args(list(argv))
    try:
        with spawn(save_state=args.save_state) as harness:
            return int(args.func(args, harness))
    except (RuntimeError, TimeoutError) as error:
        print(f"[harness] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
