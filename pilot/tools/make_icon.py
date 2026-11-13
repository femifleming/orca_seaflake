# Generates the Pilot Sealflake app icon (snowflake on deep ice gradient)
import math, sys
from PIL import Image, ImageDraw, ImageFilter
S = 1024
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
bg = Image.new("RGBA", (S, S))
px = bg.load()
for y in range(S):
    for x in range(S):
        dx, dy = (x - S * 0.5) / S, (y - S * 0.42) / S
        r = math.sqrt(dx * dx + dy * dy)
        t = min(1.0, r * 1.6)
        px[x, y] = (int(18 + 10 * (1 - t)), int(30 + 40 * (1 - t)), int(52 + 70 * (1 - t)), 255)
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=220, fill=255)
img.paste(bg, (0, 0), mask)

flake = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(flake)
cx, cy, R = S / 2, S / 2, 330
col = (215, 245, 255, 255)
w = 34
for k in range(6):
    a = k * math.pi / 3 - math.pi / 2
    ex, ey = cx + R * math.cos(a), cy + R * math.sin(a)
    d.line([cx, cy, ex, ey], fill=col, width=w)
    for frac, blen in ((0.45, 120), (0.72, 85)):
        bx, by = cx + R * frac * math.cos(a), cy + R * frac * math.sin(a)
        for s in (-1, 1):
            b = a + s * math.pi / 4
            d.line([bx, by, bx + blen * math.cos(b), by + blen * math.sin(b)], fill=col, width=w - 6)
    d.ellipse([ex - 22, ey - 22, ex + 22, ey + 22], fill=col)
d.regular_polygon((cx, cy, 70), 6, fill=(18, 30, 52, 255), outline=col, width=w - 8)
glow = flake.filter(ImageFilter.GaussianBlur(28))
img = Image.alpha_composite(img, glow)
img = Image.alpha_composite(img, flake)
img.save(sys.argv[1])
