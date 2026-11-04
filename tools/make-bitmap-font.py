#!/usr/bin/env python3
"""Generate a 1-bit bitmap atlas from a native BDF font.

This tool never scales a font: the requested size must equal the BDF SIZE.
Output is intended for game text atlases, not for the editor GUI.
"""
import argparse
import json
import math
import os
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def parse_bdf(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
    meta = {}
    glyphs = []
    cur = None
    in_bitmap = False
    bitmap_lines = []

    for raw in text:
        line = raw.rstrip("\r\n")
        if in_bitmap:
            if line.strip() == "ENDCHAR":
                if cur is not None:
                    glyphs.append(cur)
                cur = None
                in_bitmap = False
                bitmap_lines = []
            elif line.strip():
                bitmap_lines.append(line.strip())
            continue
        parts = line.split()
        if not parts:
            continue
        key = parts[0]
        if key in ("FONT", "SIZE", "FONTBOUNDINGBOX", "CHARSET_REGISTRY", "CHARSET_ENCODING"):
            meta[key] = parts[1:]
        elif key == "STARTCHAR":
            cur = {"name": " ".join(parts[1:]), "rows": []}
        elif key == "ENCODING" and cur is not None:
            cur["encoding"] = int(parts[1])
        elif key == "DWIDTH" and cur is not None:
            cur["dwidth"] = (int(parts[1]), int(parts[2]))
        elif key == "BBX" and cur is not None:
            cur["bbx"] = tuple(int(x) for x in parts[1:5])
        elif key == "BITMAP":
            in_bitmap = True
            bitmap_lines = []

    if "SIZE" not in meta or "FONTBOUNDINGBOX" not in meta:
        raise SystemExit("BDF missing SIZE/FONTBOUNDINGBOX")
    size = int(meta["SIZE"][0])
    fb = tuple(int(x) for x in meta["FONTBOUNDINGBOX"])
    # decode bitmaps now
    for g in glyphs:
        if "encoding" not in g or "bbx" not in g:
            g["rows"] = []
            continue
        w, h, xoff, yoff = g["bbx"]
        nbytes = (w + 7) // 8
        rows = []
        for line in bitmap_lines:  # not used; placeholder
            pass
    return meta, glyphs


def decode_bitmap(g):
    w, h, xoff, yoff = g["bbx"]
    nbytes = (w + 7) // 8
    hexlines = g.get("_bitmap_hex", [])
    rows = []
    for line in hexlines:
        raw = bytes.fromhex(line)
        bits = []
        for byte in raw:
            for bit in range(7, -1, -1):
                bits.append((byte >> bit) & 1)
        rows.append(bits[:w])
    while len(rows) < h:
        rows.append([0] * w)
    return rows[:h]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int)
    ap.add_argument("--text", default="")
    ap.add_argument("--charset", choices=["ascii", "all", "text"], default="ascii")
    ap.add_argument("--columns", type=int, default=16)
    args = ap.parse_args()

    meta, glyphs = parse_bdf(args.font)
    native = int(meta["SIZE"][0])
    if args.size is not None and args.size != native:
        raise SystemExit(f"native size is {native}px; refusing to scale to {args.size}px")
    fw, fh, fx, fy = (int(x) for x in meta["FONTBOUNDINGBOX"])

    # parse BDF again collecting bitmap lines properly (simple state machine)
    lines = Path(args.font).read_text(encoding="utf-8", errors="replace").splitlines()
    by_cp = {}
    cur = None
    in_bm = False
    bm = []
    for raw in lines:
        line = raw.rstrip("\r\n")
        if in_bm:
            if line.strip() == "ENDCHAR":
                if cur is not None and "encoding" in cur and cur["encoding"] >= 0:
                    cur["_bitmap_hex"] = bm
                    by_cp[cur["encoding"]] = cur
                cur = None
                in_bm = False
                bm = []
            elif line.strip():
                bm.append(line.strip())
            continue
        p = line.split()
        if not p:
            continue
        if p[0] == "STARTCHAR":
            cur = {"name": " ".join(p[1:])}
        elif p[0] == "ENCODING" and cur is not None:
            cur["encoding"] = int(p[1])
        elif p[0] == "DWIDTH" and cur is not None:
            cur["dwidth"] = (int(p[1]), int(p[2]))
        elif p[0] == "BBX" and cur is not None:
            cur["bbx"] = tuple(int(x) for x in p[1:5])
        elif p[0] == "BITMAP":
            in_bm = True
            bm = []

    if args.charset == "ascii":
        cps = list(range(32, 127))
    elif args.charset == "text":
        # unique codepoints preserving order
        cps = []
        for ch in args.text:
            cp = ord(ch)
            if cp not in cps:
                cps.append(cp)
    else:
        cps = sorted(by_cp)

    available = [cp for cp in cps if cp in by_cp]
    if not available:
        raise SystemExit("no requested glyphs found in BDF")

    cols = max(1, args.columns)
    rows = math.ceil(len(available) / cols)
    cell_w = max(1, fw)
    cell_h = max(1, fh)
    atlas = Image.new("L", (cols * cell_w, rows * cell_h), 0)
    draw = ImageDraw.Draw(atlas)
    index = []
    for i, cp in enumerate(available):
        g = by_cp[cp]
        w, h, xoff, yoff = g["bbx"]
        bits = decode_bitmap(g)
        gx = (i % cols) * cell_w
        gy = (i // cols) * cell_h
        # BDF y-up -> PNG y-down. Place glyph box relative to font box.
        dx = xoff - fx
        dy = (fy + fh) - (yoff + h)
        for yy in range(h):
            for xx in range(w):
                if bits[yy][xx]:
                    draw.point((gx + dx + xx, gy + dy + yy), fill=255)
        index.append({
            "cp": cp,
            "x": gx + dx,
            "y": gy + dy,
            "w": w,
            "h": h,
            "cell_x": gx,
            "cell_y": gy,
            "dx": dx,
            "dy": dy,
            "advance": g.get("dwidth", (cell_w, 0))[0],
        })

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(out.with_suffix(".png"))
    meta_out = {
        "font": str(Path(args.font).resolve()),
        "native_px": native,
        "cell_w": cell_w,
        "cell_h": cell_h,
        "columns": cols,
        "rows": rows,
        "glyphs": index,
    }
    out.with_suffix(".json").write_text(json.dumps(meta_out, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"{out.with_suffix('.png')}: {len(available)} glyphs, {atlas.width}x{atlas.height}")
    print(f"{out.with_suffix('.json')}: glyph metrics")


if __name__ == "__main__":
    main()
