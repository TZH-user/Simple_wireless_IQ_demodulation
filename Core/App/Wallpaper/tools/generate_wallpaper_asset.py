#!/usr/bin/env python3
"""Generate the optional external-QSPI LVGL background for this project."""
from pathlib import Path
import argparse
import struct
from PIL import Image, ImageOps

WIDTH, HEIGHT = 800, 480
DATA_ADDR = 0x001000
RESERVED_END = 0x0BD000
MAGIC = 0x47424C57
VERSION = 1
FORMAT_RGB565 = 1
FNV_SEED = 2166136261

def fnv1a(data: bytes) -> int:
    value = FNV_SEED
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value

def encode_rgb565le(img: Image.Image) -> bytes:
    out = bytearray()
    raw = img.tobytes()
    for index in range(0, len(raw), 3):
        r, g, b = raw[index], raw[index + 1], raw[index + 2]
        pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out += struct.pack('<H', pixel)
    return bytes(out)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('input_image', type=Path)
    parser.add_argument('--out-dir', type=Path, default=Path('../Assets'))
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    src = Image.open(args.input_image).convert('RGB')
    fitted = ImageOps.fit(src, (WIDTH, HEIGHT), method=Image.Resampling.LANCZOS)
    fitted.save(args.out_dir / 'wallpaper_preview_800x480.png')
    payload = encode_rgb565le(fitted)
    checksum = fnv1a(payload)
    (args.out_dir / 'wallpaper_800x480.rgb565').write_bytes(payload)
    header = struct.pack('<IHHHHII', MAGIC, VERSION, FORMAT_RGB565, WIDTH, HEIGHT, len(payload), checksum)
    blob = header + bytes([0xFF]) * (DATA_ADDR - len(header)) + payload
    (args.out_dir / 'wallpaper_qspi_0x000000.bin').write_bytes(blob)
    padded = blob + bytes([0xFF]) * (RESERVED_END - len(blob))
    (args.out_dir / 'wallpaper_qspi_reserved_0x000000_0x0BCFFF.bin').write_bytes(padded)
    print(f'RGB565={len(payload)} bytes, checksum=0x{checksum:08X}, qspi_blob={len(blob)} bytes')

if __name__ == '__main__':
    main()
