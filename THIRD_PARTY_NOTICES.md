# Third-party notices

The project code is licensed under the MIT License (see `LICENSE`).
That license does **not** apply to the third-party files listed here; their
original licenses continue to apply.

## Dear ImGui

- Path: `third_party/imgui`
- License: MIT
- License file: `third_party/imgui/LICENSE.txt`
- Upstream: https://github.com/ocornut/imgui

## miniaudio

- Path: `third_party/audio/miniaudio.h`
- License: public domain / MIT-0 (see the notice inside the header)
- Upstream: https://github.com/mackron/miniaudio

## Game bitmap font sources

These fonts are source material for generating in-game bitmap glyph atlases.
They are not editor GUI fonts and are not embedded as original font files in
games.

- `third_party/font/changban12` — OFL-1.1
- `third_party/font/firefly14` — Arphic Public License
- `third_party/font/zhengge16` — OFL-1.1

Each folder contains its own license, source URL and SHA256. See
`third_party/font/README.md` and `third_party/font/manifest.json`.

The UI now uses generated native 12/14/16 faces:
`engine/render/ui_font12.h`, `ui_font14.h`, `ui_font16.h`. The old 10px Fusion
Pixel atlas was removed; `third_party/font/LICENSE-OFL.txt` still covers the OFL
sources.
