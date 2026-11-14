// Inverter brands GridCheck can read, and the reader for each.
//
// Every reader fills the same Live readings (see model.h) in the same units
// and signs: kW, grid + import / - export, battery + charging / - discharging.
#pragma once

#include <cstdint>
#include <string>

#include "model.h"

enum Brand : uint8_t {
  BRAND_SIGENERGY = 0,
  BRAND_SOLAX = 1,
  BRAND_SUNGROW = 2,
  BRAND_FRONIUS = 3,
  BRAND_GOODWE = 4,
  BRAND_VICTRON = 5,
  BRAND_COUNT
};

struct BrandInfo {
  const char* name;      // shown in settings, e.g. "Sungrow SH hybrid"
  uint16_t port;         // default port
  uint8_t unit;          // default Modbus unit id (0 = not used)
  const char* unitName;  // what the unit id is called for this brand ("" = hide the field)
  const char* howTo;     // what has to be switched on, for the phone page
};
extern const BrandInfo BRANDS[BRAND_COUNT];

class InverterReader {
 public:
  virtual ~InverterReader() = default;
  virtual void configure(const std::string& host, uint16_t port, uint8_t unit) = 0;
  /** One poll. Fills `out` and returns true, or returns false with out.error set. */
  virtual bool poll(Live& out) = 0;
};

/** A new reader for a brand (caller owns it). */
InverterReader* makeReader(uint8_t brand);
