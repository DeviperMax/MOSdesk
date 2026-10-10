#!/usr/bin/env python3
import os
import struct
import sys
import zipfile
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
    from fontTools.ttLib import TTFont
except Exception as exc:  # pragma: no cover
    raise SystemExit(f"font conversion requires pillow and fonttools: {exc}")

HEX_CHARS = [chr(i) for i in range(32, 127)]


def discover_ttf_from_zip(zip_path):
    if not zip_path.exists():
        return None
    with zipfile.ZipFile(zip_path) as zf:
        for name in zf.namelist():
            if name.lower().endswith('.ttf') or name.lower().endswith('.otf'):
                tmp = Path('/tmp') / 'mosdesk_font_cache'
                tmp.mkdir(exist_ok=True)
                out = tmp / Path(name).name
                out.write_bytes(zf.read(name))
                return str(out)
    return None


def render_glyph_to_7x5(ch, font_path):
    font = ImageFont.truetype(font_path, size=32)
    canvas = Image.new('L', (32, 32), 0)
    draw = ImageDraw.Draw(canvas)
    bbox = draw.textbbox((0, 0), ch, font=font)
    text_w = max(1, bbox[2] - bbox[0])
    text_h = max(1, bbox[3] - bbox[1])
    x = max(0, (32 - text_w) // 2)
    y = max(0, (32 - text_h) // 2)
    draw.text((x, y), ch, font=font, fill=255)
    crop = canvas.crop((0, 0, 32, 32))
    resized = crop.resize((5, 7), Image.Resampling.NEAREST)
    pixels = list(resized.getdata())
    row_bytes = []
    for y0 in range(7):
        bits = 0
        for x0 in range(5):
            value = pixels[y0 * 5 + x0]
            if value > 128:
                bits |= 1 << (4 - x0)
        row_bytes.append(bits)
    return bytes(row_bytes)


def build_font(font_path, out_path):
    glyph_data = b''
    for ch in range(128):
        glyph = render_glyph_to_7x5(chr(ch), font_path)
        glyph_data += glyph
    header = b'TTRENDF\x00' + struct.pack('<I', 1)
    header += struct.pack('<I', 128)
    header += struct.pack('<I', 7)
    header += struct.pack('<I', 5)
    header += struct.pack('<I', 7)
    header += struct.pack('<I', 6)
    with open(out_path, 'wb') as fh:
        fh.write(header + glyph_data)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <output.font> [input.ttf|input.otf|Open_Sans.zip]", file=sys.stderr)
        return 1
    out_path = sys.argv[1]
    src = sys.argv[2] if len(sys.argv) >= 3 else None

    if src and src.lower().endswith(('.zip', '.ZIP')):
        src = discover_ttf_from_zip(Path(src))
    elif src is None:
        repo_root = Path(__file__).resolve().parents[2]
        zip_candidate = repo_root / 'Open_Sans.zip'
        if zip_candidate.exists():
            src = discover_ttf_from_zip(zip_candidate)
    if src is None:
        src = str(Path(__file__).resolve().parents[2] / 'Open_Sans.zip')
        src = discover_ttf_from_zip(Path(src))
    if src is None:
        print('No TTF/OTF font input found. Provide one explicitly.', file=sys.stderr)
        return 2

    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    build_font(src, out_path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
