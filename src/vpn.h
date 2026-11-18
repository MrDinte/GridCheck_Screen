// Tailscale for the round knob screen (MicroLink, components/microlink), so it can still
// read the inverter when it's away from home: through a Tailscale device at home (e.g.
// Home Assistant with the Tailscale and Modbus Proxy add-ons) that passes on Modbus.
// Only built for the knob (GC_KNOB18); see README "Away from home".
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vpn {
/** Start the watcher: joins the tailnet when switched on, a key is saved, Wi-Fi is up and the clock is set. */
void begin();
/** On the tailnet and the home device is known. */
bool ready();
/** For the phone page: "connected as 100.x.y.z", "off", "the auth key was rejected..." */
std::string status();
/** Short, for the round screen: "Tailscale: connected", "Tailscale: key rejected"; "" when off. */
std::string brief();

/** For the round screen's Connection page. */
struct Diag {
  bool on = false;         // switched on in settings
  bool connected = false;  // on the tailnet
  std::string state;       // "connected", "connecting...", "key rejected", "no key saved", "off"
  std::string ip;          // this screen's 100.x address
  std::string home;        // the home device as set ("" = not set)
  std::string homeIp;      // its 100.x address once found
  bool homeFound = false;
  bool homeDirect = false; // direct path (not relayed through Tailscale's servers)
};
Diag diag();

// A TCP connection to the home device over the tailnet (for Modbus).
void* tcpConnect(uint16_t port, uint32_t timeoutMs, std::string& err);
bool tcpSend(void* sock, const void* data, size_t len);
/** Bytes received, 0 on timeout, -1 when the connection is gone. */
int tcpRecv(void* sock, void* buf, size_t len, uint32_t timeoutMs);
void tcpClose(void* sock);
/** Forget this screen's Tailscale identity (part of "Reset all settings"). */
void forget();
}  // namespace vpn
