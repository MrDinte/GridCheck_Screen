# GridCheck Screen: notes for Claude

Firmware for a wall display showing a **Sigenergy** solar + battery system on the
**Waveshare ESP32-S3-Touch-LCD-7B** (1024×600 RGB panel, 16 MB flash, 8 MB PSRAM). It reads
the inverter over Modbus TCP on the home network and updates itself over Wi-Fi from this
repo's GitHub Pages site. The owner flashes the board, tests on real hardware and reports back;
we can't run the board here, so check UI changes in the PC simulator (`sim/`) before publishing.

## Before /clear or /compact: save the latest notes
When the owner says they're about to `/clear` or `/compact` (or asks to "save notes"), first
update **Current state** below: published version, what's being tested on the board, open
requests and agreed next steps, anything parked. Keep it short, replace stale lines rather than
appending, then commit and push. Only keep project facts here (this repo is public): no IP
addresses, emails or keys.

## Current state (updated 2026-11-16)
- Published: **0.7.7**. 0.7.5: Now page icons open their pages; Battery page battery that fills.
  0.7.6: Power prices editor on Settings & info (supplier list + number pad), Grid supplier on
  the phone page (not yet tried on the board). 0.7.7: refuses older versions (version read from
  the signed image) and uploads while the hotspot is open; `TECH_NOTES.md` explains update security.
- Offered, not yet agreed: passphrase on the signing key; signing `ota.json` is not needed
  (the version check uses the signed image).
- Owner's board: on 0.7.x. Updating over Wi-Fi is confirmed working on the real board
  (0.6.7 → 0.6.8); the red update dot is built and was being checked with 0.7.x test releases.
- Offered, not yet agreed: use Sigenergy lifetime energy counters (30088 PV, 30094 load,
  30200/30204 battery, 30216/30220 grid) for exact daily totals and "since installed" totals.
  Modbus has no history log, so past days can't be backfilled.
- Not wanted for now: microSD storage, other inverter brands, Amber/AEMO price alerts.

## Scope and preferences
- **Sigenergy only for now.** Other brands (SolaX, Sungrow, Fronius, GoodWe, Victron) are parked
  on the `multi-inverter` branch with research notes in `docs/research/other-inverters.md`.
  **Amber/AEMO price alerts are parked too.** Don't start either unless asked.
- The owner wants plain-English text on the screen and phone page, and asks for the
  release notes to stay tidy: summarise a run of small versions into one entry (see the 0.6
  entry in README "Version history").
- Updates are checked at start-up and **twice a day**, and a red dot on the Settings cog shows
  when one is available. Nothing installs without the owner pressing Install.
- Version numbers: `-DEH_VERSION=\"x.y.z\"` in `platformio.ini`. The 0.7.x series is current.

## Build, check, release
```sh
pio run                          # build (PlatformIO, ESP-IDF 5.3 + LVGL 8.4, not Arduino)
sh sim/build.sh && sh sim/render.sh        # render screens to sim/out/*.png on the PC
python3 sim/make_readme_images.py          # regenerate README screenshots (docs/images)
sh tools/package.sh              # build, SIGN, write docs/firmware/ (manifest.json, ota.json, bins)
```
Release = bump `EH_VERSION`, add a README "Version history" line, run `tools/package.sh`,
commit and push to `main` (GitHub Pages serves `docs/`, so pushing publishes the update; Pages
caches `ota.json` for a few minutes). `package.sh` keeps only the two newest `-app.bin` files.

## Signing (important)
- Every update must be signed: `tools/sign.py` appends an ECDSA P-256 signature trailer, and the
  screen refuses anything that doesn't verify against `src/signing_pub.h` (see `src/ota_verify.*`).
- The private key lives at `keys/signing_key.pem`, which is **gitignored and must never be
  committed** (this repo is public). The owner keeps a copy. A fresh session won't have it:
  ask the owner to attach the key file before publishing a release. `package.sh` refuses to
  run if the key doesn't match `src/signing_pub.h`.
- A new key would need one USB install of a build containing the new public key.

## Hardware and memory lessons (don't relearn these)
- RGB panel: bounce buffers + code/rodata from PSRAM (`sdkconfig.defaults`) keep the picture
  stable; the board flickered badly without them. Panel timing and pins are in `src/board.cpp`.
- IO expander at I2C 0x24 (backlight, LCD power, touch reset); GT911 touch read directly.
- **Internal RAM is scarce** (LVGL draw buffers + bounce buffers). Consequences we hit:
  - TLS must allocate from PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`), or HTTPS fails.
  - Creating new tasks at run time can silently fail: the online update runs inside the
    existing checker task (`src/ota.cpp`) and restarts use `restartSoon()` with a fallback.
- Flash layout (`partitions.csv`): nvs, otadata, ota_0/ota_1 (4 MB each), FAT `storage` (2 MB)
  for history at `/data`. Settings are in NVS namespace `eh`. Changing the layout needs a USB
  install and clears history.
- A new version confirms itself after 20 s; a reset before that rolls back to the previous
  version, and the screen reports it.

## Code map
- `src/main.cpp` UI loop (dimming, theme/palette rebuilds), inverter polling task.
- `src/modbus.*` Modbus TCP client + Sigenergy reader (plant unit 247, input registers 300xx).
- `src/net.*` Wi-Fi, settings (NVS), the phone settings page (HTML/JS inside C++), HTTP handlers.
- `src/ota.*`, `src/ota_verify.*` update check/download/upload and signature check.
- `src/data.*` history (minute samples, daily records), forecast; `src/tariff.cpp` time-of-use.
- `src/ui.cpp`, `src/ui_pages.cpp` LVGL screens; colours come only from the palette in
  `applyPalette()` (3 palettes × dark/light), so new UI must use the `C_*` colours.
- `sim/` compiles the real UI code on Linux; `sim/sim_main.cpp` stubs net/data/ota
  (env vars: `SIM_LIGHT`, `SIM_PALETTE`, `SIM_OTA`, `SIM_OTA_PCT`, `SIM_TOU`, `SIM_TIME`, `SIM_AP`).

## Related repo
The web dashboard and Node bridge (the original browser version, 800×480 and round layouts)
live in the private `MrDinte/EnergyHub` repo. Keep its look in step with the screen when the
owner asks (e.g. the Home and Settings buttons).
