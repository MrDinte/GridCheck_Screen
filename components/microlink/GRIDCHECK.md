# MicroLink in GridCheck

A copy of [MicroLink](https://github.com/CamM2325/microlink) (`components/microlink` at commit
ee5a2bab225cc002aa4368a4d485b6d01a785dc2), an unofficial Tailscale-compatible VPN client for
ESP32. MIT licence (`LICENSE`); the bundled wireguard-lwip is BSD-3-Clause
(`components/wireguard_lwip/LICENSE`); X25519 notice in `x25519-license.txt`.

Only the round knob screen's build uses it (`GC_BOARD=knob18`); the 7" build excludes it
(`EXCLUDE_COMPONENTS` in the top-level `CMakeLists.txt`). GridCheck's own code that uses it is
`src/vpn.cpp`. Keep this copy unmodified so it can be updated by copying a newer release over it.
