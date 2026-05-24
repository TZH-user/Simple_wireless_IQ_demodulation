#!/usr/bin/env python3
"""Convert a boot animation background image to 800x480 RGB565 little-endian raw.

Usage:
  python Tools/convert_boot_image_to_rgb565.py input.png App/BootAnim/Assets/boot_bg_800x480.rgb565
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path
from PIL import Image

W, H = 800, 480
TARGET = W / H


def crop_to_aspect(img: Image.Image) -> Image.Image:
    w, h = img.size
    aspect = w / h
    if aspect > TARGET:
        new_w = int(h * TARGET)
        left = (w - new_w) // 2
        return img.crop((left, 0, left + new_w, h))
    if aspect < TARGET:
        new_h = int(w / TARGET)
        top = (h - new_h) // 2
        return img.crop((0, top, w, top + new_h))
    return img


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip())
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    dst.parent.mkdir(parents=True, exist_ok=True)

    img = Image.open(src).convert("RGB")
    img = crop_to_aspect(img).resize((W, H), Image.LANCZOS)
    preview = dst.with_suffix(".preview.png")
    img.save(preview)

    with dst.open("wb") as f:
        for r, g, b in img.getdata():
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)
            f.write(struct.pack("<H", rgb565))

    print(f"wrote {dst} ({dst.stat().st_size} bytes)")
    print(f"preview {preview}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
