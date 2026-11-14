#!/bin/sh
# Renders each screen to sim/out/*.png
set -e
cd "$(dirname "$0")"
mkdir -p out
for scene in day night export error setup; do ./build/render $scene now out/$scene.raw; done
./build/render day info out/info.raw
for pg in today solar money money30 flow flow30 battery history history7 history30; do ./build/render day $pg out/p-$pg.raw; done
SIM_AP=1 ./build/render setup now out/setup-ap.raw
python3 - <<'PY'
import glob, struct
from PIL import Image
for f in glob.glob('out/*.raw'):
    d = open(f, 'rb').read()
    im = Image.new('RGB', (1024, 600))
    px = im.load()
    for i in range(1024 * 600):
        v = d[2 * i] | d[2 * i + 1] << 8
        px[i % 1024, i // 1024] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    im.save(f[:-4] + '.png')
print('rendered', len(glob.glob('out/*.png')), 'screens')
PY
