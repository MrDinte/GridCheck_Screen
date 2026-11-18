# GridCheck Screen: notes for Claude

Firmware for a display showing a **Sigenergy** solar + battery system on two boards: the
**Waveshare ESP32-S3-Touch-LCD-7B** (1024×600 RGB panel, 16 MB flash, 8 MB PSRAM) and the round
**Waveshare ESP32-S3-Knob-Touch-LCD-1.8 / 1.8B** (360×360 ST77916 over QSPI, CST816 touch,
rotary knob; 1.8B = black case, same electronics). It reads
the inverter over Modbus TCP on the home network and updates itself over Wi-Fi from this
repo's GitHub Pages site. The owner flashes the board, tests on real hardware and reports back;
we can't run the board here, so check UI changes in the PC simulator (`sim/`) before publishing.

## Before /clear or /compact: save the latest notes
When the owner says they're about to `/clear` or `/compact` (or asks to "save notes"), first
update **Current state** below: published version, what's being tested on the board, open
requests and agreed next steps, anything parked. Keep it short, replace stale lines rather than
appending, then commit and push. Only keep project facts here (this repo is public): no IP
addresses, emails or keys.

## Current state (updated 2026-11-18)
- Published: **0.8.0** for both screens. New: the round knob screen (`board_knob.cpp`,
  `ui_round.cpp`; six pages Now/Battery/Solar/Money/Today/Settings, knob turns pages and wraps,
  edge ring = battery charge). **Not yet run on a real knob board**: first install is over USB
  from the install page; check the picture, touch, knob direction (A = clockwise in
  Waveshare's demo), backlight. The 7" is unchanged apart from refusing firmware for the other
  screen (project name check in `ota.cpp`).
- 0.7.5-0.7.9 (7"): tappable Now icons, filling battery, supplier list, Peak/Off-peak/Shoulder
  editor with time wheels, feed-in at set hours with a daily limit (owner: 10c 16:00-21:00 for
  the first 10 kWh, 0c otherwise), refuse older versions, `TECH_NOTES.md`. Not yet tried on the board.
- Parked for later (owner's request): remote access with Tailscale on the screen itself, using
  microlink, a community Tailscale client for ESP32 (https://osrtos.com/projects/microlink-tailscale-vpn-for-esp32/).
  Not looked at yet: check its licence, ESP-IDF version, RAM/flash needs (TLS + WireGuard on top
  of LVGL; internal RAM is already tight on the 7") and how it authenticates. Until then the
  answer is a Tailscale subnet router at home. Nothing built.
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
- Version numbers: `-DEH_VERSION=\"x.y.z\"` in the shared `[env]` of `platformio.ini` (one
  version for both screens). The 0.8.x series is current.

## Build, check, release
```sh
pio run                          # build both (PlatformIO, ESP-IDF 5.3 + LVGL 8.4, not Arduino)
pio run -e knob18                # just the round screen (-e waveshare_7b for the 7")
sh sim/build.sh && sh sim/render.sh        # render 7" screens to sim/out/*.png on the PC
BOARD=knob18 sh sim/build.sh && sh sim/render_round.sh   # round screens -> sim/out/round-*.png
python3 sim/make_readme_images.py          # regenerate README screenshots (docs/images)
sh tools/package.sh              # build both, SIGN, write docs/firmware/ (7") and docs/firmware/knob18/
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

## Two boards (0.8.0)
- `GC_BOARD` (CMake, `-DGC_BOARD=knob18` from `board_build.cmake_extra_args`) picks the board:
  `src/CMakeLists.txt` drops `board.cpp`/`ui.cpp`/`ui_pages.cpp` for the knob and
  `board_knob.cpp`/`ui_round.cpp` for the 7"; the knob also defines `GC_KNOB18` and uses
  `sdkconfig.defaults.knob18` (no RGB workarounds, DIO flash as in Waveshare's demo).
- Project name `gridcheck` (7") / `gridcheck-knob18` is stamped into the app; `ota.cpp`
  refuses an update whose project name differs. Each screen has its own update file:
  `docs/firmware/ota.json` (7") and `docs/firmware/knob18/ota.json`. Never move the 7" one.
- Knob pins/init come from Waveshare's ESP-IDF demo (08_LVGL_Test): LCD CS 14, CLK 13,
  D0-D3 15-18, RST 21, backlight 47 (PWM); touch CST816 0x15 on SDA 11/SCL 12, RST 10, INT 9;
  knob A 8 / B 7 (each click pulses one line). Panel areas must be even-aligned (rounder in
  `main.cpp`); pixels are byte-swapped in `board_knob.cpp`.
- USB on the knob: one way round the USB-C plug reaches the second chip (CH340); the S3 shows
  as "USB JTAG/serial debug unit".
- Shared UI helpers (palette, labels, status line `statusNow()`, `touNow()`) are in `ui_common.cpp`.

## Code map
- `src/main.cpp` UI loop (dimming, theme/palette rebuilds), inverter polling task.
- `src/modbus.*` Modbus TCP client + Sigenergy reader (plant unit 247, input registers 300xx).
- `src/net.*` Wi-Fi, settings (NVS), the phone settings page (HTML/JS inside C++), HTTP handlers.
- `src/ota.*`, `src/ota_verify.*` update check/download/upload and signature check.
- `src/data.*` history (minute samples, daily records), forecast; `src/tariff.cpp` time-of-use.
- `src/ui.cpp`, `src/ui_pages.cpp` LVGL screens; colours come only from the palette in
  `applyPalette()` (3 palettes × dark/light), so new UI must use the `C_*` colours.
- `sim/` compiles the real UI code on Linux; `sim/sim_main.cpp` stubs net/data/ota
  (env vars: `SIM_LIGHT`, `SIM_PALETTE`, `SIM_OTA`, `SIM_OTA_PCT`, `SIM_TOU`, `SIM_TIME`, `SIM_AP`,
  `SIM_SUPPLIER`, `SIM_FIT`; pages `prices`, `prices-end`, `prices-list` open the price editor).

## Related repo
The web dashboard and Node bridge (the original browser version, 800×480 and round layouts)
live in the private `MrDinte/EnergyHub` repo. Keep its look in step with the screen when the
owner asks (e.g. the Home and Settings buttons).
