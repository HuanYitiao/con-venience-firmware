#!/usr/bin/env python3
"""
Convert a 128x128 4-grayscale binary image to ST75256 display format C header.

Source format  : 128x128 pixels, 2 bpp, row-major, MSB-first within each byte
                 (4 pixels per byte: bits[7:6]=px0, [5:4]=px1, [3:2]=px2, [1:0]=px3)
                 Total: 4096 bytes

Display format : 256 columns x 32 pages
                 Each byte packs 4 vertical pixels in one column:
                   bits[7:6] = gray(col, page*4+0)
                   bits[5:4] = gray(col, page*4+1)
                   bits[3:2] = gray(col, page*4+2)
                   bits[1:0] = gray(col, page*4+3)
                 Write order: page 0 col 0..255, page 1 col 0..255, ...
                 Total: 8192 bytes

The 128-pixel-wide source image is centred on the 256-pixel-wide display
(64 columns of black on each side).
"""

import sys
import os

IMG_W      = 128
IMG_H      = 128
DISP_COLS  = 256
DISP_PAGES = 32
OFFSET_X   = (DISP_COLS - IMG_W) // 2   # = 64


def get_src_gray(src: bytes, sx: int, sy: int) -> int:
    """Return 2-bit gray level [0..3] for source pixel (sx, sy)."""
    if sx < 0 or sx >= IMG_W or sy < 0 or sy >= IMG_H:
        return 0
    byte_idx = sy * (IMG_W // 4) + sx // 4
    shift    = 6 - 2 * (sx % 4)          # MSB-first
    return (src[byte_idx] >> shift) & 0x03


def convert(input_path: str, output_path: str) -> None:
    with open(input_path, "rb") as f:
        src = f.read()

    if len(src) != IMG_W * IMG_H * 2 // 8:
        raise ValueError(f"Expected {IMG_W * IMG_H * 2 // 8} bytes, got {len(src)}")

    # Build display buffer: index = page * DISP_COLS + col
    disp = bytearray(DISP_COLS * DISP_PAGES)

    for page in range(DISP_PAGES):
        for col in range(DISP_COLS):
            sx      = col - OFFSET_X
            sy_base = page * 4
            g = [get_src_gray(src, sx, sy_base + r) for r in range(4)]
            disp[page * DISP_COLS + col] = (g[0] << 6) | (g[1] << 4) | (g[2] << 2) | g[3]

    var_name = os.path.splitext(os.path.basename(input_path))[0].replace("-", "_")

    with open(output_path, "w") as f:
        f.write("#pragma once\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"// {IMG_W}x{IMG_H} 4-grayscale image converted to ST75256 display format\n")
        f.write(f"// 256 cols x 32 pages, 2 bpp, 4 vertical pixels per byte\n")
        f.write(f"static const uint8_t PROGMEM {var_name}_data[{len(disp)}] = {{\n")
        for i in range(0, len(disp), 16):
            chunk    = disp[i : i + 16]
            hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
            f.write(f"    {hex_vals},\n")
        f.write("};\n")

    print(f"Converted: {input_path} -> {output_path} ({len(disp)} bytes)")


if __name__ == "__main__":
    inp = sys.argv[1] if len(sys.argv) > 1 else "lib/display_st75256/avatar.bin"
    out = sys.argv[2] if len(sys.argv) > 2 else "include/avatar.h"
    convert(inp, out)
