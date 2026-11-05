from PIL import Image, ImageDraw
import os

OUT = "build/bee_assets"
os.makedirs(OUT, exist_ok=True)

def save(name, img):
    img.save(f"{OUT}/{name}")

# 16x16 grass tile
g = Image.new("RGBA", (16, 16), (70, 120, 70, 255))
d = ImageDraw.Draw(g)
d.rectangle([0, 0, 15, 15], outline=(50, 95, 55, 255))
d.line([(4, 4), (4, 7)], fill=(95, 150, 85, 255))
d.line([(11, 10), (11, 13)], fill=(95, 150, 85, 255))
save("grass.png", g)

# player: 16x16 blue gardener
p = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
d = ImageDraw.Draw(p)
d.ellipse([4, 1, 11, 8], fill=(245, 205, 160, 255))
d.rectangle([3, 5, 12, 8], fill=(70, 130, 200, 255))
d.rectangle([5, 9, 10, 14], fill=(40, 80, 150, 255))
d.rectangle([3, 9, 4, 12], fill=(245, 205, 160, 255))
d.rectangle([11, 9, 12, 12], fill=(245, 205, 160, 255))
d.rectangle([2, 6, 13, 7], fill=(240, 230, 120, 255))
save("player.png", p)

# bee: 16x16 yellow/black
b = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
d = ImageDraw.Draw(b)
d.ellipse([2, 4, 14, 12], fill=(245, 200, 50, 255))
d.rectangle([5, 4, 7, 12], fill=(30, 30, 30, 255))
d.rectangle([9, 4, 11, 12], fill=(30, 30, 30, 255))
d.ellipse([5, 2, 11, 6], fill=(230, 240, 255, 255))
d.ellipse([6, 6, 8, 8], fill=(20, 20, 20, 255))
d.ellipse([2, 13, 5, 14], fill=(90, 60, 20, 255))
save("bee.png", b)

# sting: 8x8 yellow
s = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
d = ImageDraw.Draw(s)
d.ellipse([1, 1, 6, 6], fill=(255, 230, 80, 255))
d.ellipse([3, 3, 4, 4], fill=(255, 255, 220, 255))
save("sting.png", s)
