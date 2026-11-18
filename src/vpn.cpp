#include "vpn.h"

#include <cstring>
#include <ctime>
#include <mutex>

#include "compat.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microlink.h"
#include "net.h"
#include "nvs.h"

static const char* TAG = "vpn";

namespace vpn {

static std::mutex lock;
static microlink_t* ml = nullptr;
static std::string runningKey;      // the key the running session was started with
static volatile bool authFailed = false;

static uint32_t homeIp() {
  if (!ml || !microlink_is_connected(ml)) return 0;
  const std::string h = net::settings().tsHome;
  if (h.empty()) return 0;
  const uint32_t ip = microlink_parse_ip(h.c_str());
  if (ip) return ip;
  // a Tailscale name: "homeassistant" or "homeassistant.tail1234.ts.net"
  uint32_t r = microlink_resolve(ml, h.c_str());
  if (!r) r = microlink_resolve(ml, h.substr(0, h.find('.')).c_str());
  return r;
}

bool ready() {
  std::lock_guard<std::mutex> g(lock);
  return homeIp() != 0;
}

std::string status() {
  std::lock_guard<std::mutex> g(lock);
  const Settings s = net::settings();
  if (!s.tsOn) return "off";
  if (s.tsKey.empty()) return "no auth key saved";
  if (authFailed) return "the auth key was rejected or has expired - make a new one";
  if (!ml) return net::connected() ? "starting..." : "waiting for Wi-Fi";
  const microlink_state_t st = microlink_get_state(ml);
  if (st != ML_STATE_CONNECTED) return "connecting...";
  char ip[20];
  microlink_ip_to_str(microlink_get_vpn_ip(ml), ip);
  std::string r = std::string("connected as ") + ip;
  if (!s.tsHome.empty()) r += homeIp() ? ", home device found" : ", home device \"" + s.tsHome + "\" not found";
  return r;
}

static void onState(microlink_t*, microlink_state_t st, void*) {
  if (st == ML_STATE_AUTH_FAILED) authFailed = true;
  if (st == ML_STATE_CONNECTED) authFailed = false;
  ESP_LOGI(TAG, "state %d", (int)st);
}

static void stopLocked() {
  if (!ml) return;
  microlink_stop(ml);
  microlink_destroy(ml);
  ml = nullptr;
  runningKey.clear();
}

static void watcher(void*) {
  for (;;) {
    const Settings s = net::settings();
    const bool clockSet = time(nullptr) > 1700000000;  // TLS needs the time
    const bool want = s.tsOn && !s.tsKey.empty() && net::connected() && clockSet;
    {
      std::lock_guard<std::mutex> g(lock);
      if (ml && (!want || s.tsKey != runningKey)) {
        ESP_LOGI(TAG, "stopping");
        stopLocked();
      }
      if (want && !ml && !authFailed) {
        microlink_config_t c = {};
        c.auth_key = s.tsKey.c_str();
        c.device_name = "gridcheck-knob";
        c.max_peers = 8;
        c.priority_peer_ip = microlink_parse_ip(s.tsHome.c_str());  // 0 when it's a name
        ml = microlink_init(&c);
        if (ml) {
          microlink_set_state_callback(ml, onState, nullptr);
          if (microlink_start(ml) == ESP_OK) {
            runningKey = s.tsKey;
            ESP_LOGI(TAG, "joining the tailnet");
          } else {
            microlink_destroy(ml);
            ml = nullptr;
          }
        }
      }
      if (authFailed && s.tsKey != runningKey) authFailed = false;  // a new key: try again
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void begin() { xTaskCreate(watcher, "vpn", 4096, nullptr, 2, nullptr); }

void* tcpConnect(uint16_t port, uint32_t timeoutMs, std::string& err) {
  uint32_t ip;
  {
    std::lock_guard<std::mutex> g(lock);
    ip = homeIp();
  }
  if (!ip) {
    err = "the home device isn't on the tailnet";
    return nullptr;
  }
  microlink_tcp_socket_t* s = microlink_tcp_connect(ml, ip, port, timeoutMs);
  if (!s) err = "no reply from the home device over Tailscale - is the Modbus proxy running?";
  return s;
}

bool tcpSend(void* sock, const void* data, size_t len) {
  return microlink_tcp_send((microlink_tcp_socket_t*)sock, data, len) == ESP_OK;
}

int tcpRecv(void* sock, void* buf, size_t len, uint32_t timeoutMs) {
  return microlink_tcp_recv((microlink_tcp_socket_t*)sock, buf, len, timeoutMs);
}

void tcpClose(void* sock) {
  if (sock) microlink_tcp_close((microlink_tcp_socket_t*)sock);
}

void forget() {
  for (const char* ns : {"microlink", "ml_peers"}) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK) {
      nvs_erase_all(h);
      nvs_commit(h);
      nvs_close(h);
    }
  }
}

}  // namespace vpn
