// The brand list (names, default ports and unit ids, setup notes).
// No hardware code here, so the PC simulator uses it too.
#include "inverters.h"

const BrandInfo BRANDS[BRAND_COUNT] = {
    {"Sigenergy", 502, 247, "Plant unit id",
     "Modbus TCP must be switched on for the inverter (your installer does this in the mySigen app). The plant unit id is 247."},
    {"SolaX Hybrid Gen4", 502, 1, "Unit id",
     "Use the inverter's LAN port or a SolaX Pocket WiFi 3.0 / LAN dongle, and switch on Modbus TCP in the inverter's settings. Unit id is usually 1."},
    {"Sungrow SH hybrid", 502, 1, "Unit id",
     "Use the IP address of the WiNet-S dongle (or the inverter's LAN port). Modbus TCP is on by default on recent firmware. Unit id is usually 1."},
    {"Fronius Primo / Symo", 80, 0, "",
     "Uses the Fronius Solar API on your network. In the inverter's web page, switch on Communication > Solar API. Battery data needs a GEN24 Plus with a battery."},
    {"GoodWe hybrid (ET/EH/BT)", 8899, 247, "Unit id",
     "Talks to the GoodWe WiFi/LAN dongle on UDP port 8899, the same way the SEMS app does locally. Use the dongle's IP address. Unit id is 247."},
    {"Victron (Cerbo GX)", 502, 100, "Unit id",
     "Use the IP address of the Cerbo GX and switch on Settings > Services > Modbus TCP on it. Unit id 100 is the system overview."},
};
