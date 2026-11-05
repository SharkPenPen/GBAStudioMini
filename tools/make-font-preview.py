import os, re
from PIL import Image

ROOT = r"D:\GitHud\GameStudio\GBAStudioMini"
OUT = os.path.join(ROOT, "docs", "font-preview")
os.makedirs(OUT, exist_ok=True)

def load_font(size):
    path = os.path.join(ROOT, "engine", "render", f"ui_font{size}.h")
    text = open(path, "r", encoding="utf-8").read()
    w = int(re.search(rf"kUiFont{size}W = (\d+)", text).group(1))
    h = int(re.search(rf"kUiFont{size}H = (\d+)", text).group(1))
    glyphs = {}
    for m in re.finditer(r"\{0x([0-9A-Fa-f]+), \{([^}]*)\}, (\d+)\}", text):
        code = int(m.group(1), 16)
        rows = [int(x) for x in m.group(2).split(",") if x.strip()]
        adv = int(m.group(3))
        glyphs[code] = (rows, adv)
    return w, h, glyphs

def draw_text(img, font, s, x, y):
    w, h, glyphs = font
    spacing = 1 if w == 12 else 0
    px = img.load()
    for ch in s:
        cp = ord(ch)
        g = glyphs.get(cp)
        if g is None:
            x += 8
            continue
        rows, adv = g
        for ry in range(h):
            bits = rows[ry] if ry < len(rows) else 0
            rx = 0
            while rx < w:
                if (bits & (1 << rx)) == 0:
                    rx += 1
                    continue
                run = 1
                while rx + run < w and (bits & (1 << (rx + run))) != 0:
                    run += 1
                for k in range(run):
                    px[x + rx + k, y + ry] = (255, 255, 255, 255)
                rx += run
        x += adv + spacing
    return x

def text_width(font, s):
    w, h, glyphs = font
    spacing = 1 if w == 12 else 0
    return sum(glyphs.get(ord(ch), (None, 8))[1] + spacing for ch in s)

def make(size, W, H):
    font = load_font(size)
    fw, fh, _ = font
    lines = [
        f"PocketLoom {size}px / {W}x{H} / 1:1",
        "打蜜蜂 / 平台跳跃 / 俯视探索",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "abcdefghijklmnopqrstuvwxyz",
        "0123456789  HP 100/100  EXP 158",
        "永和九年，岁在癸丑，暮春之初。",
    ]
    line_h = fh + 2
    total = line_h * len(lines)
    y0 = max(8, (H - total) // 2)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    for i, line in enumerate(lines):
        draw_text(img, font, line, 16, y0 + i * line_h)
    p = os.path.join(OUT, f"font{size}-{W}x{H}.png")
    img.save(p)
    print(p, text_width(font, lines[0]))

for size in (12, 14, 16):
    make(size, 480, 272)
    make(size, 400, 240)
