#!/bin/sh
# Renders the round 1.8" knob screen's pages to sim/out/round-*.png, masked to the round panel.
#   BOARD=knob18 sh sim/build.sh && sh sim/render_round.sh
set -e
cd "$(dirname "$0")"
mkdir -p out
for pg in now battery solar money today settings; do ./build/render_round day $pg out/round-$pg.raw; done
./build/render_round night now out/round-night.raw
./build/render_round night battery out/round-night-battery.raw
SIM_LIGHT=1 ./build/render_round day now out/round-light.raw
SIM_PALETTE=1 ./build/render_round day now out/round-classic.raw
SIM_AP=1 ./build/render_round setup now out/round-setup.raw
SIM_TOU=plan SIM_FIT=1 ./build/render_round day money out/round-money-tou.raw
python3 - <<'PY'
import glob
from PIL import Image, ImageDraw
for f in glob.glob('out/round-*.raw'):
    d = open(f, 'rb').read()
    im = Image.new('RGB', (360, 360))
    px = im.load()
    for i in range(360 * 360):
        v = d[2 * i] | d[2 * i + 1] << 8
        px[i % 360, i // 360] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    # the panel is round: show it in a dark bezel
    out = Image.new('RGB', (400, 400), (20, 20, 24))
    mask = Image.new('L', (360, 360), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, 359, 359), fill=255)
    out.paste(im, (20, 20), mask)
    out.save(f[:-4] + '.png')
print('rendered', len(glob.glob('out/round-*.png')), 'round screens')
PY
# README picture: the six pages, two rows of three
python3 - <<'PY'
from PIL import Image
names = ['now', 'battery', 'solar', 'money', 'today', 'settings']
sheet = Image.new('RGB', (1200, 800), (20, 20, 24))
for i, n in enumerate(names):
    sheet.paste(Image.open('out/round-%s.png' % n), (400 * (i % 3), 400 * (i // 3)))
sheet.save('../docs/images/screen-knob18.png', optimize=True)
print('wrote docs/images/screen-knob18.png')
PY
