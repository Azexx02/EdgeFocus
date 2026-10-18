#!/usr/bin/env python3
"""Patch a DTB node status value without changing FDT layout.

This intentionally only supports replacing an existing status string with a
same-length string, so the DTB structure and string tables stay untouched.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9


def align4(value: int) -> int:
    return (value + 3) & ~3


def cstring(blob: bytes, offset: int) -> str:
    end = blob.index(b"\0", offset)
    return blob[offset:end].decode("ascii")


def patch_status(src: Path, dst: Path, node_name: str, old: bytes, new: bytes) -> None:
    if len(old) != len(new):
        raise ValueError("old and new values must have the same byte length")

    data = bytearray(src.read_bytes())
    header = struct.unpack(">10I", data[:40])
    magic, totalsize, off_struct, off_strings, _off_rsvmap, _version, *_rest = header
    size_strings = header[8]
    size_struct = header[9]
    if magic != FDT_MAGIC:
        raise ValueError(f"{src} is not an FDT/DTB file")

    strings = bytes(data[off_strings : off_strings + size_strings])
    pos = off_struct
    end_struct = off_struct + size_struct
    stack: list[str] = []
    patched_offsets: list[int] = []

    while pos < end_struct:
        token = struct.unpack_from(">I", data, pos)[0]
        pos += 4

        if token == FDT_BEGIN_NODE:
            name_end = data.index(0, pos)
            name = data[pos:name_end].decode("ascii")
            stack.append(name)
            pos = align4(name_end + 1)
        elif token == FDT_END_NODE:
            if stack:
                stack.pop()
        elif token == FDT_PROP:
            prop_len, nameoff = struct.unpack_from(">II", data, pos)
            pos += 8
            value_pos = pos
            prop_name = cstring(strings, nameoff)
            current_node = stack[-1] if stack else ""
            if current_node == node_name and prop_name == "status":
                value = bytes(data[value_pos : value_pos + prop_len])
                if value != old:
                    raise ValueError(
                        f"{node_name} status is {value!r}, expected {old!r}"
                    )
                data[value_pos : value_pos + prop_len] = new
                patched_offsets.append(value_pos)
            pos = align4(value_pos + prop_len)
        elif token == FDT_NOP:
            continue
        elif token == FDT_END:
            break
        else:
            raise ValueError(f"unexpected FDT token {token} at 0x{pos - 4:x}")

    if len(patched_offsets) != 1:
        raise ValueError(
            f"expected exactly one patch for {node_name}, got {len(patched_offsets)}"
        )

    dst.write_bytes(data[:totalsize])
    print(f"patched {node_name} status at offset 0x{patched_offsets[0]:x}: {old!r} -> {new!r}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("src", type=Path)
    parser.add_argument("dst", type=Path)
    parser.add_argument("--node-name", default="can@402d0000")
    parser.add_argument("--old", default="okay")
    parser.add_argument("--new", default="fail")
    args = parser.parse_args()

    patch_status(
        args.src,
        args.dst,
        args.node_name,
        args.old.encode("ascii") + b"\0",
        args.new.encode("ascii") + b"\0",
    )


if __name__ == "__main__":
    main()
