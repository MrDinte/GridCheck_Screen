#!/usr/bin/env python3
"""Renders the README screenshots from the real screen code (needs sim/build/render and Pillow).

    sh sim/build.sh && python3 sim/make_readme_images.py

Writes docs/images/screen-7b*.png. The clock is pinned to a weekday late morning and
the prices to a typical time-of-use plan so the pictures are repeatable.
"""
import datetime
import os
import subprocess

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'docs', 'images')
W, H = 1024, 600
# Thursday 12 Nov 2026, 11:42 in Sydney (AEDT, UTC+11). The sim runs on AEST-10, so use +10.
WHEN = int(datetime.datetime(2026, 11, 12, 11, 42, tzinfo=datetime.timezone(datetime.timedelta(hours=10))).timestamp())


def render(scene, page, **env):
    e = dict(os.environ, SIM_TIME=str(WHEN), SIM_TOU='plan')
    e.update({k: str(v) for k, v in env.items()})
    raw = os.path.join(HERE, 'out', 'readme.raw')
    os.makedirs(os.path.dirname(raw), exist_ok=True)
    subprocess.run([os.path.join(HERE, 'build', 'render'), scene, page, raw], env=e, check=True)
    d = open(raw, 'rb').read()
    px = bytearray(W * H * 3)
    for i in range(W * H):
        v = d[2 * i] | d[2 * i + 1] << 8
        px[3 * i] = ((v >> 11) & 31) * 255 // 31
        px[3 * i + 1] = ((v >> 5) & 63) * 255 // 63
        px[3 * i + 2] = (v & 31) * 255 // 31
    return Image.frombytes('RGB', (W, H), bytes(px))


def grid(images, cols, scale, gap=16, bg=(24, 21, 27)):
    w, h = int(W * scale), int(H * scale)
    rows = (len(images) + cols - 1) // cols
    out = Image.new('RGB', (cols * w + (cols + 1) * gap, rows * h + (rows + 1) * gap), bg)
    for i, im in enumerate(images):
        r, c = divmod(i, cols)
        out.paste(im.resize((w, h), Image.LANCZOS), (gap + c * (w + gap), gap + r * (h + gap)))
    return out


def save(im, name):
    path = os.path.join(OUT, name)
    im.save(path, optimize=True)
    print('wrote', os.path.relpath(path))


# "a newer version" for the update pictures: the next patch number after this one
import re
_ver = re.search(r'EH_VERSION=\\"([0-9.]+)', open(os.path.join(HERE, '..', 'platformio.ini')).read()).group(1).split('.')
NEXT = '.'.join(_ver[:-1] + [str(int(_ver[-1]) + 1)])

os.makedirs(OUT, exist_ok=True)

# Main picture: the Now page.
save(render('day', 'now'), 'screen-7b.png')

# All eight pages.
pages = ['now', 'today', 'solar', 'money', 'flow', 'battery', 'history', 'info']
save(grid([render('day', p) for p in pages], 2, 0.5), 'screen-7b-pages.png')

# Dark vs light, two pages each.
save(grid([render('day', 'now'), render('day', 'now', SIM_LIGHT=1),
           render('day', 'battery'), render('day', 'battery', SIM_LIGHT=1)], 2, 0.5), 'screen-7b-light-dark.png')

# Colour palettes: dark and light for each.
pal = []
for p in range(3):
    pal += [render('day', 'now', SIM_PALETTE=p), render('day', 'now', SIM_PALETTE=p, SIM_LIGHT=1)]
save(grid(pal, 2, 0.5), 'screen-7b-palettes.png')

# Updates: a newer version is available (red dot, "tap to install"), and installing.
avail = render('day', 'info', SIM_OTA=NEXT)
installing = render('day', 'info', SIM_OTA=NEXT, SIM_OTA_PCT=42)
zoom = avail.crop((824, 520, 1024, 600)).resize((400, 160), Image.LANCZOS)
up = grid([avail, installing], 1, 0.75)
canvas = Image.new('RGB', (up.width, up.height + 160 + 16), (24, 21, 27))
canvas.paste(up, (0, 0))
canvas.paste(zoom, ((up.width - 400) // 2, up.height))
save(canvas, 'screen-7b-update.png')
