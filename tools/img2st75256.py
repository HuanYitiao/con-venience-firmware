#!/usr/bin/env python3
"""
Convert a 128x128 4-grayscale binary image to ST75256 display format C header.

Source format  : 128x128 pixels, 2 bpp, row-major, MSB-first within each byte
                 (4 pixels per byte: bits[7:6]=px0, [5:4]=px1, [3:2]=px2, [1:0]=px3)
                 Total: 4096 bytes

Display format : 128 columns x 32 pages (full-width, matches draw(..., w=128))
                 Each byte packs 4 vertical pixels in one column:
                   bits[7:6] = gray(col, page*4+0)
                   bits[5:4] = gray(col, page*4+1)
                   bits[3:2] = gray(col, page*4+2)
                   bits[1:0] = gray(col, page*4+3)
                 Write order: page 0 col 0..127, page 1 col 0..127, ...
                 Total: 4096 bytes

The 128-pixel-wide source image fills the display area directly.
Use --mode 256 for centred output with 64-column margins on each side.

The 128-pixel-wide source image is centred on the 256-pixel-wide display
(64 columns of black on each side).
"""

import argparse
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
IMG_W = 128   # source image width  (pixels)
IMG_H = 128   # source image height (pixels)

# 2-bit gray levels (source and display use the same convention)
#   0b00 = white  (background)
#   0b01 = light-gray
#   0b10 = dark-gray
#   0b11 = black  (foreground)
GRAY_00 = 0b00
GRAY_01 = 0b01
GRAY_10 = 0b10
GRAY_11 = 0b11

# Display geometry
DISP_PAGES    = 32    # 128 rows / 4 rows-per-page
DISP_COLS_256 = 256   # centred mode
DISP_COLS_128 = 128   # full-width mode (matches draw() with w=128)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def read_source(path: Path) -> bytes:
    """Read and validate the 128×128 4-gray binary file (4096 bytes)."""
    expected = IMG_W * IMG_H * 2 // 8   # = 4096
    data = path.read_bytes()
    if len(data) != expected:
        raise ValueError(
            f"Expected {expected} bytes for a {IMG_W}×{IMG_H} 2-bpp image, "
            f"got {len(data)} bytes."
        )
    return data


def get_pixel(src: bytes, x: int, y: int) -> int:
    """Return the 2-bit gray value at source pixel (x, y).
    Pixels outside [0..127]×[0..127] clamp to white (0b00).
    """
    if not (0 <= x < IMG_W and 0 <= y < IMG_H):
        return GRAY_00
    byte_idx = y * (IMG_W // 4) + x // 4
    shift    = 6 - 2 * (x % 4)   # MSB-first packing
    return (src[byte_idx] >> shift) & 0x03


def build_display_buffer(
    src: bytes, disp_cols: int, offset_x: int
) -> bytearray:
    """Convert the source image into the ST75256 display byte-stream.

    Each output byte packs 4 *vertical* pixels (2 bits each) in a column:
        bits[7:6] = gray(col, page*4 + 0)   ← top pixel
        bits[5:4] = gray(col, page*4 + 1)
        bits[3:2] = gray(col, page*4 + 2)
        bits[1:0] = gray(col, page*4 + 3)   ← bottom pixel

    Parameters
    ----------
    src       : raw 4096-byte source image.
    disp_cols : display width in columns (256 = centred, 128 = full-width).
    offset_x  : horizontal offset so the source is positioned correctly.
    """
    buf = bytearray(disp_cols * DISP_PAGES)

    for page in range(DISP_PAGES):
        sy_base = page * 4
        for col in range(disp_cols):
            sx = col - offset_x
            gray = [
                get_pixel(src, sx, sy_base + 0),
                get_pixel(src, sx, sy_base + 1),
                get_pixel(src, sx, sy_base + 2),
                get_pixel(src, sx, sy_base + 3),
            ]
            byte_val = (
                (gray[0] << 6) | (gray[1] << 4) | (gray[2] << 2) | gray[3]
            )
            buf[page * disp_cols + col] = byte_val

    return buf


def write_header(
    buf: bytearray,
    output_path: Path,
    var_name: str,
    disp_cols: int,
) -> None:
    """Write the display buffer as a C header with PROGMEM attribute."""
    BYTES_PER_LINE = 16

    with output_path.open("w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(
            f"// {IMG_W}×{IMG_H} 4-grayscale image → ST75256 format\n"
        )
        f.write(
            f"// {disp_cols} cols × {DISP_PAGES} pages, 2 bpp\n"
        )
        f.write(
            f"// Gray levels: 00=white, 01=light-gray, 10=dark-gray, 11=black\n"
        )
        f.write(
            f"static const uint8_t PROGMEM {var_name}_data[{len(buf)}] = {{\n"
        )

        for offset in range(0, len(buf), BYTES_PER_LINE):
            chunk = buf[offset : offset + BYTES_PER_LINE]
            line  = ", ".join(f"0x{b:02X}" for b in chunk)
            f.write(f"    {line},\n")

        f.write("};\n")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert 128×128 4-gray .bin → ST75256 .h header",
    )
    parser.add_argument(
        "input",
        type=Path,
        help="Path to the 128×128 4-gray .bin file (4096 bytes)",
    )
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=None,
        help="Path for the output .h file (default: <input_stem>.h in same dir)",
    )
    parser.add_argument(
        "--mode",
        choices=["256", "128"],
        default="128",
        help="Display width: 128=full-width (default), 256=centred",
    )
    args = parser.parse_args()

    # Resolve paths
    src_path = args.input.resolve()
    if not src_path.is_file():
        sys.exit(f"Error: input file not found: {src_path}")

    out_path = (
        args.output.resolve() if args.output else src_path.with_suffix(".h")
    )

    # Determine geometry
    if args.mode == "128":
        disp_cols = DISP_COLS_128
        offset_x  = 0
    else:
        disp_cols = DISP_COLS_256
        offset_x  = (DISP_COLS_256 - IMG_W) // 2   # = 64

    # Derive C variable name from file stem
    var_name = src_path.stem.replace("-", "_").replace(" ", "_")

    # Pipeline
    src = read_source(src_path)
    buf = build_display_buffer(src, disp_cols, offset_x)
    write_header(buf, out_path, var_name, disp_cols)

    print(
        f"✓ Converted: {src_path.name} → {out_path.name} "
        f"({disp_cols}×{DISP_PAGES}, {len(buf)} bytes)"
    )


if __name__ == "__main__":
    main()
