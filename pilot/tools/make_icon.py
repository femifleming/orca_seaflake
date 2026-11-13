# App icon: one Septambres Neau glyph (from ~/Library/Fonts), one colour on one colour.
#   python3 pilot/tools/make_icon.py "Pilot Sealflake" pilot/Resources/icon.png
import sys, json
from PIL import Image, ImageDraw, ImageFont
FONT = "/Users/femishonugafleming/Library/Fonts/Septambres Neau.ttf"
S = 1024
ICONS = {  # product: (glyph, letter colour, background)
 "Chloaelis":            ("C", "#d8f3ea", "#0f3d3a"),
 "Chloaeilius":          ("c", "#0f3d3a", "#d8f3ea"),
 "EX Multisample":       ("S", "#141414", "#f2c94c"),
 "EX Poly Wavetable":    ("W", "#141414", "#6fcf97"),
 "EX Poly FM":           ("f", "#141414", "#56ccf2"),
 "EX Poly Exciter":      ("X", "#141414", "#f2994a"),
 "EX Granulator":        ("t", "#141414", "#bb6bd9"),
 "EX Multi-FX":          ("x", "#141414", "#eb5757"),
 "EX Resonator":         ("r", "#141414", "#2dd4bf"),
 "EX Spectral Freeze":   ("z", "#141414", "#a5b4fc"),
 "EX Macro Oscillator 2":("A", "#141414", "#e0e0e0"),
 "ConvoSolution":        ("v", "#ffff00", "#0000aa"),
 "Cloudfin":             ("D", "#ffffff", "#29a8e0"),
 "openprocess":          ("e", "#ff5a1f", "#f4efe4"),
 "opensphere":           ("h", "#f4efe4", "#ff5a1f"),
 "Quell":                ("Q", "#c9ff3d", "#1d1d24"),
 "Partials":             ("a", "#e9e9e9", "#141414"),
 "Nabra":                ("n", "#f5ead6", "#8a3b1e"),
 "Leitner":              ("t", "#111111", "#e6e6e6"),
 "Pilot Sealflake":      ("s", "#bfe9ff", "#12233a"),
 "Orca Seaflake":        ("E", "#72dec2", "#000000"),
 "ObliqSound":           ("q", "#1f3a2c", "#c8d6a0"),
 "ObliqSonic":           ("c", "#c8d6a0", "#1f3a2c"),
 "Pinkie Pies Balloon Shop":   ("b", "#ffffff", "#ff6fb5"),
 "Pinkie Pies Balloon Palace": ("a", "#ff6fb5", "#fff0f7"),
 "AERIA Flasher":        ("A", "#ffffff", "#4a5d73"),
}

def icon(name, size=S, full=False):
    g, fg, bg = ICONS[name]
    k = 4                                    # draw big, scale down: smooth edges
    W = size * k
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    m = 0 if full else round(W * 100 / 1024)  # macOS grid: 824 of 1024, radius ~185
    d.rounded_rectangle([m, m, W - m - 1, W - m - 1], radius=0 if full else round(W * 185 / 1024), fill=bg)
    font = ImageFont.truetype(FONT, round(W * 0.62))
    box = d.textbbox((0, 0), g, font=font)
    gw, gh = box[2] - box[0], box[3] - box[1]
    scale = (W - 2 * m) * 0.44 / max(gw, gh)    # every glyph fills the same share of the tile
    font = ImageFont.truetype(FONT, round(W * 0.62 * scale))
    box = d.textbbox((0, 0), g, font=font)
    gw, gh = box[2] - box[0], box[3] - box[1]
    d.text(((W - gw) / 2 - box[0], (W - gh) / 2 - box[1]), g, font=font, fill=fg)
    return img.resize((size, size), Image.LANCZOS)

if __name__ == "__main__":
    if sys.argv[1] == "sheet":
        names = list(ICONS)
        cols = 7; t = 200
        sheet = Image.new("RGBA", (cols * t, ((len(names) + cols - 1) // cols) * (t + 30)), "#ffffff")
        dr = ImageDraw.Draw(sheet)
        for i, n in enumerate(names):
            x, y = (i % cols) * t, (i // cols) * (t + 30)
            sheet.paste(icon(n, 180), (x + 10, y + 5), icon(n, 180))
            dr.text((x + 10, y + 188), n[:28], fill="#333")
        sheet.save(sys.argv[2])
    else:   # name out.png [size] [full]
        icon(sys.argv[1], int(sys.argv[3]) if len(sys.argv) > 3 else S, len(sys.argv) > 4).save(sys.argv[2])
