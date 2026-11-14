#include "ota.h"

#include <mutex>

#include "compat.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "ota_verify.h"

#ifndef EH_VERSION
#define EH_VERSION "0.0.0"
#endif

static const char* TAG = "ota";

namespace ota {

const char* const DEFAULT_URL = "https://mrdinte.github.io/GridCheck_Screen/firmware/ota.json";

static std::mutex lock;
static Status st;
static std::string latestUrl;  // firmware file for `st.latest`
static volatile bool busy = false;
static volatile bool checkRequested = true;  // check once as soon as Wi-Fi is up

// One update being written (from the phone upload or the online download).
static esp_ota_handle_t wHandle = 0;
static const esp_partition_t* wPart = nullptr;
static size_t wSize = 0, wDone = 0;

static void set(State s, int pct, const std::string& msg) {
  std::lock_guard<std::mutex> g(lock);
  st.state = s;
  st.percent = pct;
  if (!msg.empty()) st.message = msg;
}

Status status() {
  std::lock_guard<std::mutex> g(lock);
  return st;
}

/** Compare dotted versions numerically: 0.10.0 > 0.9.3. */
static int cmpVersion(const std::string& a, const std::string& b) {
  int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
  sscanf(a.c_str(), "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b.c_str(), "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

bool updateAvailable() {
  std::lock_guard<std::mutex> g(lock);
  return !st.latest.empty() && cmpVersion(st.latest, EH_VERSION) > 0;
}

void checkNow() { checkRequested = true; }

// ---- writing an update (shared by upload and download) --------------------------------
static bool writeBegin(size_t size, std::string& err) {
  wPart = esp_ota_get_next_update_partition(nullptr);
  if (!wPart) {
    err = "this screen's flash isn't set up for updates - install version 0.5.0 or later over USB once";
    return false;
  }
  if (size < 100000 || size > wPart->size) {
    err = "that doesn't look like a GridCheck firmware file (wrong size)";
    return false;
  }
  const esp_err_t e = esp_ota_begin(wPart, size, &wHandle);
  if (e != ESP_OK) {
    err = std::string("can't start the update: ") + esp_err_to_name(e);
    return false;
  }
  wSize = size;
  wDone = 0;
  return true;
}

static bool writeChunk(const void* data, size_t len, std::string& err) {
  if (wDone == 0 && len > 0 && ((const uint8_t*)data)[0] != 0xE9) {
    err = "that isn't a firmware file (use the file ending in -app.bin)";
    return false;
  }
  if (wDone + len > wSize) {
    err = "the file is longer than it said";
    return false;
  }
  const esp_err_t e = esp_ota_write(wHandle, data, len);
  if (e != ESP_OK) {
    err = std::string("write failed: ") + esp_err_to_name(e);
    return false;
  }
  wDone += len;
  return true;
}

static void writeAbort() {
  if (wHandle) esp_ota_abort(wHandle);
  wHandle = 0;
}

/** Finish, check the signature, and only then make it the version to start next. */
static bool writeEnd(std::string& err) {
  if (wDone != wSize) {
    writeAbort();
    err = "the file was cut off";
    return false;
  }
  esp_err_t e = esp_ota_end(wHandle);
  wHandle = 0;
  if (e != ESP_OK) {
    err = e == ESP_ERR_OTA_VALIDATE_FAILED ? "the file is damaged or not for this screen" : std::string(esp_err_to_name(e));
    return false;
  }
  if (!verifySigned(wPart, wDone, err)) {
    ESP_LOGW(TAG, "rejected update: %s", err.c_str());
    return false;
  }
  e = esp_ota_set_boot_partition(wPart);
  if (e != ESP_OK) {
    err = esp_err_to_name(e);
    return false;
  }
  return true;
}

// ---- manifest -----------------------------------------------------------------------
static std::string jsonString(const std::string& body, const char* key) {
  const std::string k = std::string("\"") + key + "\"";
  size_t p = body.find(k);
  if (p == std::string::npos) return "";
  p = body.find(':', p);
  if (p == std::string::npos) return "";
  p = body.find('"', p);
  if (p == std::string::npos) return "";
  const size_t e = body.find('"', p + 1);
  return e == std::string::npos ? "" : body.substr(p + 1, e - p - 1);
}

static std::string updateUrl() {
  const std::string u = net::settings().updateUrl;
  return u.empty() ? DEFAULT_URL : u;
}

/** Open an HTTPS GET, following a few redirects. Returns the status code (or -1). */
static int openGet(esp_http_client_handle_t c) {
  for (int i = 0; i < 4; i++) {
    if (esp_http_client_open(c, 0) != ESP_OK) return -1;
    esp_http_client_fetch_headers(c);
    const int code = esp_http_client_get_status_code(c);
    if (code != 301 && code != 302 && code != 303 && code != 307 && code != 308) return code;
    esp_http_client_flush_response(c, nullptr);
    esp_http_client_close(c);
    if (esp_http_client_set_redirection(c) != ESP_OK) return code;
  }
  return -1;
}

static esp_http_client_handle_t client(const std::string& url) {
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;  // HTTPS, certificate checked
  cfg.timeout_ms = 15000;
  cfg.buffer_size = 4096;
  return esp_http_client_init(&cfg);
}

/** Fetch the update description: {"version": "0.6.0", "url": "gridcheck-7b-0.6.0-app.bin"}. */
static bool fetchManifest(std::string& version, std::string& url, std::string& err) {
  const std::string manifest = updateUrl();
  if (manifest.rfind("https://", 0) != 0) {
    err = "the update address must start with https://";
    return false;
  }
  esp_http_client_handle_t c = client(manifest);
  if (!c) {
    err = "out of memory";
    return false;
  }
  std::string body;
  bool ok = false;
  const int code = openGet(c);
  if (code == 200) {
    char buf[256];
    int n;
    while ((n = esp_http_client_read(c, buf, sizeof buf)) > 0 && body.size() < 4096) body.append(buf, n);
    version = jsonString(body, "version");
    url = jsonString(body, "url");
    ok = !version.empty() && !url.empty();
    if (!ok) err = "the update file doesn't look right";
  } else if (code < 0) {
    err = "can't reach the update address (no internet?)";
  } else {
    err = "update address answered " + std::to_string(code);
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  if (ok && url.rfind("https://", 0) != 0) {
    if (url.find("://") != std::string::npos) {
      err = "updates must come over https";
      return false;
    }
    url = manifest.substr(0, manifest.rfind('/') + 1) + url;  // relative to the manifest
  }
  return ok;
}

static void checkerTask(void*) {
  bool confirmed = false;
  uint32_t lastCheck = 0;
  for (;;) {
    // Confirm this firmware once it has been running fine for a minute, so the
    // bootloader won't go back to the previous version.
    if (!confirmed && millis() > 60000) {
      confirmed = true;
      esp_ota_img_states_t s;
      if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &s) == ESP_OK && s == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware %s confirmed", EH_VERSION);
      }
    }
    const bool due = checkRequested || (lastCheck && millis() - lastCheck > 12UL * 3600 * 1000);
    if (due && !busy && net::connected()) {
      checkRequested = false;
      lastCheck = millis() | 1;
      set(State::Checking, 0, "");
      std::string v, u, err;
      if (fetchManifest(v, u, err)) {
        {
          std::lock_guard<std::mutex> g(lock);
          st.latest = v;
          latestUrl = u;
        }
        const bool newer = cmpVersion(v, EH_VERSION) > 0;
        set(State::Idle, 0, newer ? "Version " + v + " is available" : "Up to date");
        ESP_LOGI(TAG, "online: %s (%s)", v.c_str(), newer ? "newer" : "not newer");
      } else {
        set(State::Idle, 0, "Couldn't check for updates: " + err);
        ESP_LOGW(TAG, "check failed: %s", err.c_str());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void begin() { xTaskCreate(checkerTask, "ota", 8192, nullptr, 1, nullptr); }

// ---- online install -----------------------------------------------------------------
static bool download(const std::string& url, std::string& err) {
  esp_http_client_handle_t c = client(url);
  if (!c) {
    err = "out of memory";
    return false;
  }
  bool ok = false;
  const int code = openGet(c);
  const int64_t len = esp_http_client_get_content_length(c);
  if (code != 200) {
    err = code < 0 ? "can't reach the update" : "update download answered " + std::to_string(code);
  } else if (len <= 0) {
    err = "the update server didn't say how big the file is";
  } else if (writeBegin((size_t)len, err)) {
    char* buf = (char*)malloc(4096);
    ok = buf != nullptr;
    if (!ok) err = "out of memory";
    while (ok && wDone < wSize) {
      const int n = esp_http_client_read(c, buf, 4096);
      if (n <= 0) {
        ok = false;
        err = "the download was cut off";
        break;
      }
      ok = writeChunk(buf, n, err);
      set(State::Downloading, (int)(wDone * 100 / wSize), "");
    }
    free(buf);
    if (ok) ok = writeEnd(err);
    else writeAbort();
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return ok;
}

static void installTask(void* arg) {
  std::string* url = (std::string*)arg;
  std::string err;
  const bool ok = download(*url, err);
  delete url;
  if (ok) {
    set(State::Done, 100, "Update installed. Restarting...");
    ESP_LOGI(TAG, "online update installed, restarting");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
  }
  set(State::Failed, 0, "Update failed: " + err + ". The current version keeps running.");
  ESP_LOGE(TAG, "online update failed: %s", err.c_str());
  busy = false;
  vTaskDelete(nullptr);
}

bool installOnline(std::string& err) {
  if (busy) {
    err = "an update is already running";
    return false;
  }
  std::string url;
  {
    std::lock_guard<std::mutex> g(lock);
    if (st.latest.empty() || cmpVersion(st.latest, EH_VERSION) <= 0) {
      err = "no newer version found yet - check for updates first";
      return false;
    }
    url = latestUrl;
  }
  busy = true;
  set(State::Downloading, 0, "Downloading update...");
  xTaskCreate(installTask, "ota-install", 8192, new std::string(url), 2, nullptr);
  return true;
}

// ---- upload from the phone page ----------------------------------------------------
bool uploadBegin(size_t size, std::string& err) {
  if (busy) {
    err = "an update is already running";
    return false;
  }
  if (!writeBegin(size, err)) return false;
  busy = true;
  set(State::Writing, 0, "Writing update...");
  return true;
}

bool uploadWrite(const void* data, size_t len, std::string& err) {
  if (!writeChunk(data, len, err)) return false;
  set(State::Writing, (int)(wDone * 100 / (wSize ? wSize : 1)), "");
  return true;
}

bool uploadEnd(std::string& err) {
  const bool ok = writeEnd(err);
  busy = false;
  if (!ok) {
    set(State::Failed, 0, "Update failed: " + err + ". The current version keeps running.");
    return false;
  }
  set(State::Done, 100, "Update installed. Restarting...");
  return true;
}

void uploadAbort() {
  writeAbort();
  busy = false;
  set(State::Failed, 0, "Update cancelled. The current version keeps running.");
}

}  // namespace ota
