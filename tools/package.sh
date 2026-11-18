#!/bin/sh
# Builds the firmware for both screens and writes the files for installing and updating them.
# 7" (ESP32-S3-Touch-LCD-7B) into <out>/, round 1.8" knob (ESP32-S3-Knob-Touch-LCD-1.8) into <out>/knob18/:
#   <out>/gridcheck-7b-<v>-bootloader.bin   \
#   <out>/gridcheck-7b-<v>-partitions.bin    | first install over USB (web flasher
#   <out>/gridcheck-7b-<v>-otadata.bin       | or esptool), see manifest.json
#   <out>/gridcheck-7b-<v>-app.bin          /  also the file for updates over Wi-Fi
#   <out>/manifest.json   read by the web flasher (ESP Web Tools)
#   <out>/ota.json        read by screens looking for updates: {"version","url"}
# <out> defaults to docs/firmware (../web/firmware inside the GridCheck bridge
# project); pass another folder as the first argument.
# The parts are flashed separately (not one merged file) so the saved settings
# at 0x9000 are left alone when installing over an existing GridCheck.
#
# The app is signed with the private key in keys/signing_key.pem (or $SIGNING_KEY).
# Screens only accept updates signed with the key matching src/signing_pub.h.
# Building your own? Make your own key first:
#   python3 tools/sign.py newkey keys/signing_key.pem
#   python3 tools/sign.py header keys/signing_key_pub.pem src/signing_pub.h
set -e
cd "$(dirname "$0")/.."
if [ -d ../web ]; then DEF=../web/firmware; else DEF=docs/firmware; fi
OUT=${1:-$DEF}
VERSION=$(sed -n 's/.*-DEH_VERSION=\\"\([^\\]*\)\\".*/\1/p' platformio.ini | head -1)
: "${PLATFORMIO_CORE_DIR:=$HOME/.platformio}"
KEY=${SIGNING_KEY:-keys/signing_key.pem}
[ -f "$KEY" ] || { echo "No signing key at $KEY (see the top of this script)"; exit 1; }
# the key must match the public key built into the firmware
TMPH=$(mktemp)
python3 - "$KEY" "$TMPH" <<'PYK'
import sys
from cryptography.hazmat.primitives import serialization
k = serialization.load_pem_private_key(open(sys.argv[1], 'rb').read(), None)
open(sys.argv[2], 'wb').write(k.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
PYK
python3 tools/sign.py header "$TMPH" "$TMPH.h" >/dev/null
diff -q "$TMPH.h" src/signing_pub.h >/dev/null || { echo "$KEY doesn't match src/signing_pub.h"; rm -f "$TMPH" "$TMPH.h"; exit 1; }
# package <pio env> <file prefix> <folder> <board name for the web flasher>
package() {
  ENV=$1; PFX=$2; DIR=$3; NAME=$4
  pio run -e "$ENV"
  B=.pio/build/$ENV
  OTADATA=$(find "$B" -name ota_data_initial.bin | head -1)
  [ -n "$OTADATA" ] || { echo "ota_data_initial.bin not found in $B"; exit 1; }
  mkdir -p "$DIR"
  # Keep the previous version's update file (-app.bin) as well: GitHub Pages
  # caches ota.json for a few minutes, so a screen may still ask for it.
  rm -f "$DIR"/$PFX-*-bootloader.bin "$DIR"/$PFX-*-partitions.bin "$DIR"/$PFX-*-otadata.bin
  N="$PFX-$VERSION"
  P="$DIR/$N"
  cp "$B/bootloader.bin" "$P-bootloader.bin"
  cp "$B/partitions.bin" "$P-partitions.bin"
  cp "$OTADATA" "$P-otadata.bin"
  python3 tools/sign.py sign "$KEY" "$B/firmware.bin" "$P-app.bin"
  python3 tools/sign.py verify "$TMPH" "$P-app.bin"
  cat > "$DIR/manifest.json" <<JSON
{
  "name": "GridCheck for $NAME",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "$N-bootloader.bin", "offset": 0 },
        { "path": "$N-partitions.bin", "offset": 32768 },
        { "path": "$N-otadata.bin", "offset": 61440 },
        { "path": "$N-app.bin", "offset": 131072 }
      ]
    }
  ]
}
JSON
  cat > "$DIR/ota.json" <<JSON
{ "version": "$VERSION", "url": "$N-app.bin" }
JSON
  # prune to the two newest update files
  ls "$DIR"/$PFX-*-app.bin | sort -V | head -n -2 | xargs -r rm -f
  ls -l "$DIR"
}

package waveshare_7b gridcheck-7b "$OUT" "Waveshare ESP32-S3-Touch-LCD-7B"
package knob18 gridcheck-knob18 "$OUT/knob18" "Waveshare ESP32-S3-Knob-Touch-LCD-1.8"
rm -f "$TMPH" "$TMPH.h"
