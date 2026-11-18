// Minimal Modbus TCP client and the Sigenergy reads GridCheck needs.
// Same registers, units and gains as the GridCheck web dashboard.
#pragma once

#include <string>

#include "model.h"

enum class MbResult { Ok, Refused, Dropped, NoConnect };

class ModbusTcp {
 public:
  void setTarget(const std::string& host, uint16_t port);
  /** Read `count` input registers (function 0x04). */
  MbResult readInput(uint8_t unit, uint16_t addr, uint16_t count, uint16_t* out, std::string& err);
  void close();
  /** Round screen only: read through the home device on the tailnet instead of directly. */
  void setVpn(bool on);
  bool vpn() const { return _vpn; }
  /** Can a connection be opened right now? (closed again straight away) */
  bool probe(std::string& err);

 private:
  bool connectNow(std::string& err);
  bool readExactly(uint8_t* buf, size_t n);
  bool connected() const { return _sock >= 0 || _vsock; }
  int _sock = -1;
  void* _vsock = nullptr;  // tailnet connection (round screen)
  bool _vpn = false;
  std::string _host;
  uint16_t _port = 502;
  uint16_t _tid = 0;
};

class SigenergyReader {
 public:
  void configure(const std::string& host, uint16_t port, uint8_t plantId);
  /** One poll. Fills `out` and returns true, or returns false with out.error set. */
  bool poll(Live& out);
  /** Round screen away from home: read through Tailscale (see vpn.h). */
  void useVpn(bool on) { _mb.setVpn(on); }
  bool viaVpn() const { return _mb.vpn(); }

 private:
  ModbusTcp _mb;
  uint8_t _plant = 247;
  bool _noLoad = false, _noToday = false, _noHouseToday = false, _noAvail = false, _noCap = false, _noTemp = false;  // registers this firmware lacks
};

/** Does the inverter (or Modbus proxy) answer on the local network? Quick check, closes again. */
bool lanReachable(const std::string& host, uint16_t port);

/** Simulated system for demo mode. */
void demoFill(Live& out);
