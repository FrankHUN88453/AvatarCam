# AvatarCam ikon: sötét színátmenetes lap, rajta fehér "webkamera-fej" két hegyes füllel és cián lencsével,
# a sarokban piros élő/felvétel pont. Kimenet: src/app.ico (16-256 px) és docs/icon.png (512 px).
# Futtatás: python tools/make_icon.py
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 1024  # rajzolási méret (utána kicsinyítünk)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def gradient(size, c0, c1):
    g = Image.new("RGBA", (size, size))
    px = g.load()
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2 * (size - 1))
            px[x, y] = lerp(c0, c1, t) + (255,)
    return g


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=255)
    return m


def draw_icon():
    small = 256
    bg = gradient(small, (26, 22, 64), (12, 96, 138)).resize((S, S), Image.BICUBIC)
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    img.paste(bg, (0, 0), rounded_mask(S, int(S * 0.22)))

    # finom cián fény a fej mögött
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((S * 0.18, S * 0.22, S * 0.82, S * 0.86), fill=(89, 217, 255, 90))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.07))
    img = Image.alpha_composite(img, glow)

    d = ImageDraw.Draw(img)
    white = (244, 247, 252, 255)
    cx, cy, r = S * 0.5, S * 0.58, S * 0.30
    # fülek (hegyes háromszögek), a fej mögött
    ear_l = [(cx - r * 0.95, cy - r * 0.35), (cx - r * 0.78, cy - r * 1.55), (cx - r * 0.15, cy - r * 0.85)]
    ear_r = [(cx + r * 0.95, cy - r * 0.35), (cx + r * 0.78, cy - r * 1.55), (cx + r * 0.15, cy - r * 0.85)]
    d.polygon(ear_l, fill=white)
    d.polygon(ear_r, fill=white)
    # belső fül (cián)
    inner = (89, 217, 255, 255)

    def shrink(tri, k):
        mx = sum(p[0] for p in tri) / 3
        my = sum(p[1] for p in tri) / 3
        return [(mx + (p[0] - mx) * k, my + (p[1] - my) * k + r * 0.06) for p in tri]

    d.polygon(shrink(ear_l, 0.52), fill=inner)
    d.polygon(shrink(ear_r, 0.52), fill=inner)
    # fej (a kamera teste)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=white)
    # lencse: sötét gyűrű, cián írisz, csillanás
    lr = r * 0.62
    d.ellipse((cx - lr, cy - lr, cx + lr, cy + lr), fill=(20, 24, 48, 255))
    ir = lr * 0.72
    d.ellipse((cx - ir, cy - ir, cx + ir, cy + ir), fill=(46, 170, 230, 255))
    pr = ir * 0.48
    d.ellipse((cx - pr, cy - pr, cx + pr, cy + pr), fill=(14, 30, 70, 255))
    hr = ir * 0.26
    hx, hy = cx - ir * 0.38, cy - ir * 0.40
    d.ellipse((hx - hr, hy - hr, hx + hr, hy + hr), fill=(255, 255, 255, 235))
    # élő / felvétel pont
    rr = S * 0.075
    rx, ry = S * 0.80, S * 0.20
    d.ellipse((rx - rr * 1.35, ry - rr * 1.35, rx + rr * 1.35, ry + rr * 1.35), fill=(26, 22, 64, 255))
    d.ellipse((rx - rr, ry - rr, rx + rr, ry + rr), fill=(255, 64, 88, 255))
    return img


def main():
    big = draw_icon()
    sizes = [256, 128, 64, 48, 40, 32, 24, 20, 16]
    frames = [big.resize((s, s), Image.LANCZOS) for s in sizes]
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    frames[0].save(os.path.join(ROOT, "src", "app.ico"), format="ICO", sizes=[(s, s) for s in sizes], append_images=frames[1:])
    big.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, "docs", "icon.png"))
    # előnézet több méretben (csak ellenőrzéshez)
    prev = Image.new("RGBA", (256 + 128 + 64 + 48 + 32 + 16 + 70, 260), (40, 44, 52, 255))
    x = 10
    for s in (256, 128, 64, 48, 32, 16):
        prev.paste(big.resize((s, s), Image.LANCZOS), (x, 2), big.resize((s, s), Image.LANCZOS))
        x += s + 10
    prev.save(os.path.join(ROOT, "build", "icon_preview.png"))
    print("ok")


if __name__ == "__main__":
    main()
