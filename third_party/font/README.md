# Bundled game bitmap font sources

These font files are **only** source material for generating in-game bitmap
glyph atlases. They are not used by the editor GUI and must not replace the
editor's system UI font.

- Editor GUI keeps using the system CJK font (Microsoft YaHei / fallback).
- GBA/PC runtime should consume generated 1-bit/8-bit bitmap atlases, not the
  original TTF/BDF files.
- Do not embed the original font files in a GBA ROM.
- Keep the license and source files next to each font. Bitmap conversion,
  subsetting and other modifications remain under the original font license.

## Native sizes

Only native sizes are listed. The generator must not scale a font to another
size; a font at 12px is not a 14/16px font.

| id | native px | format | license | role |
|---|---:|---|---|---|
| changban12 | 12 | BDF | OFL-1.1 | Simplified Chinese, Song（保留；当前 12px UI 改用 Firefly 渲染） |
| firefly14 | 14 | EBDT in TTF | Arphic Public License | Simplified Chinese, Song；也用于生成 12px UI 面 |
| zhengge16 | 16 | BDF | OFL-1.1 | Simplified Chinese, Hei |

The font files are original upstream files. The catalog repository
(https://github.com/legendtired/chinese-bitmap-specimens) was used only for
research and license/provenance checks; it is not a font redistribution source.


## Generate a game atlas

BDF fonts:

```sh
python tools/make-bitmap-font.py \
  --font third_party/font/changban12/font.bdf \
  --size 12 \
  --charset text --text "视野无限广 箱子 主角" \
  --out build/font-atlas/changban12/text
```

Embedded-bitmap TTF (Firefly 14):

```sh
pip install freetype-py
PYTHONPATH=build/pydeps python tools/make-bitmap-font-ft.py \
  --font third_party/font/firefly14/font.ttf \
  --size 14 \
  --text "视野无限广 箱子 主角" \
  --out build/font-atlas/firefly14/text
```

Both tools reject a size that is not native. They output a 1-bit PNG atlas and
JSON glyph metrics for the game runtime. They do not touch editor GUI fonts.

