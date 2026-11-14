# Other inverter brands: research notes (parked)

Collected 2026-11-14 from open-source projects (sources named per brand). Not yet implemented
or tested on real hardware. Our conventions: kW, grid + import / - export, battery + charging / - discharging.
All addresses are 0-based wire (PDU) addresses unless stated.

## Sungrow SH hybrids
Source: mkaiser/Sungrow-SHx-Inverter-Modbus-Home-Assistant `modbus_sungrow.yaml` (main), bohdan-s/SunGather.
- Modbus TCP 502, unit 1. Live values FC04 (input); min/max SoC and reserve FC03 (holding).
  32-bit values are **low word first**. 20-30 ms gap between requests via WiNet-S, 5 ms on LAN.
- Connection: inverter LAN port strongly recommended. Enable it in iSolarCloud: Support > Local Access
  (admin / pw8888) > More > Communication Settings > "Inverter ETH port". WiNet-S works with limits;
  some firmware blocks Modbus on it (its WebSocket API on 8082 can still identify the device).
- Solar W: 5016 u32. Grid: 13009 s32 "export power" (+ = export, so grid = -raw); 0x7FFFFFFF = no value.
  Battery: 5213 s32 (- = charging, so battery = -raw). Older firmware: 13021 unsigned + 13000 bits
  (0x02 charging, 0x04 discharging). House: 13007 s32. SOC 13022 u16 x0.1. SOH 13023 x0.1.
  Battery temp 13024 s16 x0.1. Capacity 5638 u16 x0.01 kWh. Solar today 13001 u16 x0.1 kWh.
  House today = PV(13001) - export(13044) + import(13035) - batt charge(13039) + batt discharge(13025), all x0.1.
- Blocks: FC04 5016x2, 5213x2, 13000x45 (keep blocks <= 38 near 5000 for SH*RS over WiNet-S).
- Fingerprint: FC04 4999 x1 = device type code; 0x0D0D-0x0D1B SH-RS, 0x0E00-0x0E13 SH-RT, 0x0E20-0x0E28 SH-T.
- Discovery: none known; scan /24 for TCP 502 then fingerprint.

## SolaX Hybrid Gen4 (X1/X3-Hybrid G4)
Source: wills106/homeassistant-solax-modbus `plugin_solax.py`, docs/.
- Gen4 has **no LAN port**. Use Pocket WiFi 3.0 (firmware V3.004.03+) on 502, or the RS485 COM port
  with an RS485-to-Ethernet gateway (Modbus TCP to RTU, 19200 baud). Pocket LAN does not do Modbus.
- Modbus TCP 502, unit 1 (some report 4). Live values FC04. 32-bit **low word first**.
- PV total 0x32 u32 W (protocol version >= 100, holding 0x82; else PV1 0x0A + PV2 0x0B u16).
  Grid 0x46 s32 W (+ = feed-in, so grid = -raw). Battery 0x16 s16 W (+ = charging).
  SOC 0x1C u16 %. Battery temp 0x18 s16 C. SOH 0xBF %. Solar today 0x96 u16 x0.1 kWh.
  House = inverter AC power (0x02 s16) + grid (ours). Min SOC holding 0x93 high byte.
- Blocks: FC04 0 x81, FC04 150 x42.
- Fingerprint: FC03 0x00 x7 = serial ASCII; X1 G4 prefixes H43/H44/H450/H460/H475, X3 G4 H34/N34.

## GoodWe hybrids
Source: marcelblijleven/goodwe (master): et.py, es.py, protocol.py, modbus.py, __init__.py.
- ET family (ET/EH/BT/BH, Gen2 ES "ESN/EMN/EBN"): Modbus RTU over **UDP 8899**, unit 0xF7, FC03.
  Request: F7 03 hi lo cnthi cntlo CRClo CRChi (CRC-16/Modbus). Reply: AA 55 F7 03 len data CRC
  (CRC over bytes from F7). Replies may arrive in two datagrams. TCP 502 works on the V2.0 LAN+WiFi kit.
  Example frame (35100 x125): f7 03 89 1c 00 7d 7a e7.
- Running block 35100 x125: PV = u32 at 35105/35109/35113/35117 (W, 0xFFFFFFFF = 0).
  Grid 35140 s16 (+ = export, grid = -raw). Battery 35182 s32 (+ = discharge, battery = -raw).
  House = PV + battery(GoodWe sign) - grid(GoodWe sign). Solar today 35193 u32 x0.1 kWh.
  House today 35205 u16 x0.1. Battery block 37000 x24: SOC 37007, SOH 37008, temp 37003 s16 x0.1.
- ES family (ESU/EMU/ESA/BPS/BPU/EMJ/IJL): AA55 protocol on UDP 8899, request aa55c07f0106000245.
- Discovery: UDP broadcast "WIFIKIT-214028-READ" to port 48899 -> reply "IP,MAC,name".
  Family from serial (AA55 aa55c07f0102000241, serial at payload[31:47]).

## Fronius Primo / Symo / GEN24
Source: home-assistant/core fronius (uses fronius-solarapi 0.1.1), test fixtures.
- Solar API over HTTP, no login: GET /solar_api/v1/GetPowerFlowRealtimeData.fcgi, Head.Status.Code == 0.
  Must be enabled in the inverter web UI (GEN24 returns 404 on everything until it is).
- Body.Data.Site: P_PV (W, null at night -> 0), P_Grid (+ import, matches ours), P_Akku (+ discharge,
  battery = -raw; null without battery), P_Load (negative = consumption, house = -raw).
  SOC: Body.Data.Inverters."1".SOC (only trust when P_Akku not null). E_Day Wh (null on GEN24:
  derive from E_Total at midnight).
- Probe: /solar_api/GetAPIVersion.cgi; /solar_api/v1/GetLoggerInfo.cgi (Datamanager) vs Code 11 (GEN24).
  HA discovers by DHCP MAC prefix 00:03:AC; no mDNS.

## Victron Cerbo GX
Source: victronenergy/dbus_modbustcp attributes.csv, sfstar/hass-victron, dbus-systemcalc-py.
- Modbus TCP 502, unit 100 (system), FC03, 32-bit high word first. Enable Modbus TCP in the GX settings.
  Unavailable values read as 0.
- One block 840 x74: battery 842 s16 W (+ charging), SOC 843 u16 %, DC PV 850 u16 W,
  PV on AC-out 884/886/888 u32, PV on AC-in 890/892/894 u32, house 902/904/906 u32,
  grid 908/910/912 s32 (+ import). Fallback for old firmware: 808 x19 (16-bit) + 840 x11.
- Solar today only per MPPT unit (reg 784 x0.1 kWh); battery temp on the battery unit (262 x0.1).
- Discovery: mDNS hostname venus.local; SSDP announcement. Fingerprint: 800 x6 = serial (12 hex chars).

## Amber / AEMO prices (for later)
- Amber official API: https://api.amber.com.au/v1, Bearer token from app.amber.com.au (Settings >
  Developer Mode > Generate API Key). /sites, /sites/{id}/prices/current?next=12. Fields: perKwh,
  spotPerKwh, descriptor (extremelyLow..spike), spikeStatus (none/potential/spike), channelType
  (general/feedIn; feedIn perKwh negative = paid), estimate, advancedPrice. ~50 requests / 5 min.
- Unofficial, no account: GET https://backend.amber.com.au/postcode/{postcode}/prices?past-hours=1
  with header Origin: https://www.amber.com.au (undocumented; may break).
- AEMO wholesale (anyone): https://dashboards.public.aemo.com.au (replaced visualisations.aemo.com.au
  in Nov 2026): GET /config.json -> xApiKey, then /NEM/v1/PWS/NEMDashboard/elecSummary with
  x-api-key header; price $/MWh ex-GST per region. Postcode -> region by state (ACT -> NSW1;
  WA/NT not in NEM).
- None of these hosts could be reached from the build environment; test from a real network first.
