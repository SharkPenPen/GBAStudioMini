r"""Probe a Mode 0 GBA frame: palette/tiles/map -> PNG.

Start mGBA with the GDB stub, then run this once:
  toolchain\mgba\mGBA.exe -g build\gba\game.gba
  python tools\gba-probe-mode0.py --out build\gba\screen-mode0.png
"""
import argparse
import os
import socket
import sys
import time

HOST, PORT = "127.0.0.1", 2345
MARKER_ADDR = 0x0203FFF0
TICKS_ADDR = 0x0203FFF4
PAL_ADDR = 0x05000000
OBJ_PAL_ADDR = 0x05000200
VRAM_ADDR = 0x06000000
OBJ_VRAM_ADDR = 0x06010000
MAP_ADDR = 0x0600E000
OAM_ADDR = 0x07000000
W, H = 240, 160


def packet(data: str) -> bytes:
    return b"$" + data.encode() + b"#" + f"{sum(data.encode()) & 0xFF:02x}".encode()


class RSP:
    def __init__(self, timeout: float = 10.0):
        self.s = socket.create_connection((HOST, PORT), timeout=8)
        self.s.settimeout(timeout)

    def _reply(self) -> str:
        out = b""
        while True:
            ch = self.s.recv(1)
            if not ch:
                raise RuntimeError("mGBA closed the connection")
            if ch == b"$":
                break
        while True:
            ch = self.s.recv(1)
            if ch == b"#":
                break
            out += ch
        self.s.recv(2)
        self.s.sendall(b"+")
        return out.decode(errors="replace")

    def cmd(self, data: str) -> str:
        self.s.sendall(packet(data))
        return self._reply()

    def resume(self):
        self.s.sendall(packet("c"))

    def interrupt(self) -> str:
        self.s.sendall(b"\x03")
        return self._reply()

    def read(self, addr: int, n: int) -> bytes:
        out = bytearray()
        while n > 0:
            k = min(n, 512)
            h = self.cmd(f"m{addr:x},{k:x}")
            if h.startswith("E"):
                raise RuntimeError(f"read failed at 0x{addr:x}: {h}")
            out += bytes.fromhex(h)
            addr += k
            n -= k
        return bytes(out)


def rgb15(v: int):
    return ((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31,
            ((v >> 10) & 31) * 255 // 31)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="build/gba/screen-mode0.png")
    ap.add_argument("--run-seconds", type=float, default=2.0)
    args = ap.parse_args()

    try:
        r = RSP()
    except OSError as e:
        print(f"connect failed: {e}", file=sys.stderr)
        print("start: toolchain\\mgba\\mGBA.exe -g build\\gba\\game.gba", file=sys.stderr)
        return 1

    r.cmd("?")
    r.resume()
    time.sleep(args.run_seconds)
    r.interrupt()

    marker = int.from_bytes(r.read(MARKER_ADDR, 4), "little")
    ticks = int.from_bytes(r.read(TICKS_ADDR, 4), "little")
    dispcnt = int.from_bytes(r.read(0x04000000, 2), "little")
    bg0cnt = int.from_bytes(r.read(0x04000008, 2), "little")
    cnt = []
    for a in (0x0400000A, 0x0400000C, 0x0400000E):
        cnt.append(int.from_bytes(r.read(a, 2), "little"))
    print("BG0CNT =", hex(bg0cnt), " BG1/2/3CNT =", [hex(v) for v in cnt])
    print(f"DISPCNT=0x{dispcnt:04X}")

    pal_raw = r.read(PAL_ADDR, 512)
    pal = [int.from_bytes(pal_raw[i:i + 2], "little") for i in range(0, 512, 2)]
    tiles = r.read(VRAM_ADDR, 4096)

    def map_size(bits):
        return {0: (32, 32), 1: (64, 32), 2: (32, 64), 3: (64, 64)}[bits]

    layers = []
    # BG0 HUD：32x32，固定，palette bank 3
    b0base = (bg0cnt >> 8) & 0x1F
    raw0 = r.read(0x06000000 + b0base * 0x800, 32 * 32 * 2)
    ent0 = [int.from_bytes(raw0[i:i + 2], "little") for i in range(0, len(raw0), 2)]
    layers.append((ent0, 32, 32, 0, 0))
    for idx, c in enumerate(cnt):
        base = (c >> 8) & 0x1F
        bits = (c >> 14) & 3
        mw, mh = map_size(bits)
        raw = r.read(0x06000000 + base * 0x800, mw * mh * 2)
        ent = [int.from_bytes(raw[i:i + 2], "little") for i in range(0, len(raw), 2)]
        # 出生点相机在 (0,0)：水平偏移 0，垂直偏移 = 地图高 - 20 行。
        ho = 0
        vo = max(0, (mh - 20) * 8)
        layers.append((ent, mw, mh, ho, vo))

    opal_raw = r.read(OBJ_PAL_ADDR, 64)
    opal = [int.from_bytes(opal_raw[i:i + 2], "little") for i in range(0, 64, 2)]
    otiles = r.read(OBJ_VRAM_ADDR, 4096)
    oam_raw = r.read(OAM_ADDR, 32)
    oam = [int.from_bytes(oam_raw[i:i + 2], "little") for i in range(0, 32, 2)]
    print("OAM0:", [hex(v) for v in oam])
    print("OBJTILES:", [hex(v) for v in otiles[:16]])

    try:
        from PIL import Image
    except ImportError:
        print("Pillow not available", file=sys.stderr)
        return 2

    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        for x in range(W):
            color = (0, 0, 0)
            for ent, mw, mh, ho, vo in layers:
                tx = (x + ho) & 7
                ty = (y + vo) & 7
                mx = ((x + ho) // 8) % mw
                my = ((y + vo) // 8) % mh
                e = ent[my * mw + mx]
                tile = e & 0x3FF
                bank = (e >> 12) & 0xF
                off = tile * 32 + ty * 4 + tx // 2
                if off >= len(tiles):
                    continue
                b = tiles[off]
                idx = (b & 0x0F) if (tx % 2) == 0 else (b >> 4)
                if idx == 0:
                    continue
                color = rgb15(pal[bank * 16 + idx])
                break
            px[x, y] = color

    # 覆盖所有 16x16、4bpp 精灵
    for slot in range(8):
        o = slot * 4
        if o + 3 >= len(oam):
            break
        a0, a1, a2 = oam[o], oam[o + 1], oam[o + 2]
        if a0 & 0x0200:  # disabled
            continue
        if ((a0 >> 14) & 3) != 0 or ((a1 >> 14) & 3) != 1:
            continue
        oy = a0 & 0xFF
        ox = a1 & 0x1FF
        tbase = a2 & 0x3FF
        pbank = (a2 >> 12) & 0xF
        for py in range(16):
            for pxx in range(16):
                sx2 = ox + pxx
                sy2 = oy + py
                if sx2 < 0 or sx2 >= W or sy2 < 0 or sy2 >= H:
                    continue
                tile = tbase + (py // 8) * 2 + (pxx // 8)
                off = tile * 32 + (py % 8) * 4 + (pxx % 8) // 2
                if off >= len(otiles):
                    continue
                b = otiles[off]
                idx = (b & 0x0F) if (pxx % 2) == 0 else (b >> 4)
                if idx == 0:
                    continue
                px[sx2, sy2] = rgb15(opal[pbank * 16 + idx])

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.save(out)
    print(f"saved {out}")
    print("palette[0:8] =", [hex(v) for v in pal[:8]])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
