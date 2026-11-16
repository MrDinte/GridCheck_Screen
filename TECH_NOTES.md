# Tech notes: how updates are kept safe

GridCheck Screen updates itself over Wi-Fi. That is handy, but it also means the screen has to
be sure every update really came from GridCheck. This page explains how it does that, what it
protects against, and what it can't.

**The short version:** the screen only installs firmware that carries GridCheck's digital
signature. Only the holder of GridCheck's private signing key can make that signature, and that
key is not on GitHub, on the website or on the screen. Anyone else, whether they are on the
internet, on your home Wi-Fi or inside the GitHub account, can't get their own firmware onto
the screen over Wi-Fi.

## How an update reaches the screen

1. A new version is built, then **signed** on the maintainer's computer with the private key
   (`tools/sign.py`). The signature is added to the end of the firmware file.
2. The signed file and a small `ota.json` ("the newest version is 0.7.7, here is the file") are
   published on this repository's website (GitHub Pages).
3. The screen asks the website for `ota.json` when it starts and twice a day. If there is a
   newer version, a red dot appears on the Settings button. **Nothing installs on its own.**
4. When you press Install (on the screen or the phone page), the screen downloads the file into
   its **spare** firmware slot. The version you are running is not touched.
5. The screen then checks the file (see below). Only if every check passes does it switch to
   the new slot and restart.
6. The new version has to run properly for 20 seconds before it is kept. If it crashes or
   restarts before then, the screen goes back to the previous version by itself and says so.

You can also upload a firmware file from the phone page. It goes through exactly the same
checks.

## The checks before an update is used

| Check | What it stops |
| --- | --- |
| **Signature** (ECDSA P-256 over a SHA-256 hash of the whole file), checked against the public key built into the running firmware (`src/signing_pub.h`) | Any firmware not made by GridCheck. A file that is unsigned, signed with another key, or changed by even one bit is refused. |
| **Version**, read from *inside* the signed file (from 0.7.7) | Putting back an older GridCheck release, for example one with a problem that has since been fixed. Because the version is inside the signed part, it can't be faked. |
| **Image format** (ESP-IDF's own check of the firmware layout and checksum) | Damaged or cut-off downloads, and files for a different chip. |
| **Size** (must fit the firmware slot and match the length announced) | Oversized or truncated files. |

The signature is checked after the whole file is written to the spare slot and before the
screen is told to start from it. A refused file is simply left unused in the spare slot, and the
current version keeps running.

## Attacks from the internet

- **Nothing on the internet can connect to the screen.** It only makes outgoing connections:
  to this website for updates, to Open-Meteo for the solar forecast, and to a time server. It
  doesn't open any port on your router. (Don't set up port forwarding to it.)
- **Downloads use HTTPS with the certificate checked**, against the certificate list built into
  ESP-IDF. The update address must start with `https://`, and so must the firmware link inside
  `ota.json`. (Even a file that arrived some other way would still have to pass the signature
  check.)
- **Even if the website or the GitHub account were taken over**, an attacker could only publish
  files that the screen then refuses, because they can't sign them. The worst they could do is
  stop updates from appearing, or announce a version that then fails the checks.
- **The private key never goes to GitHub.** `keys/` is in `.gitignore`, and `tools/package.sh`
  refuses to publish if the key doesn't match the public key in the firmware.

## Attacks from your home network

Someone (or a hacked device) on your own Wi-Fi can reach the screen's phone settings page.

- **They still can't install their own firmware**: uploads go through the same signature and
  version checks as online updates.
- **Set a settings PIN on the phone page.** Without a PIN, anyone on your Wi-Fi can change the
  screen's settings, restart it, or start a genuine update. With a PIN, all of those need the PIN.
  - Wrong PINs are slowed down: after 5 wrong tries the page locks for 1 minute, then 2, 4, 8
    and 16 minutes. The PIN is stored only as a salted, repeatedly hashed value.
  - Use 6 to 8 digits rather than 4.
  - Forgot it? Hold **Reset all settings** on the screen for 5 seconds (this clears all
    settings, including Wi-Fi).
- **The setup hotspot.** `GridCheck-Setup` has no password, so it is only open when it is
  needed: on first setup, or when the screen can't join your Wi-Fi for 45 seconds (for example
  during a power cut). While it is open, the screen refuses firmware uploads (from 0.7.7 this
  covers the 15 seconds the hotspot stays open after the screen gets back onto your Wi-Fi). A
  PIN also protects your settings from anyone who joins the hotspot.
- **Read-only to the inverter.** The screen only *reads* the inverter (Modbus function 4) and
  never sends it commands.

## What it can't protect against

Being honest about the limits:

- **Physical access over USB.** Anyone holding the board can re-flash it with any firmware
  over its USB port, and could read the saved Wi-Fi password from its memory. The ESP32-S3 has
  "secure boot" and "flash encryption" features that would block this, but switching them on
  is permanent (it burns fuses in the chip) and a mistake can make the board unusable, so
  GridCheck doesn't use them. Keep the screen where you'd keep your router.
- **The phone page uses plain HTTP**, not HTTPS, because a small device on a home network has no
  certificate a phone would trust. Someone already on your Wi-Fi and capturing traffic could
  read the PIN as you type it. That would let them change settings, but still not install
  their own firmware.
- **Anyone on your Wi-Fi can view** the phone page and live readings (power, battery level). The
  Wi-Fi password is never shown.
- **The inverter itself.** Sigenergy's Modbus TCP has no password. Anything on your network can
  talk to the inverter directly, with or without GridCheck. Don't expose the inverter to the
  internet, and consider a separate network for smart-home devices.
- **The signing key is the crown jewel.** Whoever holds `keys/signing_key.pem` can make updates
  every screen will accept. It must be kept private and backed up. If it were ever leaked, a new
  key would be needed, and every screen would need one USB install of a version with the new
  public key.

## Checklist for owners

- Set a settings PIN (6 to 8 digits) on the phone page.
- Use a strong password on your home Wi-Fi.
- Don't port-forward anything to the screen or the inverter.
- Leave the update address on the phone page at its default unless you build your own
  firmware.

## For developers

- Signed file format: the normal ESP-IDF app image, then a 72-byte trailer: a 64-byte ECDSA
  P-256 signature (r‖s) of the SHA-256 of everything before it, then the 8 bytes `GCSIG001`.
- Code: `src/ota_verify.cpp` (signature), `src/ota.cpp` (check, download, version and
  roll-back), `src/net.cpp` (phone page, PIN, upload handler), `tools/sign.py` (sign, verify,
  new key), `tools/package.sh` (build, sign, publish files).
- To run your own builds: make your own key with `tools/sign.py newkey`, build its public key into
  the firmware with `tools/sign.py header`, install that build over USB once, and point the
  update address at your own `ota.json`. Screens running the official firmware will never
  accept your builds, and vice versa.
