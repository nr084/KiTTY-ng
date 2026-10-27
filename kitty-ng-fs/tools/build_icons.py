#!/usr/bin/env python3
"""Render the original SVG icon system and write multiresolution Windows ICOs.

Build-time dependencies: Python 3, Pillow, Inkscape. No extra dependency is
required when using the compiled WFX plugin. PNGs are intermediate renderings;
the source of truth is the SVG files in assets/.
"""
from io import BytesIO
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
ICONS = ("plugin", "ssh", "settings")


def icon_dib(image):
    """Classic 32-bit BGRA icon frame plus 1-bit transparency mask."""
    width, height = image.size
    image = image.convert("RGBA")
    pixels = bytearray()
    mask = bytearray()
    stride = ((width + 31) // 32) * 4
    for y in range(height - 1, -1, -1):
        row = bytearray(stride)
        for x in range(width):
            red, green, blue, alpha = image.getpixel((x, y))
            pixels.extend((blue, green, red, alpha))
            if not alpha:
                row[x // 8] |= 0x80 >> (x % 8)
        mask.extend(row)
    header = struct.pack("<IIIHHIIIIII", 40, width, height * 2, 1, 32, 0,
                         len(pixels) + len(mask), 0, 0, 0, 0)
    return header + bytes(pixels) + bytes(mask)


def write_ico(path, images):
    frames = []
    for size in SIZES:
        if size < 128:
            frames.append(icon_dib(images[size]))
        else:
            png = BytesIO()
            images[size].save(png, format="PNG", optimize=True)
            frames.append(png.getvalue())
    out = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    offset = 6 + 16 * len(frames)
    for size, frame in zip(SIZES, frames):
        dimension = 0 if size == 256 else size
        out.extend(struct.pack("<BBBBHHII", dimension, dimension, 0, 0,
                               1, 32, len(frame), offset))
        offset += len(frame)
    for frame in frames:
        out.extend(frame)
    path.write_bytes(out)
    # Read every size through an independent ICO decoder before accepting it.
    decoded = Image.open(path)
    assert decoded.ico.sizes() == {(s, s) for s in SIZES}
    for size in SIZES:
        actual = decoded.ico.getimage((size, size)).convert("RGBA")
        assert actual.size == (size, size)
        assert actual.getextrema()[3][0] == 0
        assert actual.getextrema()[3][1] == 255
        assert actual.tobytes() == images[size].convert("RGBA").tobytes()


def render(inkscape, source, size, destination):
    subprocess.run([inkscape, str(source), f"--export-width={size}",
                    f"--export-height={size}", "--export-type=png",
                    f"--export-filename={destination}"], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    with Image.open(destination) as image:
        return image.convert("RGBA")


def font(size, bold=False):
    path = "/usr/share/fonts/truetype/dejavu/DejaVuSans" + ("-Bold" if bold else "") + ".ttf"
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()


def preview(images, folder):
    canvas = Image.new("RGB", (980, 378), "#edf2f8")
    draw = ImageDraw.Draw(canvas)
    draw.text((26, 16), "KiTTY-ng FS 1.0 — native icon system", font=font(21, True), fill="#183349")
    draw.text((26, 48), "Original vector artwork · 16 / 20 / 24 / 32 / 40 / 48 / 64 / 128 / 256 px",
              font=font(13), fill="#536a7e")
    labels = (("plugin", "Plugin"), ("ssh", "SSH connection"),
              ("settings", "Settings"), ("folder", "Group (TC folder)"))
    for top, background, foreground in ((83, "#ffffff", "#244157"),
                                         (229, "#1d2026", "#e4ecf6")):
        draw.rounded_rectangle((16, top, 963, top + 133), radius=12, fill=background)
        for col, (name, title) in enumerate(labels):
            x = 35 + col * 238
            draw.text((x, top + 10), title, font=font(14, True), fill=foreground)
            for size, delta in ((48, 0), (32, 67), (24, 118), (16, 160)):
                icon = folder[size] if name == "folder" else images[name][size]
                canvas.paste(icon, (x + delta, top + 42 + (48 - size) // 2), icon)
                draw.text((x + delta + max(0, (size - 16) // 2), top + 103), str(size),
                          font=font(11), fill=foreground)
    canvas.save(ASSETS / "icons-preview.png", optimize=True)


def main():
    inkscape = shutil.which("inkscape")
    if not inkscape:
        raise SystemExit("Install Inkscape or put inkscape in PATH.")
    images = {}
    with tempfile.TemporaryDirectory(prefix="kittyngfs-icons-") as tmp:
        temporary = Path(tmp)
        for name in ICONS:
            rendered = {}
            for size in SIZES:
                source = ASSETS / f"{name}-small.svg" if size <= 24 and (ASSETS / f"{name}-small.svg").exists() else ASSETS / f"{name}.svg"
                rendered[size] = render(inkscape, source, size, temporary / f"{name}-{size}.png")
            images[name] = rendered
            write_ico(ASSETS / f"{name}.ico", rendered)
            print(f"{name}.ico: verified {len(SIZES)} sizes")
        folder = {size: render(inkscape, ASSETS / "group-preview.svg", size,
                               temporary / f"folder-{size}.png")
                  for size in (16, 24, 32, 48)}
        preview(images, folder)
    print(f"Preview: {ASSETS / 'icons-preview.png'}")


if __name__ == "__main__":
    main()
