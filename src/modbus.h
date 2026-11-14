// Minimal Modbus TCP client and the Sigenergy reads GridCheck needs.
// Same registers, units and gains as the GridCheck web dashboard.
#pragma once

#include <string>

#include "inverters.h"
#include "model.h"

enum class MbResult { Ok, Refused, Dropped, NoConnect };

class ModbusTcp {
 public:
  void setTarget(const std::string& host, uint16_t port);
  /** Read `count` input registers (function 0x04). */
  MbResult readInput(uint8_t unit, uint16_t addr, uint16_t count, uint16_t* out, std::string& err) {
    return read(0x04, unit, addr, count, out, err);
  }
  /** Read `count` holding registers (function 0x03). */
  MbResult readHolding(uint8_t unit, uint16_t addr, uint16_t count, uint16_t* out, std::string& err) {
    return read(0x03, unit, addr, count, out, err);
  }
  MbResult read(uint8_t func, uint8_t unit, uint16_t addr, uint16_t count, uint16_t* out, std::string& err);
  void close();

 private:
  bool connectNow(std::string& err);
  bool readExactly(uint8_t* buf, size_t n);
  int _sock = -1;
  std::string _host;
  uint16_t _port = 502;
  uint16_t _tid = 0;
};

class SigenergyReader : public InverterReader {
 public:
  void configure(const std::string& host, uint16_t port, uint8_t plantId) override;
  bool poll(Live& out) override;

 private:
  ModbusTcp _mb;
  uint8_t _plant = 247;
  bool _noLoad = false, _noToday = false, _noHouseToday = false, _noAvail = false, _noCap = false, _noTemp = false;  // registers this firmware lacks
};

/** Simulated system for demo mode. */
void demoFill(Live& out);
