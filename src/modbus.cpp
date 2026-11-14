#include "modbus.h"

#include <fcntl.h>
#include <time.h>

#include "lwip/netdb.h"
#include "lwip/sockets.h"

static constexpr int TIMEOUT_MS = 3000;

void ModbusTcp::setTarget(const std::string& host, uint16_t port) {
  _host = host;
  _port = port;
  close();
}

void ModbusTcp::close() {
  if (_sock >= 0) ::close(_sock);
  _sock = -1;
}

bool ModbusTcp::connectNow(std::string& err) {
  close();
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  char port[8];
  snprintf(port, sizeof port, "%u", _port);
  if (getaddrinfo(_host.c_str(), port, &hints, &res) != 0 || !res) {
    err = "\"" + _host + "\" isn't a valid address";
    return false;
  }
  _sock = socket(AF_INET, SOCK_STREAM, 0);
  if (_sock < 0) {
    freeaddrinfo(res);
    err = "out of network sockets";
    return false;
  }
  // Connect with a timeout: non-blocking connect, then wait for writable.
  fcntl(_sock, F_SETFL, fcntl(_sock, F_GETFL, 0) | O_NONBLOCK);
  int r = connect(_sock, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (r < 0 && errno != EINPROGRESS) {
    close();
    err = "can't connect to " + _host + " - check the IP address";
    return false;
  }
  fd_set w;
  FD_ZERO(&w);
  FD_SET(_sock, &w);
  struct timeval tv = {TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000};
  int soerr = 0;
  socklen_t len = sizeof soerr;
  if (select(_sock + 1, nullptr, &w, nullptr, &tv) <= 0 || getsockopt(_sock, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0 || soerr) {
    close();
    err = soerr == ECONNREFUSED ? "nothing is accepting Modbus connections at " + _host + " - is Modbus TCP switched on?"
                                : "no reply from " + _host + " - check the IP address";
    return false;
  }
  fcntl(_sock, F_SETFL, fcntl(_sock, F_GETFL, 0) & ~O_NONBLOCK);
  struct timeval rt = {TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000};
  setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
  setsockopt(_sock, SOL_SOCKET, SO_SNDTIMEO, &rt, sizeof rt);
  int one = 1;
  setsockopt(_sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  return true;
}

bool ModbusTcp::readExactly(uint8_t* buf, size_t n) {
  size_t got = 0;
  while (got < n) {
    const int r = recv(_sock, buf + got, n - got, 0);
    if (r <= 0) return false;  // closed, error or timeout
    got += r;
  }
  return true;
}

MbResult ModbusTcp::readInput(uint8_t unit, uint16_t addr, uint16_t count, uint16_t* out, std::string& err) {
  if (_sock < 0 && !connectNow(err)) return MbResult::NoConnect;
  _tid++;
  const uint8_t req[12] = {(uint8_t)(_tid >> 8), (uint8_t)_tid, 0, 0, 0, 6, unit, 0x04,
                           (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(count >> 8), (uint8_t)count};
  uint8_t head[9];
  if (send(_sock, req, sizeof req, 0) != (int)sizeof req || !readExactly(head, 9)) {
    close();
    err = "the inverter hung up while reading " + std::to_string(addr) + " from unit " + std::to_string(unit);
    return MbResult::Dropped;
  }
  if (head[7] & 0x80) {
    err = "register " + std::to_string(addr) + " refused (Modbus exception " + std::to_string(head[8]) + ")";
    return MbResult::Refused;
  }
  const uint8_t bytes = head[8];
  uint8_t data[250];
  if (bytes != count * 2 || !readExactly(data, bytes)) {
    close();
    err = "reply cut short";
    return MbResult::Dropped;
  }
  for (uint16_t i = 0; i < count; i++) out[i] = (uint16_t)(data[i * 2] << 8 | data[i * 2 + 1]);
  return MbResult::Ok;
}

// ---- Sigenergy -------------------------------------------------------------
static float s32(const uint16_t* w, float gain) { return (float)(int32_t)((uint32_t)w[0] << 16 | w[1]) / gain; }
static float u32(const uint16_t* w, float gain) { return (float)((uint32_t)w[0] << 16 | w[1]) / gain; }

void SigenergyReader::configure(const std::string& host, uint16_t port, uint8_t plantId) {
  _mb.setTarget(host, port);
  _plant = plantId;
  _noLoad = _noToday = _noHouseToday = _noAvail = _noCap = _noTemp = false;
}

bool SigenergyReader::poll(Live& out) {
  uint16_t w[4];
  std::string err;
  MbResult r;

  // Required: grid power, battery %, PV power + battery power.
  if ((r = _mb.readInput(_plant, 30005, 2, w, err)) != MbResult::Ok) goto fail;
  out.grid = s32(w, 1000);
  if ((r = _mb.readInput(_plant, 30014, 1, w, err)) != MbResult::Ok) goto fail;
  out.soc = w[0] / 10.0f;
  if ((r = _mb.readInput(_plant, 30035, 4, w, err)) != MbResult::Ok) goto fail;
  out.solar = s32(w, 1000);
  out.battery = s32(w + 2, 1000);

  // Optional: older inverter firmware may not have these.
  out.house = NAN;
  if (!_noLoad) {
    r = _mb.readInput(_plant, 30284, 2, w, err);
    if (r == MbResult::Ok) out.house = s32(w, 1000);
    else if (r == MbResult::Refused) _noLoad = true;
    else goto fail;
  }
  if (std::isnan(out.house)) out.house = out.solar + out.grid - out.battery;
  if (out.house < 0) out.house = 0;
  if (!_noToday) {
    r = _mb.readInput(_plant, 30272, 4, w, err);
    if (r == MbResult::Ok) {
      out.solarToday = u32(w, 100);
      out.solarYesterday = u32(w + 2, 100);
    } else if (r == MbResult::Refused) _noToday = true;
    else goto fail;
  }
  if (!_noHouseToday) {
    r = _mb.readInput(_plant, 30092, 2, w, err);
    if (r == MbResult::Ok) out.houseToday = u32(w, 100);
    else if (r == MbResult::Refused) _noHouseToday = true;
    else goto fail;
  }

  // Battery details for the Battery page (all optional).
  if (!_noAvail) {
    r = _mb.readInput(_plant, 30064, 4, w, err);
    if (r == MbResult::Ok) {
      out.roomKWh = u32(w, 100);
      out.availKWh = u32(w + 2, 100);
    } else if (r == MbResult::Refused) _noAvail = true;
    else goto fail;
  }
  if (!_noCap) {
    uint16_t c[5];
    r = _mb.readInput(_plant, 30083, 5, c, err);  // capacity, charge cut-off, discharge cut-off, health
    if (r == MbResult::Ok) {
      out.capacityKWh = u32(c, 100);
      out.reserve = c[3] / 10.0f;
      out.soh = c[4] / 10.0f;
    } else if (r == MbResult::Refused) _noCap = true;
    else goto fail;
  }
  if (!_noTemp) {
    r = _mb.readInput(_plant, 30286, 1, w, err);
    if (r == MbResult::Ok) out.tempC = (int16_t)w[0] / 10.0f;
    else if (r == MbResult::Refused) _noTemp = true;
    else goto fail;
  }

  out.ok = true;
  out.demo = false;
  out.error[0] = 0;
  out.updatedMs = millis();
  return true;

fail:
  if (r == MbResult::Refused) err += ". Check the plant unit id (Sigenergy uses 247).";
  if (r == MbResult::Dropped) err += ". Is another device (e.g. Home Assistant) connected to it?";
  out.ok = false;
  snprintf(out.error, sizeof out.error, "%s", err.c_str());
  return false;
}

// ---- demo ------------------------------------------------------------------
void demoFill(Live& out) {
  // Use the real clock once it's set; before that, run a fast 24-minute "day".
  float h;
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm t;
    localtime_r(&now, &t);
    h = t.tm_hour + t.tm_min / 60.0f + t.tm_sec / 3600.0f;
  } else {
    h = fmodf(millis() / 60000.0f + 10.0f, 24.0f);
  }
  const float sun = fmaxf(0, sinf(PI * (h - 6) / 13));
  const float cloud = 0.85f + 0.15f * sinf(millis() / 41000.0f);
  out.solar = 7.0f * powf(sun, 1.3f) * cloud;
  const float bump = 1.2f * expf(-powf(h - 7.5f, 2) / 1.3f) + 1.9f * expf(-powf(h - 18.5f, 2) / 3.9f);
  out.house = 0.4f + bump + 0.1f * sinf(millis() / 7000.0f);
  static float soc = 55;
  const float surplus = out.solar - out.house;
  out.battery = 0;
  if (surplus > 0 && soc < 100) out.battery = fminf(surplus, 5);
  if (surplus < 0 && soc > 10) out.battery = -fminf(-surplus, 5);
  soc = constrain(soc + out.battery * 0.02f, 0.0f, 100.0f);
  out.soc = soc;
  out.grid = out.house + out.battery - out.solar;
  out.solarToday = 30.0f * (h < 6 ? 0 : h > 19 ? 1 : (1 - cosf(PI * (h - 6) / 13)) / 2);
  out.solarYesterday = 33.5f;
  out.houseToday = 0.9f * h + 2;
  out.capacityKWh = 13.5f;
  out.reserve = 10;
  out.availKWh = fmaxf(0, soc - 10) / 100 * 13.5f;
  out.roomKWh = (100 - soc) / 100 * 13.5f;
  out.soh = 99.2f;
  out.tempC = 24 + fabsf(out.battery) * 0.8f;
  out.ok = true;
  out.demo = true;
  out.error[0] = 0;
  out.updatedMs = millis();
}
