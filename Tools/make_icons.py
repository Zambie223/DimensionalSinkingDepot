"""Draws the Dimensional Sinking Depot artwork: a violet isometric depot cube whose top face is an orange vortex,
with an arrow sinking into it. Original drawing with Pillow, no game or external art.

Writes:
  Branding/IconDesc_SinkingDepot_512.png, _256.png  - transparent build-menu / unlock icons (imported by create_assets.py)
  Resources/Icon128.png                             - SML Mods menu icon (fixed path and size; ships with the mod)
  Branding/Icon512.png                              - the same logo at 512 px for the future SMR page (not shipped)

Run: py -3 Tools/make_icons.py [--preview <dir>]
"""
import argparse
import math
import os

from PIL import Image, ImageDraw, ImageFilter

SS = 4  # supersampling factor
BASE = 512
N = BASE * SS

# Violet body after the Dimensional Depot; orange after the AWESOME Sink.
CUBE_TOP = (120, 84, 196)
CUBE_LEFT = (104, 62, 186)
CUBE_RIGHT = (70, 40, 138)
EDGE = (28, 14, 52)
RIM = (176, 132, 255)
ORANGE_HOT = (255, 236, 160)
ORANGE = (255, 146, 34)
ORANGE_DEEP = (214, 72, 8)
VORTEX_DARK = (40, 14, 6)
WHITE = (250, 250, 250)
BG_TOP = (46, 52, 62)
BG_BOTTOM = (22, 25, 30)
BORDER = (70, 78, 92)


def s(v):
    return v * SS


def lerp(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def iso_points(cx, cy, r):
    dx = r * math.cos(math.radians(30))
    return {
        "top": (cx, cy - r),
        "ul": (cx - dx, cy - r / 2),
        "ur": (cx + dx, cy - r / 2),
        "c": (cx, cy),
        "ll": (cx - dx, cy + r / 2),
        "lr": (cx + dx, cy + r / 2),
        "bottom": (cx, cy + r),
    }


def scaled(pts):
    return [(s(x), s(y)) for x, y in pts]


def draw_cube(layer, cx, cy, r):
    d = ImageDraw.Draw(layer)
    p = iso_points(cx, cy, r)
    d.polygon(scaled([p["top"], p["ur"], p["c"], p["ul"]]), fill=CUBE_TOP)
    d.polygon(scaled([p["ul"], p["c"], p["bottom"], p["ll"]]), fill=CUBE_LEFT)
    d.polygon(scaled([p["c"], p["ur"], p["lr"], p["bottom"]]), fill=CUBE_RIGHT)
    # Panel lines on the side faces, a nod to the depot's plated look.
    for t in (0.33, 0.66):
        a = lerp_pt(p["ul"], p["ll"], t)
        b = lerp_pt(p["c"], p["bottom"], t)
        c = lerp_pt(p["ur"], p["lr"], t)
        d.line(scaled([a, b, c]), fill=EDGE, width=s(3))
    return p


def lerp_pt(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def draw_edges(layer, p, width):
    d = ImageDraw.Draw(layer)
    w = s(width)
    d.line(scaled([p["top"], p["ur"], p["lr"], p["bottom"], p["ll"], p["ul"], p["top"]]), fill=EDGE, width=w, joint="curve")
    d.line(scaled([p["ul"], p["c"], p["ur"]]), fill=EDGE, width=w, joint="curve")
    d.line(scaled([p["c"], p["bottom"]]), fill=EDGE, width=w)
    # Light rim on the top edges, as if lit by the vortex.
    d.line(scaled([p["ul"], p["top"], p["ur"]]), fill=RIM, width=s(max(2, width // 2)), joint="curve")


def top_face_mask(p, inset):
    """Mask of the cube's top rhombus, shrunk towards its centre by `inset` (0..1)."""
    cx, cy = (p["top"][0] + p["c"][0]) / 2, (p["top"][1] + p["c"][1]) / 2
    pts = [lerp_pt(q, (cx, cy), inset) for q in (p["top"], p["ur"], p["c"], p["ul"])]
    mask = Image.new("L", (N, N), 0)
    ImageDraw.Draw(mask).polygon(scaled(pts), fill=255)
    return mask, (cx, cy)


def draw_vortex(p):
    """Orange spiral sinking into the top face, clipped to it, plus a soft glow layer."""
    mask, (cx, cy) = top_face_mask(p, 0.12)
    dx = p["ur"][0] - cx  # half-width of the rhombus
    dy = cy - p["top"][1]  # half-height of the rhombus
    a, b = dx * 0.86, dy * 0.86  # ellipse radii (isometric circle)

    vortex = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(vortex)
    # Base: dark pit fading to orange at the rim.
    rings = 48
    for i in range(rings, 0, -1):
        t = i / rings
        col = lerp(VORTEX_DARK, ORANGE_DEEP, t ** 0.8) if t > 0.15 else VORTEX_DARK
        d.ellipse((s(cx - a * t), s(cy - b * t), s(cx + a * t), s(cy + b * t)), fill=col + (255,))
    # Spiral arms.
    arms = 3
    for arm in range(arms):
        phase = arm * 2 * math.pi / arms
        steps = 900
        for k in range(steps + 1):
            u = k / steps  # 0 at the rim, 1 at the centre
            radius = (1.0 - u) ** 0.85
            ang = phase + u * 1.35 * math.pi
            x = cx + a * radius * math.cos(ang)
            y = cy + b * radius * math.sin(ang)
            # Round brush stamps give a smooth tapered stroke (wide line segments leave jagged joints).
            half = max(0.6, 15 * (1 - u) ** 1.1 + 0.5)
            col = lerp(ORANGE, ORANGE_HOT, min(1.0, u * 1.6))
            d.ellipse((s(x - half), s(y - half * b / a), s(x + half), s(y + half * b / a)), fill=col + (255,))
    # Hot core.
    core = 0.16
    d.ellipse((s(cx - a * core), s(cy - b * core), s(cx + a * core), s(cy + b * core)), fill=ORANGE_HOT + (255,))

    clipped = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    clipped.paste(vortex, (0, 0), mask)
    glow = Image.new("RGBA", (N, N), ORANGE + (0,))
    glow.putalpha(mask.filter(ImageFilter.GaussianBlur(s(18))).point(lambda v: int(v * 0.85)))
    return clipped, glow, (cx, cy), (a, b)


def draw_arrow(layer, cx, tip_y, top_y, shaft_w, head_w, head_h, color):
    """Downward arrow: shaft from top_y down to the head, tip at tip_y."""
    d = ImageDraw.Draw(layer)
    d.rectangle((s(cx - shaft_w / 2), s(top_y), s(cx + shaft_w / 2), s(tip_y - head_h + 2)), fill=color)
    d.polygon([(s(cx), s(tip_y)), (s(cx - head_w / 2), s(tip_y - head_h)), (s(cx + head_w / 2), s(tip_y - head_h))], fill=color)


def render_object():
    """The depot with its vortex and arrow on a transparent canvas, at N x N."""
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    cube_layer = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    p = draw_cube(cube_layer, 256, 300, 196)

    shadow = cube_layer.split()[3].filter(ImageFilter.GaussianBlur(s(10)))
    img.paste((0, 0, 0, 120), (s(6), s(14)), shadow)

    vortex, glow, (vx, vy), (a, b) = draw_vortex(p)
    img.alpha_composite(glow, (0, -s(10)))
    img.alpha_composite(cube_layer)
    img.alpha_composite(vortex)
    edges = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    draw_edges(edges, p, 7)
    img.alpha_composite(edges)

    arrow_layer = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    draw_arrow(arrow_layer, vx, vy - b * 0.05, 18, 44, 118, 78, WHITE)
    arrow_shadow = arrow_layer.split()[3].filter(ImageFilter.GaussianBlur(s(7)))
    img.paste((40, 10, 0, 150), (s(3), s(8)), arrow_shadow)
    img.alpha_composite(arrow_layer)
    return img


def rounded_background():
    grad = Image.new("RGB", (N, N))
    d = ImageDraw.Draw(grad)
    for y in range(N):
        d.line([(0, y), (N, y)], fill=lerp(BG_TOP, BG_BOTTOM, y / (N - 1)))
    mask = Image.new("L", (N, N), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, N - 1, N - 1), radius=s(96), fill=255)
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    img.paste(grad, (0, 0), mask)
    ImageDraw.Draw(img).rounded_rectangle((s(6), s(6), N - s(6), N - s(6)), radius=s(90), outline=BORDER, width=s(6))
    return img


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", help="also write previews (icons on light and dark backgrounds) to this folder")
    args = parser.parse_args()

    mod_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    branding = os.path.join(mod_dir, "Branding")
    resources = os.path.join(mod_dir, "Resources")
    os.makedirs(branding, exist_ok=True)
    os.makedirs(resources, exist_ok=True)

    obj = render_object().resize((BASE, BASE), Image.LANCZOS)
    obj.save(os.path.join(branding, "IconDesc_SinkingDepot_512.png"))
    obj.resize((256, 256), Image.LANCZOS).save(os.path.join(branding, "IconDesc_SinkingDepot_256.png"))

    # Mod logo: the same drawing, slightly smaller, on the rounded background used by the author's other mods.
    logo = rounded_background()
    inner = render_object().resize((int(N * 0.86), int(N * 0.86)), Image.LANCZOS)
    logo.alpha_composite(inner, ((N - inner.width) // 2, (N - inner.height) // 2 + s(6)))
    logo = logo.resize((BASE, BASE), Image.LANCZOS)
    logo.save(os.path.join(branding, "Icon512.png"))
    logo.resize((128, 128), Image.LANCZOS).save(os.path.join(resources, "Icon128.png"))

    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        sheet = Image.new("RGBA", (512 + 256 + 128 + 64 + 128 + 5 * 24, 2 * 512 + 3 * 24), (0, 0, 0, 255))
        for row, bg in enumerate([(32, 36, 44, 255), (205, 205, 205, 255)]):
            y = 24 + row * (512 + 24)
            ImageDraw.Draw(sheet).rectangle((0, y - 12, sheet.width, y + 512 + 12), fill=bg)
            x = 24
            for size in (512, 256, 128, 64):
                sheet.alpha_composite(obj.resize((size, size), Image.LANCZOS), (x, y))
                x += size + 24
            sheet.alpha_composite(logo.resize((128, 128), Image.LANCZOS), (x, y))
        sheet.convert("RGB").save(os.path.join(args.preview, "icons_preview.png"))
        print("preview:", os.path.join(args.preview, "icons_preview.png"))
    print("wrote icons to", branding, "and", resources)


main()
