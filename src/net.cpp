#include "net.h"

#include <algorithm>
#include <ctime>
#include <mutex>
#include <vector>

#include "board.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/sha256.h"
#include "model.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "inverters.h"
#include "ota.h"

#ifndef EH_VERSION
#define EH_VERSION "0.0.0"
#endif

static const char* TAG = "net";

namespace net {

static Settings S;
static std::mutex lock;
static volatile uint32_t version = 1;
static volatile bool ap = false, apBecauseNoWifi = false, haveIp = false;
static char ipStr[16] = "";
static uint32_t staStartMs = 0, connectedSinceMs = 0;
static std::vector<std::string> networks;
static int pinFails = 0;
static uint32_t pinLockedUntil = 0;
static esp_netif_t *staIf = nullptr, *apIf = nullptr;
static bool sntpStarted = false;

static const char* AP_NAME = "GridCheck-Setup";
static const char* AP_IP = "192.168.4.1";

Settings settings() {
  std::lock_guard<std::mutex> g(lock);
  return S;
}
uint32_t settingsVersion() { return version; }
bool apMode() { return ap; }
bool connected() { return haveIp; }
std::string ssidInUse() {
  wifi_ap_record_t info;
  if (haveIp && esp_wifi_sta_get_ap_info(&info) == ESP_OK) return std::string((const char*)info.ssid);
  return "";
}
std::string address() {
  if (haveIp) return std::string("http://") + ipStr + "/";
  if (ap) return std::string("Join Wi-Fi \"") + AP_NAME + "\"";
  return "";
}

// ---- settings storage ------------------------------------------------------
static std::string getStr(nvs_handle_t h, const char* key, const std::string& def) {
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) return def;
  std::string v(len, '\0');
  nvs_get_str(h, key, v.data(), &len);
  v.resize(len ? len - 1 : 0);
  return v;
}

static void load() {
  nvs_handle_t h;
  if (nvs_open("eh", NVS_READONLY, &h) != ESP_OK) return;
  S.ssid = getStr(h, "ssid", "");
  S.pass = getStr(h, "pass", "");
  S.host = getStr(h, "host", "");
  nvs_get_u16(h, "port", &S.port);
  nvs_get_u8(h, "plant", &S.plantId);
  nvs_get_u8(h, "brand", &S.brand);
  if (S.brand >= BRAND_COUNT) S.brand = 0;
  uint8_t demo = 0;
  if (nvs_get_u8(h, "demo", &demo) == ESP_OK) S.demo = demo;
  S.tz = getStr(h, "tz", S.tz);
  nvs_get_u8(h, "bright", &S.brightness);
  size_t len = sizeof(Tariff);
  Tariff t;
  if (nvs_get_blob(h, "tou", &t, &len) == ESP_OK && len == sizeof(Tariff)) {
    for (auto& p : t.p) p.name[sizeof p.name - 1] = 0;
    t.baseName[sizeof t.baseName - 1] = 0;
    S.tariff = t;
  } else {
    // 0.5.0 and earlier: a buy price plus up to 4 unnamed windows
    struct OldWindow { bool used; uint16_t start, end; float price; };
    struct OldTariff { bool set; float buy, sell, supply; OldWindow win[4]; };
    OldTariff o;
    len = sizeof o;
    if (nvs_get_blob(h, "tariff", &o, &len) == ESP_OK && len == sizeof o) {
      S.tariff.set = o.set;
      S.tariff.buy = o.buy;
      S.tariff.sell = o.sell;
      S.tariff.supply = o.supply;
      for (int i = 0; i < 4; i++) {
        TouPeriod& p = S.tariff.p[i];
        p.used = o.win[i].used;
        p.start = o.win[i].start;
        p.end = o.win[i].end;
        p.price = o.win[i].price;
        snprintf(p.name, sizeof p.name, "%s", p.price == 0 ? "Free" : "Off-peak");
      }
    }
  }
  float f[3];
  len = sizeof f;
  if (nvs_get_blob(h, "fcst", f, &len) == ESP_OK && len == sizeof f) S.lat = f[0], S.lon = f[1], S.kwp = f[2];
  nvs_get_u8(h, "palette", &S.palette);
  if (S.palette > 2) S.palette = 0;
  nvs_get_u8(h, "theme", &S.theme);
  if (S.theme > 2) S.theme = 0;
  nvs_get_u8(h, "dimafter", &S.dimAfterMin);
  nvs_get_u8(h, "dimlevel", &S.dimLevel);
  uint8_t nd = 1;
  if (nvs_get_u8(h, "nightdim", &nd) == ESP_OK) S.nightDim = nd;
  nvs_get_u16(h, "nstart", &S.nightStart);
  nvs_get_u16(h, "nend", &S.nightEnd);
  S.pinSalt = getStr(h, "psalt", "");
  S.pinHash = getStr(h, "phash", "");
  S.updateUrl = getStr(h, "updurl", "");
  nvs_close(h);
}

static void save() {
  nvs_handle_t h;
  if (nvs_open("eh", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, "ssid", S.ssid.c_str());
  nvs_set_str(h, "pass", S.pass.c_str());
  nvs_set_str(h, "host", S.host.c_str());
  nvs_set_u16(h, "port", S.port);
  nvs_set_u8(h, "plant", S.plantId);
  nvs_set_u8(h, "brand", S.brand);
  nvs_set_u8(h, "demo", S.demo ? 1 : 0);
  nvs_set_str(h, "tz", S.tz.c_str());
  nvs_set_u8(h, "bright", S.brightness);
  nvs_set_blob(h, "tou", &S.tariff, sizeof(Tariff));
  const float f[3] = {S.lat, S.lon, S.kwp};
  nvs_set_blob(h, "fcst", f, sizeof f);
  nvs_set_u8(h, "palette", S.palette);
  nvs_set_u8(h, "theme", S.theme);
  nvs_set_u8(h, "dimafter", S.dimAfterMin);
  nvs_set_u8(h, "dimlevel", S.dimLevel);
  nvs_set_u8(h, "nightdim", S.nightDim ? 1 : 0);
  nvs_set_u16(h, "nstart", S.nightStart);
  nvs_set_u16(h, "nend", S.nightEnd);
  nvs_set_str(h, "psalt", S.pinSalt.c_str());
  nvs_set_str(h, "phash", S.pinHash.c_str());
  nvs_set_str(h, "updurl", S.updateUrl.c_str());
  nvs_commit(h);
  nvs_close(h);
  version++;
}

void setTheme(uint8_t theme) {
  std::lock_guard<std::mutex> g(lock);
  S.theme = theme % 3;
  save();
}

void setPalette(uint8_t palette) {
  std::lock_guard<std::mutex> g(lock);
  S.palette = palette % 3;
  save();
}

void setDimAfter(uint8_t minutes) {
  std::lock_guard<std::mutex> g(lock);
  S.dimAfterMin = minutes;
  save();
}

void factoryReset() {
  nvs_handle_t h;
  if (nvs_open("eh", NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
  }
  ESP_LOGW(TAG, "settings cleared, restarting");
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_restart();
}

// ---- PIN -------------------------------------------------------------------
static std::string hashPin(const std::string& pin, const std::string& salt) {
  uint8_t out[32];
  const std::string s = salt + ":" + pin;
  mbedtls_sha256((const uint8_t*)s.data(), s.size(), out, 0);
  for (int i = 0; i < 5000; i++) mbedtls_sha256(out, 32, out, 0);  // slow guessing down a little
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", out[i]);
  return hex;
}

/** "" when the PIN is accepted (or none is set), otherwise a message. Caller holds `lock`. */
static std::string checkPin(const std::string& pin) {
  if (S.pinHash.empty()) return "";
  if (millis() < pinLockedUntil) return "Too many wrong PINs. Wait " + std::to_string((pinLockedUntil - millis()) / 1000 + 1) + " seconds.";
  if (hashPin(pin, S.pinSalt) == S.pinHash) {
    pinFails = 0;
    return "";
  }
  pinFails++;
  if (pinFails >= 5) pinLockedUntil = millis() + 60000UL * (1UL << std::min(pinFails - 5, 4));
  return "Wrong PIN.";
}

// ---- pages -----------------------------------------------------------------
static std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

static const char* STYLE = R"CSS(
:root{--bg:#0b0a0d;--card:#18151b;--ink:#ffffff;--muted:#a39ca8;--line:#2c2830;--acc:#ff2d87;--bad:#ffb020}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 -apple-system,"Segoe UI",Roboto,Arial,sans-serif}
main{max-width:560px;margin:0 auto;padding:22px 16px 48px}h1{font-size:26px;margin:4px 0 4px}p.l{color:var(--muted);margin:0 0 18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
.card h2{font-size:13px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin:0 0 12px}
label{display:block;font-size:14px;font-weight:600;margin:10px 0 6px}
input,select{width:100%;height:46px;padding:0 12px;font:inherit;color:var(--ink);background:var(--bg);border:1px solid var(--line);border-radius:10px}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.chk{display:flex;gap:10px;align-items:center;margin-top:12px}.chk input{width:22px;height:22px}
button{width:100%;height:50px;margin-top:6px;border:0;border-radius:12px;background:var(--acc);color:#ffffff;font:inherit;font-weight:700;font-size:17px}
.help{color:var(--muted);font-size:14px;margin:8px 0 0}button.sec{background:var(--line)}button[hidden]{display:none}
input[type=file]{height:auto;padding:10px}.tou{border-top:1px solid var(--line);padding-top:10px;margin-top:10px}.tou[hidden]{display:none}progress{width:100%;height:10px;accent-color:var(--acc);margin-top:10px}.ok{color:var(--acc)}.bad{color:var(--bad)}.kv{display:flex;justify-content:space-between;gap:12px;padding:4px 0}
)CSS";

static const char* TZS[][2] = {
    {"AEST-10AEDT,M10.1.0,M4.1.0/3", "Sydney, Melbourne, Canberra, Hobart"},
    {"AEST-10", "Brisbane"},
    {"ACST-9:30ACDT,M10.1.0,M4.1.0/3", "Adelaide"},
    {"ACST-9:30", "Darwin"},
    {"AWST-8", "Perth"},
    {"NZST-12NZDT,M9.5.0,M4.1.0/3", "New Zealand"},
    {"UTC0", "UTC"},
};

static const char* OTA_JS = R"JS(
(()=>{const s=document.getElementById('brand');if(!s||typeof BRANDS==='undefined')return;
s.onchange=()=>{const b=BRANDS[s.value];document.getElementById('port').value=b[0];document.getElementById('plant').value=b[1];
document.getElementById('unitbox').hidden=!b[2];document.getElementById('unitlbl').textContent=b[2]||'Unit id';
document.getElementById('brandhelp').textContent=b[3];};})();
(()=>{const b=document.getElementById('restart'),m=document.getElementById('rmsg');if(!b)return;
b.onclick=async()=>{if(!confirm('Restart the screen? It takes about 20 seconds.'))return;const p=(document.getElementById('pin')||{}).value||'';
try{const r=await fetch('/restart',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+encodeURIComponent(p)});
const j=await r.json();if(!r.ok){m.textContent=j.error||'Something went wrong.';return;}m.textContent='Restarting... this page reloads in 25 seconds.';setTimeout(()=>location.reload(),25000);}
catch(e){m.textContent='The screen did not answer.';}};})();
(()=>{const a=document.getElementById('touadd');if(a){const upd=()=>{a.hidden=!document.querySelector('.tou[hidden]');};
a.onclick=()=>{const r=document.querySelector('.tou[hidden]');if(r){r.hidden=false;r.querySelector('input').focus();}upd();};upd();}})();
(()=>{const $=i=>document.getElementById(i),pin=()=>($('pin')||{}).value||'';let timer,latest='';
const busy=s=>s=='downloading'||s=='writing';
function show(j){latest=j.latest||'';$('olatest').textContent=j.latest||(j.state=='checking'?'checking...':'not checked yet');
$('omsg').textContent=j.state=='checking'?'Checking...':(j.message||'');$('oprog').hidden=!busy(j.state);$('oprog').value=j.percent||0;
$('oinstall').hidden=!j.available||busy(j.state)||j.state=='done';if(j.available)$('oinstall').textContent='Install version '+j.latest;}
function restarting(){clearInterval(timer);$('omsg').textContent='Update installed. The screen is restarting - this page reloads in 30 seconds.';$('oprog').hidden=true;setTimeout(()=>location.reload(),30000);}
async function status(){const r=await fetch('/ota/status');const j=await r.json();show(j);return j;}
function poll(){clearInterval(timer);timer=setInterval(async()=>{try{const j=await status();if(j.state=='done')restarting();else if(!busy(j.state)&&j.state!='checking')clearInterval(timer);}catch(e){restarting();}},1500);}
async function post(u){try{const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'pin='+encodeURIComponent(pin())});
const j=await r.json();if(!r.ok){$('omsg').textContent=j.error||'Something went wrong.';return false;}show(j);return true;}catch(e){$('omsg').textContent='The screen did not answer.';return false;}}
$('ocheck').onclick=async()=>{if(await post('/ota/check'))poll();};
$('oinstall').onclick=async()=>{if(confirm('Install version '+latest+'? The screen restarts when it is done (about a minute).')&&await post('/ota/install'))poll();};
$('oupload').onclick=()=>{const f=$('ofile').files[0];if(!f){$('omsg').textContent='Choose the firmware file first.';return;}
if(!confirm('Install '+f.name+'? The screen restarts when it is done.'))return;
const x=new XMLHttpRequest();x.open('POST','/update');x.setRequestHeader('X-PIN',pin());x.setRequestHeader('Content-Type','application/octet-stream');
$('oprog').hidden=false;$('oprog').value=0;$('omsg').textContent='Sending '+f.name+'...';
x.upload.onprogress=e=>{if(e.lengthComputable)$('oprog').value=Math.round(e.loaded*100/e.total);};
x.onload=()=>{let j={};try{j=JSON.parse(x.responseText);}catch(e){}if(x.status==200)restarting();else{$('oprog').hidden=true;$('omsg').textContent=j.error||'Update failed.';}};
x.onerror=()=>{$('oprog').hidden=true;$('omsg').textContent='The upload was cut off. The current version keeps running.';};x.send(f);};
status().then(j=>{if(busy(j.state)||j.state=='checking')poll();}).catch(()=>{});})();
)JS";

static std::string page(const std::string& message, bool good) {
  const Live l = liveGet();
  const Settings s = settings();
  std::string h;
  h.reserve(8000);
  h += "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>GridCheck</title><style>";
  h += STYLE;
  h += "</style></head><body><main><h1>GridCheck</h1><p class=l>Settings for the wall screen.</p>";
  if (!message.empty()) h += std::string("<div class='card ") + (good ? "ok" : "bad") + "'>" + esc(message) + "</div>";

  h += "<div class=card><h2>Status</h2>";
  h += "<div class=kv><span>Wi-Fi</span><b>" + (haveIp ? esc(ssidInUse()) + " (" + ipStr + ")" : std::string("not connected")) + "</b></div>";
  h += std::string("<div class=kv><span>Inverter</span><b class=") + (l.ok ? "ok" : "bad") + ">" +
       (s.demo ? "demo data" : l.ok ? "connected" : !s.host.empty() ? "not reachable" : "not set up") + "</b></div>";
  if (!l.ok && l.error[0]) h += "<p class=help>" + esc(l.error) + "</p>";
  if (l.ok) {
    char b[200];
    snprintf(b, sizeof b, "<div class=kv><span>Now</span><b>solar %.2f kW, home %.2f kW, battery %.0f%%</b></div>", l.solar, l.house, l.soc);
    h += b;
  }
  h += "<div class=kv><span>Firmware</span><b>" EH_VERSION "</b></div>"
       "<button type=button class=sec id=restart>Restart screen</button><p class=help id=rmsg>Settings and history are kept.";
  if (!s.pinHash.empty()) h += " Fill in your current PIN (at the bottom) first.";
  h += "</p></div>";

  h += "<form method=post action=/save>";
  h += "<div class=card><h2>Wi-Fi</h2><label for=ssid>Network</label><input id=ssid name=ssid list=nets autocomplete=off autocapitalize=off value='" +
       esc(s.ssid) + "'><datalist id=nets>";
  {
    std::lock_guard<std::mutex> g(lock);
    for (const auto& n : networks) h += "<option value='" + esc(n) + "'>";
  }
  h += "</datalist><label for=pass>Password</label><input id=pass name=pass type=password placeholder='";
  h += s.pass.empty() ? "Wi-Fi password" : "Saved - leave blank to keep";
  h += "'></div>";

  {
    const BrandInfo& b = BRANDS[s.brand < BRAND_COUNT ? s.brand : 0];
    h += "<div class=card><h2>Inverter</h2><label for=brand>Brand</label><select id=brand name=brand>";
    for (int i = 0; i < BRAND_COUNT; i++) h += "<option value=" + std::to_string(i) + (s.brand == i ? " selected" : "") + ">" + BRANDS[i].name + "</option>";
    h += "</select><label for=host>IP address</label><input id=host name=host inputmode=decimal autocomplete=off placeholder='192.168.1.50' value='" + esc(s.host) +
         "'><div class=row><div><label for=port>Port</label><input id=port name=port inputmode=numeric value='" + std::to_string(s.port) +
         "'></div><div id=unitbox" + (b.unitName[0] ? "" : " hidden") + "><label for=plant id=unitlbl>" + (b.unitName[0] ? b.unitName : "Unit id") +
         "</label><input id=plant name=plant inputmode=numeric value='" + std::to_string(s.plantId) +
         "'></div></div><div class=chk><input type=checkbox id=demo name=demo value=1" + (s.demo ? " checked" : "") +
         "><label for=demo style='margin:0'>Show demo data instead</label></div><p class=help id=brandhelp>" + b.howTo + "</p></div>";
    // brand defaults for the page script: [port, unit, unit name, how-to]
    h += "<script>const BRANDS=[";
    for (int i = 0; i < BRAND_COUNT; i++) {
      h += std::string(i ? "," : "") + "[" + std::to_string(BRANDS[i].port) + "," + std::to_string(BRANDS[i].unit) + ",\"" + BRANDS[i].unitName + "\",\"" +
           BRANDS[i].howTo + "\"]";
    }
    h += "];</script>";
  }

  h += "<div class=card><h2>Screen</h2><label for=tz>Time zone</label><select id=tz name=tz>";
  for (auto& z : TZS) h += std::string("<option value='") + z[0] + "'" + (s.tz == z[0] ? " selected" : "") + ">" + z[1] + "</option>";
  h += "</select><label for=palette>Colours</label><select id=palette name=palette>";
  for (int i = 0; i < 3; i++) h += "<option value=" + std::to_string(i) + (s.palette == i ? " selected" : "") + ">" + PALETTE_NAMES[i] + "</option>";
  h += "</select><label for=theme>Theme</label><select id=theme name=theme>";
  static const char* THEME_TEXT[3] = {"Dark", "Light", "Auto: light by day, dark during the night hours below"};
  for (int i = 0; i < 3; i++) h += "<option value=" + std::to_string(i) + (s.theme == i ? " selected" : "") + ">" + THEME_TEXT[i] + "</option>";
  h += "</select><div class=row><div><label for=bright>Brightness (%)</label><input id=bright name=bright inputmode=numeric value='" + std::to_string(s.brightness) +
       "'></div><div><label for=dimlevel>Dimmed brightness (%)</label><input id=dimlevel name=dimlevel inputmode=numeric value='" + std::to_string(s.dimLevel) +
       "'></div></div><label for=dimafter>Dim the screen</label><select id=dimafter name=dimafter>";
  for (uint8_t m : DIM_CHOICES) {
    const std::string text = m == 0 ? "Never (always on)" : "After " + std::to_string(m) + (m == 1 ? " minute" : " minutes") + " without a touch";
    h += "<option value=" + std::to_string(m) + (s.dimAfterMin == m ? " selected" : "") + ">" + text + "</option>";
  }
  char ns[16], ne[16];
  snprintf(ns, sizeof ns, "%02u:%02u", s.nightStart / 60, s.nightStart % 60);
  snprintf(ne, sizeof ne, "%02u:%02u", s.nightEnd / 60, s.nightEnd % 60);
  h += std::string("</select><div class=chk><input type=checkbox id=nightdim name=nightdim value=1") + (s.nightDim ? " checked" : "") +
       "><label for=nightdim style='margin:0'>Also dim overnight</label></div><div class=row><div><label for=nstart>From</label><input type=time id=nstart name=nstart value='" + ns +
       "'></div><div><label for=nend>Until</label><input type=time id=nend name=nend value='" + ne +
       "'></div></div><p class=help>Touching the screen brings it back to full brightness. Dimmed brightness 0 turns the backlight off.</p></div>";

  {
    const Tariff& t = s.tariff;
    auto num = [](float v) {
      char b[16];
      snprintf(b, sizeof b, "%g", v);
      return std::string(b);
    };
    auto hm = [](uint16_t m) {
      char b[8];
      snprintf(b, sizeof b, "%02u:%02u", m / 60, m % 60);
      return std::string(b);
    };
    h += "<div class=card><h2>Power prices</h2><div class=row><div><label for=sell>Feed-in price (c/kWh)</label><input id=sell name=sell inputmode=decimal value='" +
         num(t.sell) + "'></div><div><label for=supply>Supply charge (c/day)</label><input id=supply name=supply inputmode=decimal placeholder='optional' value='" +
         (t.supply > 0 ? num(t.supply) : std::string("")) + "'></div></div>";
    h += "<label style='margin-top:16px'>Time of use (optional)</label>"
         "<p class=help style='margin:0 0 8px'>Add a period for each price on your bill, e.g. Off-peak, Shoulder and Peak, or a free window at 0c. "
         "Times can run past midnight (22:00 to 07:00). Clear a period's name and times to remove it.</p><datalist id=tounames>"
         "<option value='Off-peak'><option value='Shoulder'><option value='Peak'><option value='Free'><option value='Super off-peak'></datalist>";
    int shown = 0;
    for (int i = 0; i < TOU_MAX; i++)
      if (t.p[i].used) shown = i + 1;
    for (int i = 0; i < TOU_MAX; i++) {
      const TouPeriod& p = t.p[i];
      const std::string n = std::to_string(i);
      const bool hide = !p.used && i > shown;  // one empty row is always shown
      h += "<div class=tou id=tou" + n + (hide ? " hidden" : "") + "><div class=row style='grid-template-columns:3fr 2fr'><input name=tn" + n +
           " list=tounames maxlength=15 placeholder='Name, e.g. Peak' aria-label='Period " + std::to_string(i + 1) + " name' value='" + (p.used ? esc(p.name) : "") +
           "'><input name=tp" + n + " inputmode=decimal placeholder='c/kWh' aria-label='price' value='" + (p.used ? num(p.price) : "") +
           "'></div><div class=row style='grid-template-columns:1fr 1fr 1.3fr;margin-top:6px'><input type=time name=ts" + n + " aria-label='from' value='" +
           (p.used ? hm(p.start) : "") + "'><input type=time name=te" + n + " aria-label='to' value='" + (p.used ? hm(p.end) : "") + "'><select name=td" + n +
           " aria-label='days'>";
      static const char* DAYS[] = {"Every day", "Weekdays", "Weekends"};
      for (int d = 0; d < 3; d++) h += "<option value=" + std::to_string(d) + (p.days == d ? " selected" : "") + ">" + DAYS[d] + "</option>";
      h += "</select></div></div>";
    }
    h += "<button type=button class=sec id=touadd>Add another period</button>"
         "<label style='margin-top:16px'>All other times</label><div class=row style='grid-template-columns:3fr 2fr'><input name=bn list=tounames maxlength=15 "
         "placeholder='Name, e.g. Shoulder' aria-label='Name for all other times' value='" + esc(t.baseName) + "'><input id=buy name=buy inputmode=decimal "
         "aria-label='Price at all other times, c/kWh' placeholder='c/kWh' value='" + num(t.buy) + "'></div>"
         "<p class=help>No time of use? Just set this one price and leave the periods empty. Prices include GST, as on your bill. Costs are counted with the price in force at each minute.</p></div>";

    h += "<div class=card><h2>Solar forecast (optional)</h2><div class=row><div><label for=lat>Latitude</label><input id=lat name=lat inputmode=decimal placeholder='-33.87' value='" +
         (std::isnan(s.lat) ? std::string("") : num(s.lat)) + "'></div><div><label for=lon>Longitude</label><input id=lon name=lon inputmode=decimal placeholder='151.21' value='" +
         (std::isnan(s.lon) ? std::string("") : num(s.lon)) + "'></div></div><label for=kwp>Solar panel size (kW)</label><input id=kwp name=kwp inputmode=decimal placeholder='6.6' value='" +
         (s.kwp > 0 ? num(s.kwp) : std::string("")) +
         "'><p class=help>Needs internet: the screen asks Open-Meteo (free, no account) for the day's sunshine once an hour. Leave blank to compare with your 7-day average. Your inverter data never leaves your network.</p></div>";
  }
  h += "<div class=card><h2>Firmware</h2><div class=kv><span>On this screen</span><b>" EH_VERSION "</b></div>"
       "<div class=kv><span>Newest online</span><b id=olatest>-</b></div><p class=help id=omsg></p><progress id=oprog max=100 hidden></progress>"
       "<button type=button class=sec id=ocheck>Check for updates</button><button type=button id=oinstall hidden>Install update</button>"
       "<label for=ofile style='margin-top:16px'>Or install a firmware file (ends in -app.bin)</label><input type=file id=ofile accept='.bin'>"
       "<button type=button class=sec id=oupload>Install file</button>"
       "<label for=updurl style='margin-top:16px'>Update address (leave blank for the normal one)</label><input id=updurl name=updurl autocapitalize=off autocomplete=off placeholder='" +
       std::string(ota::DEFAULT_URL) + "' value='" + esc(s.updateUrl) +
       "'><p class=help>The screen looks for new versions twice a day but only installs one when you press Install (here or on the screen's Settings &amp; info page). "
       "Only firmware signed by GridCheck is accepted. Your settings and history are kept. If a new version won't start, the screen goes back to the old one by itself.";
  if (!s.pinHash.empty()) h += " Fill in your current PIN below first.";
  h += "</p></div>";
  h += "<div class=card><h2>Settings PIN</h2>";
  if (!s.pinHash.empty()) h += "<label for=pin>Current PIN (needed to save)</label><input id=pin name=pin type=password inputmode=numeric autocomplete=off>";
  h += std::string("<label for=newpin>") + (s.pinHash.empty() ? "Set a PIN (optional, 4-8 digits)" : "New PIN (optional, 4-8 digits; 0000 removes it)") +
       "</label><input id=newpin name=newpin type=password inputmode=numeric autocomplete=off>"
       "<p class=help>Forgot the PIN? On the screen, swipe to the Settings &amp; info page and hold <b>Reset all</b> for 5 seconds.</p></div>";
  h += "<button type=submit>Save</button></form></main><script>";
  h += OTA_JS;
  h += "</script></body></html>";
  return h;
}

// ---- form parsing ------------------------------------------------------------
static std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else o += s[i];
  }
  return o;
}

static std::string formField(const std::string& body, const char* name) {
  const std::string key = std::string(name) + "=";
  size_t p = 0;
  while (p < body.size()) {
    size_t e = body.find('&', p);
    if (e == std::string::npos) e = body.size();
    if (body.compare(p, key.size(), key) == 0) return urlDecode(body.substr(p + key.size(), e - p - key.size()));
    p = e + 1;
  }
  return "";
}

static bool hasField(const std::string& body, const char* name) {
  const std::string key = std::string(name) + "=";
  return body.compare(0, key.size(), key) == 0 || body.find("&" + key) != std::string::npos;
}

static std::string trim(std::string s) {
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  size_t i = 0;
  while (i < s.size() && isspace((unsigned char)s[i])) i++;
  return s.substr(i);
}

// ---- HTTP handlers -------------------------------------------------------------
static esp_err_t sendPage(httpd_req_t* req, const char* status, const std::string& msg, bool good) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  const std::string h = page(msg, good);
  return httpd_resp_send(req, h.data(), h.size());
}

static esp_err_t handleRoot(httpd_req_t* req) { return sendPage(req, "200 OK", "", true); }

void restart() {
  ESP_LOGI(TAG, "restart requested");
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_restart();
}

static void restartTask(void*);
/** Restart in a couple of seconds (after the reply is sent); right away if there's no memory for that. */
static void restartSoon() {
  if (xTaskCreate(restartTask, "restart", 2048, nullptr, 1, nullptr) != pdPASS) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
  }
}

static void restartTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
}

static esp_err_t handleSave(httpd_req_t* req) {
  if (req->content_len > 4000) return sendPage(req, "400 Bad Request", "Form too large.", false);
  std::string body(req->content_len, '\0');
  size_t got = 0;
  while (got < body.size()) {
    const int r = httpd_req_recv(req, body.data() + got, body.size() - got);
    if (r <= 0) return ESP_FAIL;
    got += r;
  }

  std::string ssid = trim(formField(body, "ssid")), host = trim(formField(body, "host"));
  const long port = strtol(formField(body, "port").c_str(), nullptr, 10);
  const long plant = strtol(formField(body, "plant").c_str(), nullptr, 10);
  const long bright = strtol(formField(body, "bright").c_str(), nullptr, 10);
  const std::string newPin = formField(body, "newpin");
  if (port < 1 || port > 65535) return sendPage(req, "400 Bad Request", "Port should be a number like 502.", false);
  // prices
  auto parseNum = [&](const char* name, float def, float lo, float hi, float& out) -> bool {
    const std::string v = trim(formField(body, name));
    if (v.empty()) { out = def; return true; }
    char* end = nullptr;
    out = strtof(v.c_str(), &end);
    return end && *end == 0 && out >= lo && out <= hi;
  };
  auto parseHm = [](const std::string& v, uint16_t& out) -> bool {
    int hh, mm;
    if (sscanf(v.c_str(), "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    out = (uint16_t)(hh * 60 + mm);
    return true;
  };
  Tariff tariff;
  tariff.set = true;
  if (!parseNum("buy", 30, 0, 500, tariff.buy)) return sendPage(req, "400 Bad Request", "Buy price should be a number of cents, e.g. 32.5.", false);
  if (!parseNum("sell", 5, 0, 500, tariff.sell)) return sendPage(req, "400 Bad Request", "Feed-in price should be a number of cents, e.g. 6.", false);
  if (!parseNum("supply", 0, 0, 1000, tariff.supply)) return sendPage(req, "400 Bad Request", "Supply charge should be cents per day, e.g. 110.", false);
  snprintf(tariff.baseName, sizeof tariff.baseName, "%s", trim(formField(body, "bn")).c_str());
  int nPeriods = 0;
  for (int i = 0; i < TOU_MAX; i++) {
    const std::string n = std::to_string(i);
    const std::string name = trim(formField(body, ("tn" + n).c_str()));
    const std::string ts = trim(formField(body, ("ts" + n).c_str())), te = trim(formField(body, ("te" + n).c_str()));
    const std::string tp = trim(formField(body, ("tp" + n).c_str()));
    if (name.empty() && ts.empty() && te.empty() && tp.empty()) continue;
    TouPeriod& p = tariff.p[nPeriods];
    const std::string label = name.empty() ? "Period " + std::to_string(i + 1) : name;
    if (!parseHm(ts, p.start) || !parseHm(te, p.end) || p.start == p.end)
      return sendPage(req, "400 Bad Request", label + ": enter a start and end time, e.g. 15:00 to 21:00.", false);
    float price;
    if (tp.empty() || !parseNum(("tp" + n).c_str(), 0, 0, 500, price))
      return sendPage(req, "400 Bad Request", label + ": the price should be cents per kWh (0 = free).", false);
    p.price = price;
    p.days = (uint8_t)constrain(strtol(formField(body, ("td" + n).c_str()).c_str(), nullptr, 10), 0L, 2L);
    snprintf(p.name, sizeof p.name, "%s", label.c_str());
    p.used = true;
    nPeriods++;
  }
  // periods must not overlap: check every minute of the week
  for (int d = 0; d < 7; d++)
    for (int m = 0; m < 1440; m++) {
      int hit = -1;
      for (int i = 0; i < nPeriods; i++) {
        Tariff one;
        one.p[0] = tariff.p[i];
        if (periodAt(one, m, d) != 0) continue;
        if (hit >= 0) {
          static const char* DAY[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
          char at[48];
          snprintf(at, sizeof at, " overlap at %02d:%02d on %s.", m / 60, m % 60, DAY[d]);
          return sendPage(req, "400 Bad Request", std::string(tariff.p[hit].name) + " and " + tariff.p[i].name + at, false);
        }
        hit = i;
      }
    }
  // screen
  const long palette = strtol(formField(body, "palette").c_str(), nullptr, 10);
  const long dimAfter = strtol(formField(body, "dimafter").c_str(), nullptr, 10);
  const long dimLevel = strtol(formField(body, "dimlevel").c_str(), nullptr, 10);
  uint16_t nStart = 20 * 60, nEnd = 7 * 60;
  const bool nightDim = hasField(body, "nightdim");
  if (nightDim && (!parseHm(trim(formField(body, "nstart")), nStart) || !parseHm(trim(formField(body, "nend")), nEnd) || nStart == nEnd))
    return sendPage(req, "400 Bad Request", "Night dimming: enter a start and end time, e.g. 20:00 until 07:00.", false);
  if (dimLevel < 0 || dimLevel > 100) return sendPage(req, "400 Bad Request", "Dimmed brightness should be 0 to 100 %.", false);
  // forecast
  float lat = NAN, lon = NAN, kwp = 0;
  const bool anyFc = !trim(formField(body, "lat")).empty() || !trim(formField(body, "lon")).empty() || !trim(formField(body, "kwp")).empty();
  if (anyFc) {
    if (!parseNum("lat", NAN, -90, 90, lat) || !parseNum("lon", NAN, -180, 180, lon) || std::isnan(lat) || std::isnan(lon))
      return sendPage(req, "400 Bad Request", "Latitude and longitude look wrong. Find them on a map, e.g. -33.87 and 151.21.", false);
    if (!parseNum("kwp", 0, 0.1f, 200, kwp) || kwp <= 0) return sendPage(req, "400 Bad Request", "Solar panel size should be in kW, e.g. 6.6.", false);
  }
  const long brand = strtol(formField(body, "brand").c_str(), nullptr, 10);
  if (brand < 0 || brand >= BRAND_COUNT) return sendPage(req, "400 Bad Request", "Choose your inverter brand.", false);
  if (plant < 0 || plant > 255)
    return sendPage(req, "400 Bad Request", std::string("The unit id should be 0-255 (usually ") + std::to_string(BRANDS[brand].unit) + " for " + BRANDS[brand].name + ").", false);
  if (!newPin.empty() && newPin != "0000") {
    bool digits = newPin.size() >= 4 && newPin.size() <= 8;
    for (char c : newPin) digits = digits && isdigit((unsigned char)c);
    if (!digits) return sendPage(req, "400 Bad Request", "The PIN must be 4 to 8 digits.", false);
  }

  std::string pinErr;
  {
    std::lock_guard<std::mutex> g(lock);
    pinErr = checkPin(formField(body, "pin"));
  }
  if (!pinErr.empty()) return sendPage(req, "403 Forbidden", pinErr, false);

  bool wifiChanged;
  {
    std::lock_guard<std::mutex> g(lock);
    const std::string oldSsid = S.ssid, oldPass = S.pass;
    S.ssid = ssid;
    const std::string pass = formField(body, "pass");
    if (!pass.empty()) S.pass = pass;
    else if (ssid != oldSsid) S.pass = "";
    S.host = host;
    S.port = (uint16_t)port;
    S.plantId = (uint8_t)plant;
    S.brand = (uint8_t)brand;
    S.demo = hasField(body, "demo");
    const std::string tz = formField(body, "tz");
    if (!tz.empty()) S.tz = tz;
    S.brightness = (uint8_t)constrain(bright, 5L, 100L);
    S.tariff = tariff;
    S.palette = (uint8_t)constrain(palette, 0L, 2L);
    if (hasField(body, "theme")) S.theme = (uint8_t)constrain(strtol(formField(body, "theme").c_str(), nullptr, 10), 0L, 2L);
    S.dimAfterMin = (uint8_t)constrain(dimAfter, 0L, 60L);
    S.dimLevel = (uint8_t)dimLevel;
    S.nightDim = nightDim;
    if (nightDim) S.nightStart = nStart, S.nightEnd = nEnd;
    S.lat = lat;
    S.lon = lon;
    S.kwp = kwp;
    if (hasField(body, "updurl")) {
      const std::string u = trim(formField(body, "updurl"));
      S.updateUrl = u == ota::DEFAULT_URL ? "" : u;
    }
    if (newPin == "0000") {
      S.pinSalt.clear();
      S.pinHash.clear();
    } else if (!newPin.empty()) {
      char salt[17];
      snprintf(salt, sizeof salt, "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
      S.pinSalt = salt;
      S.pinHash = hashPin(newPin, S.pinSalt);
    }
    save();
    wifiChanged = S.ssid != oldSsid || S.pass != oldPass;
    setenv("TZ", S.tz.c_str(), 1);
    tzset();
  }
  if (wifiChanged) {
    restartSoon();
    return sendPage(req, "200 OK",
                    "Saved. The screen is restarting to join \"" + ssid +
                        "\". Reconnect your phone to your home Wi-Fi, then open the address shown at the bottom of the screen.",
                    true);
  }
  return sendPage(req, "200 OK", "Saved.", true);
}

static esp_err_t handleLive(httpd_req_t* req) {
  const Live l = liveGet();
  char b[420];
  auto num = [](float v) { return std::isnan(v) ? -1.0f : v; };
  snprintf(b, sizeof b,
           "{\"ok\":%s,\"demo\":%s,\"ageSeconds\":%lu,\"power\":{\"solar\":%.3f,\"grid\":%.3f,\"battery\":%.3f,\"house\":%.3f},"
           "\"battery\":{\"soc\":%.1f},\"today\":{\"solar\":%.2f,\"house\":%.2f},\"yesterday\":{\"solar\":%.2f}}",
           l.ok ? "true" : "false", l.demo ? "true" : "false", (unsigned long)((millis() - l.updatedMs) / 1000), l.solar, l.grid,
           l.battery, l.house, num(l.soc), num(l.solarToday), num(l.houseToday), num(l.solarYesterday));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, b);
}

// ---- firmware updates ------------------------------------------------------------
static std::string jsonEsc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\', o += c;
    else if ((unsigned char)c >= 0x20) o += c;
  }
  return o;
}

static esp_err_t sendJson(httpd_req_t* req, const char* status, const std::string& body) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body.data(), body.size());
}

static esp_err_t sendError(httpd_req_t* req, const char* status, const std::string& msg) {
  return sendJson(req, status, "{\"error\":\"" + jsonEsc(msg) + "\"}");
}

static esp_err_t sendOtaStatus(httpd_req_t* req) {
  static const char* NAMES[] = {"idle", "checking", "downloading", "writing", "done", "failed"};
  const ota::Status o = ota::status();
  const std::string body = std::string("{\"version\":\"" EH_VERSION "\",\"latest\":\"") + jsonEsc(o.latest) + "\",\"available\":" +
                           (ota::updateAvailable() ? "true" : "false") + ",\"state\":\"" + NAMES[(int)o.state] + "\",\"percent\":" +
                           std::to_string(o.percent) + ",\"message\":\"" + jsonEsc(o.message) + "\"}";
  return sendJson(req, "200 OK", body);
}

/** "" when the request may change things (right PIN, or no PIN set). */
static std::string otaPin(const std::string& pin) {
  std::lock_guard<std::mutex> g(lock);
  return checkPin(pin);
}

static std::string readSmallBody(httpd_req_t* req) {
  std::string body;
  if (req->content_len > 200) return body;
  body.resize(req->content_len);
  size_t got = 0;
  while (got < body.size()) {
    const int n = httpd_req_recv(req, body.data() + got, body.size() - got);
    if (n <= 0) return "";
    got += n;
  }
  return body;
}

static esp_err_t handleOtaStatus(httpd_req_t* req) { return sendOtaStatus(req); }

static esp_err_t handleRestart(httpd_req_t* req) {
  const std::string err = otaPin(formField(readSmallBody(req), "pin"));
  if (!err.empty()) return sendError(req, "403 Forbidden", err);
  ESP_LOGI(TAG, "restart from the phone page");
  const esp_err_t r = sendJson(req, "200 OK", "{\"ok\":true}");
  restartSoon();
  return r;
}

static esp_err_t handleOtaCheck(httpd_req_t* req) {
  const std::string err = otaPin(formField(readSmallBody(req), "pin"));
  if (!err.empty()) return sendError(req, "403 Forbidden", err);
  if (!connected()) return sendError(req, "409 Conflict", "The screen isn't on your home Wi-Fi, so it can't look online.");
  ota::checkNow();
  return sendOtaStatus(req);
}

static esp_err_t handleOtaInstall(httpd_req_t* req) {
  std::string err = otaPin(formField(readSmallBody(req), "pin"));
  if (!err.empty()) return sendError(req, "403 Forbidden", err);
  if (!ota::installOnline(err)) return sendError(req, "409 Conflict", err);
  return sendOtaStatus(req);
}

static esp_err_t handleUpload(httpd_req_t* req) {
  char pinBuf[24] = "";
  httpd_req_get_hdr_value_str(req, "X-PIN", pinBuf, sizeof pinBuf);
  std::string err = otaPin(pinBuf);
  if (!err.empty()) return sendError(req, "403 Forbidden", err);
  // The setup hotspot has no password, so never take firmware through it.
  if (!connected()) return sendError(req, "409 Conflict", "Connect the screen to your home Wi-Fi first, then install updates from there.");
  if (!ota::uploadBegin(req->content_len, err)) return sendError(req, "400 Bad Request", err);
  ESP_LOGI(TAG, "firmware upload, %u bytes", (unsigned)req->content_len);
  const size_t CHUNK = 4096;
  char* buf = (char*)malloc(CHUNK);
  size_t left = req->content_len;
  bool ok = buf != nullptr;
  if (!ok) err = "out of memory";
  int timeouts = 0;
  while (ok && left > 0) {
    const int n = httpd_req_recv(req, buf, std::min(left, CHUNK));
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
    if (n <= 0) {
      ok = false;
      err = "the upload was cut off";
      break;
    }
    timeouts = 0;
    ok = ota::uploadWrite(buf, n, err);
    left -= n;
  }
  free(buf);
  if (!ok) {
    ota::uploadAbort();
    ESP_LOGW(TAG, "upload failed: %s", err.c_str());
    return sendError(req, "400 Bad Request", err);
  }
  if (!ota::uploadEnd(err)) return sendError(req, "400 Bad Request", err);
  ESP_LOGI(TAG, "firmware uploaded, restarting");
  const esp_err_t r = sendOtaStatus(req);
  restartSoon();
  return r;
}

// Anything else (including phones' captive-portal checks) goes to the settings page.
static esp_err_t redirectHome(httpd_req_t* req, httpd_err_code_t) {
  const std::string loc = std::string("http://") + (ap && !haveIp ? AP_IP : ipStr) + "/";
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", loc.c_str());
  return httpd_resp_send(req, nullptr, 0);
}

static void startHttp() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 10240;
  cfg.lru_purge_enable = true;
  cfg.max_uri_handlers = 12;
  cfg.recv_wait_timeout = 10;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "web server didn't start");
    return;
  }
  httpd_uri_t root = {"/", HTTP_GET, handleRoot, nullptr};
  httpd_uri_t save = {"/save", HTTP_POST, handleSave, nullptr};
  httpd_uri_t live = {"/api/live", HTTP_GET, handleLive, nullptr};
  httpd_uri_t ostatus = {"/ota/status", HTTP_GET, handleOtaStatus, nullptr};
  httpd_uri_t ocheck = {"/ota/check", HTTP_POST, handleOtaCheck, nullptr};
  httpd_uri_t oinstall = {"/ota/install", HTTP_POST, handleOtaInstall, nullptr};
  httpd_uri_t upload = {"/update", HTTP_POST, handleUpload, nullptr};
  httpd_uri_t rst = {"/restart", HTTP_POST, handleRestart, nullptr};
  httpd_register_uri_handler(server, &root);
  httpd_register_uri_handler(server, &save);
  httpd_register_uri_handler(server, &live);
  httpd_register_uri_handler(server, &ostatus);
  httpd_register_uri_handler(server, &ocheck);
  httpd_register_uri_handler(server, &oinstall);
  httpd_register_uri_handler(server, &upload);
  httpd_register_uri_handler(server, &rst);
  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirectHome);
}

// ---- captive-portal DNS: answer every name with our own address --------------
static void dnsTask(void*) {
  const int sock = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(sock, (sockaddr*)&addr, sizeof addr);
  uint8_t buf[512];
  for (;;) {
    sockaddr_in from;
    socklen_t fl = sizeof from;
    const int n = recvfrom(sock, buf, sizeof buf - 16, 0, (sockaddr*)&from, &fl);
    if (n < 12 || !ap) continue;
    // Find the end of the question (name labels + type + class).
    int p = 12;
    while (p < n && buf[p]) p += buf[p] + 1;
    p += 5;
    if (p > n) continue;
    buf[2] = 0x81;  // response, recursion desired
    buf[3] = 0x80;  // recursion available, no error
    buf[6] = 0, buf[7] = 1;   // one answer
    buf[8] = buf[9] = buf[10] = buf[11] = 0;
    const uint8_t ans[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
    memcpy(buf + p, ans, sizeof ans);
    sendto(sock, buf, p + sizeof ans, 0, (sockaddr*)&from, fl);
  }
}

// ---- Wi-Fi -----------------------------------------------------------------
static void scan() {
  wifi_scan_config_t sc = {};
  if (esp_wifi_scan_start(&sc, true) != ESP_OK) return;
  uint16_t n = 20;
  wifi_ap_record_t recs[20];
  esp_wifi_scan_get_ap_records(&n, recs);
  std::lock_guard<std::mutex> g(lock);
  networks.clear();
  for (int i = 0; i < n; i++) {
    const std::string s((const char*)recs[i].ssid);
    if (!s.empty() && std::find(networks.begin(), networks.end(), s) == networks.end()) networks.push_back(s);
  }
}

static void startAP(bool noWifi) {
  if (ap) return;
  ap = true;
  apBecauseNoWifi = noWifi;
  esp_wifi_set_mode(WIFI_MODE_APSTA);
  wifi_config_t c = {};
  strcpy((char*)c.ap.ssid, AP_NAME);
  c.ap.ssid_len = strlen(AP_NAME);
  c.ap.channel = 1;
  c.ap.max_connection = 4;
  c.ap.authmode = WIFI_AUTH_OPEN;
  esp_wifi_set_config(WIFI_IF_AP, &c);
  ESP_LOGI(TAG, "setup hotspot \"%s\" at %s", AP_NAME, AP_IP);
}

static void stopAP() {
  if (!ap) return;
  esp_wifi_set_mode(WIFI_MODE_STA);
  ap = false;
  ESP_LOGI(TAG, "setup hotspot closed");
}

static void onEvent(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (!settings().ssid.empty()) esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (haveIp) ESP_LOGW(TAG, "Wi-Fi lost, reconnecting");
    if (haveIp) staStartMs = millis();
    haveIp = false;
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (!settings().ssid.empty()) esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto* e = (ip_event_got_ip_t*)data;
    snprintf(ipStr, sizeof ipStr, IPSTR, IP2STR(&e->ip_info.ip));
    haveIp = true;
    connectedSinceMs = millis();
    ESP_LOGI(TAG, "connected: %s", ipStr);
    if (!sntpStarted) {
      esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
      esp_netif_sntp_init(&sc);
      sntpStarted = true;
    }
  }
}

static void watchTask(void*) {
  for (;;) {
    const Settings s = settings();
    // Couldn't join for 45 s: open the setup hotspot too, so it can be fixed from a phone.
    if (!haveIp && !ap && !s.ssid.empty() && millis() - staStartMs > 45000) {
      scan();
      startAP(false);
    }
    // Joined home Wi-Fi again: close a hotspot we opened because of a failure.
    if (haveIp && ap && !apBecauseNoWifi && millis() - connectedSinceMs > 15000) stopAP();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void begin() {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  load();
  setenv("TZ", S.tz.c_str(), 1);
  tzset();
  board::setBrightness(S.brightness);

  esp_netif_init();
  esp_event_loop_create_default();
  staIf = esp_netif_create_default_wifi_sta();
  apIf = esp_netif_create_default_wifi_ap();
  esp_netif_set_hostname(staIf, "gridcheck");
  wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&wc);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr);

  esp_wifi_set_mode(WIFI_MODE_STA);
  if (!S.ssid.empty()) {
    wifi_config_t c = {};
    snprintf((char*)c.sta.ssid, sizeof c.sta.ssid, "%s", S.ssid.c_str());
    snprintf((char*)c.sta.password, sizeof c.sta.password, "%s", S.pass.c_str());
    esp_wifi_set_config(WIFI_IF_STA, &c);
  }
  esp_wifi_start();
  staStartMs = millis();
  if (S.ssid.empty()) {
    scan();  // list nearby networks on the setup page
    startAP(true);
  }
  else ESP_LOGI(TAG, "joining \"%s\"", S.ssid.c_str());

  startHttp();
  xTaskCreate(dnsTask, "dns", 3072, nullptr, 2, nullptr);
  xTaskCreate(watchTask, "netwatch", 3072, nullptr, 1, nullptr);
}

}  // namespace net
