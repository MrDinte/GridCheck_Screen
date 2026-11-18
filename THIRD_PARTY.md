# Third-party components

| Component | Used for | Licence |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | the board's operating system, Wi-Fi, display driver | Apache-2.0 |
| [LVGL](https://github.com/lvgl/lvgl) 8.4 | screen graphics (downloaded at build time) | MIT |
| Montserrat font (via LVGL) | text and the large numbers in `src/fonts/eh_num_*.c` | SIL Open Font License 1.1 |
| [Font Awesome Free](https://fontawesome.com) 5 (via LVGL) | icons in `src/fonts/eh_icons_44.c` | SIL Open Font License 1.1 (fonts), CC BY 4.0 (icons) |
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) 10.4.0 | the install button in `docs/` | Apache-2.0 (see `docs/vendor/esp-web-tools/LICENSE`) |
| [Open-Meteo](https://open-meteo.com) | optional solar forecast (free API, data CC BY 4.0) | |
| Waveshare ESP32-S3-Knob-Touch-LCD-1.8 demo code | the round panel's (ST77916) start-up sequence and the board's pins in `src/board_knob.cpp` | from Waveshare's published example code for the board |
| [MicroLink](https://github.com/CamM2325/microlink) 2.3 | Tailscale on the round screen (`components/microlink`, unmodified) | MIT |
| [wireguard-lwip](https://github.com/smartalock/wireguard-lwip) (inside MicroLink) | WireGuard tunnel for Tailscale | BSD-3-Clause |
