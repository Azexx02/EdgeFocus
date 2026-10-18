import struct

base = "/sys/class/graphics/fb0"

with open(base + "/virtual_size", "r", encoding="ascii") as f:
    width, height = map(int, f.read().strip().split(","))

with open(base + "/stride", "r", encoding="ascii") as f:
    stride = int(f.read().strip())

with open(base + "/bits_per_pixel", "r", encoding="ascii") as f:
    bpp = int(f.read().strip())

colors = [0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xFFE0]

with open("/dev/fb0", "wb", buffering=0) as fb:
    for y in range(height):
        color = colors[(y * len(colors)) // height]
        row = struct.pack("<H", color) * width
        fb.write(row + b"\0" * (stride - len(row)))

print("FB_BARS_WRITTEN", width, height, stride, bpp)
