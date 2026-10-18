#!/usr/bin/env python3
from pathlib import Path

BYTES_PER_PIXEL = 2
COLORS_RGB565 = [
    0xF800,  # red
    0x07E0,  # green
    0x001F,  # blue
    0xFFE0,  # yellow
    0xF81F,  # magenta
    0x07FF,  # cyan
    0xFFFF,  # white
    0x0000,  # black
]


def main() -> None:
    width, height = (
        int(value)
        for value in Path("/sys/class/graphics/fb0/virtual_size").read_text().strip().split(",")
    )
    stride_path = Path("/sys/class/graphics/fb0/stride")
    stride = int(stride_path.read_text().strip()) if stride_path.exists() else width * BYTES_PER_PIXEL
    visible_bytes = width * BYTES_PER_PIXEL
    if stride < visible_bytes:
        raise RuntimeError(f"fb0 stride {stride} is smaller than visible line {visible_bytes}")

    line = bytearray()
    for x in range(width):
        color = COLORS_RGB565[(x * len(COLORS_RGB565)) // width]
        line.extend((color & 0xFF, color >> 8))
    line.extend(b"\x00" * (stride - visible_bytes))

    fb = Path("/dev/fb0")
    with fb.open("wb") as fp:
        for _ in range(height):
            fp.write(line)


if __name__ == "__main__":
    main()
