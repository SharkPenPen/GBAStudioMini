#!/usr/bin/env python3
"""Generate a native-size 1-bit atlas from an embedded-bitmap TTF/OTF.

Firefly 14px and similar fonts store their pixel strikes in EBDT/EBLC. Pillow
rasterizes outlines, so this tool uses FreeType directly and requests the
embedded bitmap strike. It refuses to scale.

Requires: pip install freetype-py
The generated atlas is game data; the editor GUI is not involved.
"""
import argparse
import json
import math
from pathlib import Path

import freetype
from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, required=True, help="must match a native strike")
    ap.add_argument("--text", required=True)
    ap.add_argument("--columns", type=int, default=16)
    args = ap.parse_args()

    face = freetype.Face(args.font)
    strikes = sorted({s.y_ppem // 64 for s in face.available_sizes})
    if args.size not in strikes:
        raise SystemExit(f"no native {args.size}px strike; available: {strikes}")
    face.set_pixel_sizes(0, args.size)

    cps = []
    for ch in args.text:
        cp = ord(ch)
        if cp not in cps:
            cps.append(cp)

    cell_w = max(1, args.size)
    cell_h = max(1, args.size)
    cols = max(1, args.columns)
    rows = math.ceil(len(cps) / cols)
    atlas = Image.new("L", (cols * cell_w, rows * cell_h), 0)
    draw = ImageDraw.Draw(atlas)
    glyphs = []

    for i, cp in enumerate(cps):
        face.load_char(chr(cp), freetype.FT_LOAD_RENDER |
                       freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_MONOCHROME)
        g = face.glyph
        bm = g.bitmap
        gx = (i % cols) * cell_w
        gy = (i // cols) * cell_h
        # FreeType bitmap is row-major, MSB first, pitch bytes per row.
        for yy in range(bm.rows):
            row = bm.buffer[yy * bm.pitch:(yy + 1) * bm.pitch]
            for xx in range(bm.width):
                if (row[xx >> 3] >> (7 - (xx & 7))) & 1:
                    px = gx + g.bitmap_left + xx
                    py = gy + (args.size - g.bitmap_top) + yy
                    if 0 <= px < atlas.width and 0 <= py < atlas.height:
                        draw.point((px, py), fill=255)
        glyphs.append({
            "cp": cp,
            "x": gx,
            "y": gy,
            "w": bm.width,
            "h": bm.rows,
            "bitmap_left": g.bitmap_left,
            "bitmap_top": g.bitmap_top,
            "advance": g.advance.x >> 6,
        })

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(out.with_suffix(".png"))
    out.with_suffix(".json").write_text(json.dumps({
        "font": str(Path(args.font).resolve()),
        "native_px": args.size,
        "cell_w": cell_w,
        "cell_h": cell_h,
        "columns": cols,
        "rows": rows,
        "glyphs": glyphs,
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"{out.with_suffix('.png')}: {len(cps)} glyphs, {atlas.width}x{atlas.height}")
    print(f"{out.with_suffix('.json')}: glyph metrics")


if __name__ == "__main__":
    main()
