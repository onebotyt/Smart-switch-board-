/*******************************************************
  SmartBoard — ESP32 (REST + Blynk + GitHub UI via Blynk API)
  - No OTA
  - ESP is master controller (decides relay state)
  - REST endpoints for local UI control & state  (http://esp-ip/...)
  - Blynk handlers for app + GitHub UI control (ESP still decides)
  - Touch sensors are TTP223 momentary (rising-edge toggle)
  - Relays are active LOW

  WiFi priority (NEW):
  1. Saved WiFi (from prefs)
  2. Auto scan + reconnect every 15 seconds

  Blynk Virtual Pins:
  V0..V7  : 8 relay channels
  V8      : UI heartbeat
  V9      : status text
  V10     : presence
  V11     : PIR
  V12     : RCWL
  V15     : terminal
  V20     : mode
  V21     : restart
  V25     : logs
  V30     : WiFi SSID
  V31     : WiFi PASS
  V32     : Apply WiFi
********************************************************/

#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID   "your template id"
#define BLYNK_TEMPLATE_NAME "smartboard"
#define BLYNK_AUTH_TOKEN    "your blynk auth token"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <BlynkSimpleEsp32.h>
#include <time.h>

// ---------------- CONFIG ----------------
const char* DEFAULT_WIFI_SSID = "add ssid";
const char* DEFAULT_WIFI_PASS = "add password";

// NEW SECONDARY WIFI
const char* WIFI2_SSID = "add ssid";
const char* WIFI2_PASS = "add password";

// Pin Mapping
const uint8_t RELAY_PINS[8] = {4, 5, 13, 16, 17, 18, 19, 21};
const uint8_t TOUCH_PINS[8] = {32, 22, 23, 25, 26, 27, 34, 35};
const uint8_t PIR_PIN       = 39;
const uint8_t RCWL_PIN      = 36;
const uint8_t STATUS_LED    = 2;

// Timing
const unsigned long TOUCH_DEBOUNCE_MS        = 160;
const unsigned long PRESENCE_CLEAR_MARGIN_MS = 3000UL;
const unsigned long WIFI_CHECK_INTERVAL_MS   = 15000UL;

// Preferences
const char* PREF_NS          = "smartboard";
const char* PREF_PIR         = "pir";
const char* PREF_RCWL        = "rcwl";
const char* PREF_AUTO        = "auto";
const char* PREF_WIFI_SSID   = "w_ssid";
const char* PREF_WIFI_PASS   = "w_pass";
const char* PREF_NAME_PREFIX = "name";

// GLOBALS
WebServer   server(80);
Preferences prefs;
BlynkTimer  timer;

struct Light {
  bool on;
  char name[32];
};

Light lights[8];
bool pirEnabled = true;
bool rcwlEnabled = true;
bool presence = false;
unsigned long presenceLastSeen = 0;
unsigned long autoOffTimeout   = 120;

bool blynkConnected = false;
bool lastTouchState[8];
unsigned long lastTouchTime[8];
unsigned long lastWiFiCheck = 0;

// ---------------- UTIL ----------------
String nowTime() {
  time_t t = time(nullptr);
  struct tm *tm_info = localtime(&t);
  if (!tm_info) return String(millis() / 1000);
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
           tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
  return String(buf);
}

void logToSerialAndTerminal(const String &s) {
  Serial.println(s);
  if (Blynk.connected()) Blynk.virtualWrite(V25, s);
}

// ---------------- PREFERENCES ----------------
void loadPrefs() {
  prefs.begin(PREF_NS, true);
  pirEnabled     = prefs.getBool(PREF_PIR, true);
  rcwlEnabled    = prefs.getBool(PREF_RCWL, true);
  autoOffTimeout = prefs.getULong(PREF_AUTO, 120UL);

  for (int i = 0; i < 8; i++) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_NAME_PREFIX, i);
    String name = prefs.getString(key, "");
    if (name.length()) name.toCharArray(lights[i].name, 32);
    else {
      const char* def[8] = {
        "Front Light","Board Light","Left Wall","Right Wall",
        "Center 1","Center 2","Back Left","Back Right"
      };
      strncpy(lights[i].name, def[i], 31);
      lights[i].name[31] = 0;
    }
  }

  prefs.end();
}

void savePrefs() {
  prefs.begin(PREF_NS, false);
  prefs.putBool(PREF_PIR, pirEnabled);
  prefs.putBool(PREF_RCWL, rcwlEnabled);
  prefs.putULong(PREF_AUTO, autoOffTimeout);

  for (int i = 0; i < 8; i++) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", PREF_NAME_PREFIX, i);
    prefs.putString(key, lights[i].name);
  }
  prefs.end();
}

// ---------------- WIFI HELPERS ----------------
void scanAndPrintNetworks() {
  Serial.println("=== WiFi Scan ===");
  int n = WiFi.scanNetworks();
  if (n <= 0) Serial.println("No networks found");
  else {
    for (int i = 0; i < n; i++) {
      Serial.printf("%d: %s (%d dBm)\n",
        i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
  }
}

bool tryConnectWiFi(const String &ssid, const String &pass, unsigned long timeoutMs) {
  if (!ssid.length()) return false;

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.printf("Trying WiFi \"%s\"\n", ssid.c_str());
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("Failed.");
  return false;
}

bool ssidVisibleInScan(const String &target) {
  if (!target.length()) return false;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == target) return true;
  }
  return false;
}

// ------------ WIFI AT BOOT (Saved → Default → Secondary) ------------
void connectWiFiAtBoot() {
  prefs.begin(PREF_NS, true);
  String savedSsid = prefs.getString(PREF_WIFI_SSID, DEFAULT_WIFI_SSID);
  String savedPass = prefs.getString(PREF_WIFI_PASS, DEFAULT_WIFI_PASS);
  prefs.end();

  bool ok = tryConnectWiFi(savedSsid, savedPass, 8000);

  if (!ok) ok = tryConnectWiFi(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, 8000);

  if (!ok) ok = tryConnectWiFi(WIFI2_SSID, WIFI2_PASS, 8000);

  if (!ok) Serial.println("Boot WiFi: All failed.");
}

// ------------ AUTO WIFI MAINTAIN ------------
void autoWiFiMaintain() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWiFiCheck < WIFI_CHECK_INTERVAL_MS) return;
  lastWiFiCheck = millis();

  Serial.println("AutoWiFi: scanning...");

  prefs.begin(PREF_NS, true);
  String savedSsid = prefs.getString(PREF_WIFI_SSID, "");
  String savedPass = prefs.getString(PREF_WIFI_PASS, "");
  prefs.end();

  if (savedSsid.length() && ssidVisibleInScan(savedSsid)) {
    tryConnectWiFi(savedSsid, savedPass, 8000);
    return;
  }

  if (ssidVisibleInScan(DEFAULT_WIFI_SSID)) {
    tryConnectWiFi(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, 8000);
    return;
  }

  if (ssidVisibleInScan(WIFI2_SSID)) {
    tryConnectWiFi(WIFI2_SSID, WIFI2_PASS, 8000);
    return;
  }

  Serial.println("AutoWiFi: No known networks found.");
}


// ---------------- SAVE WIFI + APPLY FROM PREFS ----------------
void saveWiFiToPrefs(const String &ssid, const String &pass) {
  prefs.begin(PREF_NS, false);
  prefs.putString(PREF_WIFI_SSID, ssid);
  prefs.putString(PREF_WIFI_PASS, pass);
  prefs.end();
  Serial.printf("WiFi creds saved: SSID=%s, PASS=%s\n",
                ssid.c_str(),
                pass.length() ? "********" : "(empty)");
  logToSerialAndTerminal("WiFi creds saved (via prefs): " + ssid);
}

// Use prefs WiFi (for REST /applywifi and Blynk V32)
bool applyWifiFromPrefsCore() {
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString(PREF_WIFI_SSID, "");
  String pass = prefs.getString(PREF_WIFI_PASS, "");
  prefs.end();

  if (!ssid.length()) {
    logToSerialAndTerminal("applyWifiFromPrefsCore: no WiFi saved");
    return false;
  }

  logToSerialAndTerminal("applyWifiFromPrefs: trying SSID=" + ssid);
  bool ok = tryConnectWiFi(ssid, pass, 8000UL);

  if (ok) {
    if (Blynk.connected()) Blynk.disconnect();
    Blynk.config(BLYNK_AUTH_TOKEN);
    if (WiFi.status() == WL_CONNECTED) {
      Blynk.connect(5000);
    }
  }
  return ok;
}

// ---------------- RELAY CONTROL ----------------
void publishRelayStateToBlynk(int idx) {
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0 + idx, lights[idx].on ? 1 : 0);
  }
}

void setRelay(int idx, bool on, bool notify = true) {
  if (idx < 0 || idx >= 8) return;
  digitalWrite(RELAY_PINS[idx], on ? LOW : HIGH);
  lights[idx].on = on;
  if (notify && Blynk.connected()) publishRelayStateToBlynk(idx);
  logToSerialAndTerminal(String("[Relay] ") + String(lights[idx].name) + (on ? " ON" : " OFF"));
}

void toggleRelayIdx(int idx) {
  setRelay(idx, !lights[idx].on, true);
}

// ---------------- TOUCH POLLING ----------------
void pollTouchSensors() {
  unsigned long now = millis();
  for (int i = 0; i < 8; ++i) {
    bool s = (digitalRead(TOUCH_PINS[i]) == HIGH);
    if (s && !lastTouchState[i]) {
      if (now - lastTouchTime[i] > TOUCH_DEBOUNCE_MS) {
        toggleRelayIdx(i);
        lastTouchTime[i] = now;
      }
    }
    lastTouchState[i] = s;
  }
}

// ---------------- PRESENCE LOGIC ----------------
void reportPresence(bool p) {
  presence = p;
  presenceLastSeen = millis();
  if (Blynk.connected()) Blynk.virtualWrite(V10, p ? 1 : 0);
  logToSerialAndTerminal(String("Presence -> ") + (p ? "YES" : "NO"));
}

void pollPresenceSensors() {
  bool detected = false;
  if (pirEnabled  && digitalRead(PIR_PIN)  == HIGH) detected = true;
  if (rcwlEnabled && digitalRead(RCWL_PIN) == HIGH) detected = true;

  if (detected) {
    if (!presence) reportPresence(true);
    presenceLastSeen = millis();
  } else {
    if (presence && (millis() - presenceLastSeen > PRESENCE_CLEAR_MARGIN_MS)) {
      reportPresence(false);
    }
  }
}

void handleAutoOffCheck() {
  if (!presence && autoOffTimeout > 0) {
    if (millis() - presenceLastSeen > (autoOffTimeout * 1000UL)) {
      bool anyOn = false;
      for (int i = 0; i < 8; i++) {
        if (lights[i].on) { anyOn = true; break; }
      }
      if (anyOn) {
        for (int i = 0; i < 8; i++) setRelay(i, false, true);
        logToSerialAndTerminal("Auto-off executed: all lights OFF");
        if (Blynk.connected()) Blynk.virtualWrite(V25, "Auto-off executed: all lights OFF");
      }
    }
  }
}

// ---------------- JSON HELP ----------------
String jsonEscape(const String &s) {
  String out;
  for (char c : s) {
    if (c == '"' )      out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else                out += c;
  }
  return out;
}

// ---------------- REST HANDLERS ----------------
void handle_status() {
  String s = "{";
  s += "\"esp\":\"" + String(WiFi.status() == WL_CONNECTED ? "online" : "offline") + "\",";
  s += "\"ip\":\""  + WiFi.localIP().toString() + "\",";
  s += "\"presence\":" + String(presence ? 1 : 0) + ",";
  s += "\"pir\":"      + String(pirEnabled ? 1 : 0) + ",";
  s += "\"rcwl\":"     + String(rcwlEnabled ? 1 : 0) + ",";
  s += "\"auto_off\":" + String(autoOffTimeout) + ",";
  s += "\"lights\":[";
  for (int i = 0; i < 8; i++) {
    s += "{\"id\":" + String(i) +
         ",\"on\":" + String(lights[i].on ? 1 : 0) +
         ",\"name\":\"" + jsonEscape(String(lights[i].name)) + "\"}";
    if (i < 7) s += ",";
  }
  s += "],";
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString(PREF_WIFI_SSID, "");
  prefs.end();
  s += "\"wifi\":\"" + jsonEscape(ssid) + "\"";
  s += "}";
  server.send(200, "application/json", s);
}

void handle_relay() {
  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"error\":\"missing id\"}");
    return;
  }
  int id  = server.arg("id").toInt();
  int val = server.hasArg("val") ? server.arg("val").toInt() : -1;
  if (id < 0 || id > 7) {
    server.send(400, "application/json", "{\"error\":\"id out of range\"}");
    return;
  }
  if (val == 2)      toggleRelayIdx(id);
  else if (val == 1) setRelay(id, true, true);
  else if (val == 0) setRelay(id, false, true);
  else {
    server.send(400, "application/json", "{\"error\":\"invalid val\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

void applyModeByName(const String &m) {
  if (m.equalsIgnoreCase("teaching") || m == "1") {
    for (int i = 0; i < 8; i++) setRelay(i, true, true);
    logToSerialAndTerminal("Mode: Teaching");
  } else if (m.equalsIgnoreCase("projector") || m == "2") {
    for (int i = 0; i < 8; i++) setRelay(i, (i == 1 || i == 4 || i == 5), true);
    logToSerialAndTerminal("Mode: Projector");
  } else if (m.equalsIgnoreCase("energy") || m == "3") {
    for (int i = 0; i < 8; i++) setRelay(i, (i == 0 || i == 1), true);
    logToSerialAndTerminal("Mode: Energy");
  } else {
    logToSerialAndTerminal("Mode: unknown '" + m + "'");
  }
}

void handle_mode() {
  if (!server.hasArg("m")) {
    server.send(400, "application/json", "{\"error\":\"missing mode\"}");
    return;
  }
  applyModeByName(server.arg("m"));
  server.send(200, "application/json", "{\"ok\":1}");
}

void handle_pir() {
  if (!server.hasArg("val")) {
    server.send(400, "application/json", "{\"error\":\"missing val\"}");
    return;
  }
  pirEnabled = server.arg("val").toInt() ? true : false;
  savePrefs();
  server.send(200, "application/json", "{\"ok\":1}");
}

void handle_rcwl() {
  if (!server.hasArg("val")) {
    server.send(400, "application/json", "{\"error\":\"missing val\"}");
    return;
  }
  rcwlEnabled = server.arg("val").toInt() ? true : false;
  savePrefs();
  server.send(200, "application/json", "{\"ok\":1}");
}

void handle_autooff() {
  if (!server.hasArg("sec")) {
    server.send(400, "application/json", "{\"error\":\"missing sec\"}");
    return;
  }
  unsigned long v = (unsigned long) server.arg("sec").toInt();
  if (v > 86400UL) v = 86400UL;
  autoOffTimeout = v;
  savePrefs();
  server.send(200, "application/json", "{\"ok\":1}");
}

void handle_wifi() {
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json", "{\"error\":\"missing ssid\"}");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";
  saveWiFiToPrefs(ssid, pass);
  logToSerialAndTerminal("Received WiFi via REST: " + ssid);
  server.send(200, "application/json", "{\"ok\":1}");
}

void handle_applywifi() {
  bool ok = applyWifiFromPrefsCore();
  if (ok) {
    server.send(200, "application/json",
                "{\"ok\":1, \"ip\":\"" + WiFi.localIP().toString() + "\"}");
  } else {
    server.send(200, "application/json",
                "{\"ok\":0, \"msg\":\"connect failed\"}");
  }
}

void handle_rename() {
  if (!server.hasArg("id") || !server.hasArg("name")) {
    server.send(400, "application/json", "{\"error\":\"missing id or name\"}");
    return;
  }
  int id = server.arg("id").toInt();
  String nm = server.arg("name");
  if (id < 0 || id > 7) {
    server.send(400, "application/json", "{\"error\":\"id out of range\"}");
    return;
  }
  nm.toCharArray(lights[id].name, sizeof(lights[id].name));
  prefs.begin(PREF_NS, false);
  char key[16];
  snprintf(key, sizeof(key), "%s%d", PREF_NAME_PREFIX, id);
  prefs.putString(key, nm);
  prefs.end();
  logToSerialAndTerminal("Renamed " + String(id) + " -> " + nm);
  server.send(200, "application/json", "{\"ok\":1}");
}

void handle_terminal() {
  if (!server.hasArg("txt")) {
    server.send(400, "application/json", "{\"error\":\"missing txt\"}");
    return;
  }
  String txt = server.arg("txt");
  logToSerialAndTerminal(String("[UI] ") + txt);
  server.send(200, "application/json", "{\"ok\":1}");
}

void handle_notfound() {
  server.send(404, "text/plain", "Not found");
}

// ---------------- BLYNK HANDLERS ----------------
BLYNK_WRITE(V0) { setRelay(0, param.asInt() == 1, true); }
BLYNK_WRITE(V1) { setRelay(1, param.asInt() == 1, true); }
BLYNK_WRITE(V2) { setRelay(2, param.asInt() == 1, true); }
BLYNK_WRITE(V3) { setRelay(3, param.asInt() == 1, true); }
BLYNK_WRITE(V4) { setRelay(4, param.asInt() == 1, true); }
BLYNK_WRITE(V5) { setRelay(5, param.asInt() == 1, true); }
BLYNK_WRITE(V6) { setRelay(6, param.asInt() == 1, true); }
BLYNK_WRITE(V7) { setRelay(7, param.asInt() == 1, true); }

BLYNK_WRITE(V11) { pirEnabled  = (param.asInt() == 1); savePrefs(); }
BLYNK_WRITE(V12) { rcwlEnabled = (param.asInt() == 1); savePrefs(); }

BLYNK_WRITE(V20) {
  int v = param.asInt();
  if (v == 1)      applyModeByName("teaching");
  else if (v == 2) applyModeByName("projector");
  else if (v == 3) applyModeByName("energy");
}

BLYNK_WRITE(V21) {
  if (param.asInt() == 1) {
    logToSerialAndTerminal("Reboot via Blynk");
    delay(200);
    ESP.restart();
  }
}

BLYNK_WRITE(V15) {
  String cmd = param.asStr();
  logToSerialAndTerminal(String("[BlynkCmd] ") + cmd);

  if (cmd.equalsIgnoreCase("status")) {
    for (int i = 0; i < 8; i++) Blynk.virtualWrite(V0 + i, lights[i].on ? 1 : 0);
    Blynk.virtualWrite(V10, presence ? 1 : 0);
    Blynk.virtualWrite(V11, pirEnabled  ? 1 : 0);
    Blynk.virtualWrite(V12, rcwlEnabled ? 1 : 0);
    Blynk.virtualWrite(V9, String("SmartBoard > ") + nowTime());
  } else if (cmd.startsWith("setname ")) {
    int sp1 = cmd.indexOf(' ');
    int sp2 = cmd.indexOf(' ', sp1 + 1);
    if (sp2 > 0) {
      int idx = cmd.substring(sp1 + 1, sp2).toInt();
      String nm = cmd.substring(sp2 + 1);
      if (idx >= 0 && idx < 8 && nm.length() > 0) {
        nm.toCharArray(lights[idx].name, sizeof(lights[idx].name));
        prefs.begin(PREF_NS, false);
        char key[16]; snprintf(key, sizeof(key), "%s%d", PREF_NAME_PREFIX, idx);
        prefs.putString(key, nm);
        prefs.end();
        logToSerialAndTerminal("Name saved (via Blynk): " + nm);
      }
    }
  } else if (cmd.equalsIgnoreCase("applywifi")) {
    bool ok = applyWifiFromPrefsCore();
    logToSerialAndTerminal(ok ? "applywifi OK" : "applywifi FAILED");
  }
}

BLYNK_WRITE(V30) {
  String newSsid = param.asStr();
  if (newSsid.length()) {
    prefs.begin(PREF_NS, true);
    String oldPass = prefs.getString(PREF_WIFI_PASS, DEFAULT_WIFI_PASS);
    prefs.end();
    saveWiFiToPrefs(newSsid, oldPass);
    logToSerialAndTerminal("WiFi SSID updated via V30: " + newSsid);
  }
}

BLYNK_WRITE(V31) {
  String newPass = param.asStr();
  prefs.begin(PREF_NS, true);
  String oldSsid = prefs.getString(PREF_WIFI_SSID, DEFAULT_WIFI_SSID);
  prefs.end();
  saveWiFiToPrefs(oldSsid, newPass);
  logToSerialAndTerminal("WiFi PASS updated via V31");
}

BLYNK_WRITE(V32) {
  if (param.asInt() == 1) {
    logToSerialAndTerminal("V32: Apply WiFi requested");
    bool ok = applyWifiFromPrefsCore();
    logToSerialAndTerminal(ok ? "V32: WiFi OK" : "V32: WiFi FAILED");
  }
}

BLYNK_CONNECTED() {
  blynkConnected = true;
  digitalWrite(STATUS_LED, HIGH);
  Blynk.virtualWrite(V9, "ESP Online");
  for (int i = 0; i < 8; i++) Blynk.virtualWrite(V0 + i, lights[i].on ? 1 : 0);
  Blynk.virtualWrite(V11, pirEnabled  ? 1 : 0);
  Blynk.virtualWrite(V12, rcwlEnabled ? 1 : 0);
  Blynk.virtualWrite(V10, presence    ? 1 : 0);
}

BLYNK_DISCONNECTED() {
  blynkConnected = false;
  digitalWrite(STATUS_LED, LOW);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("=== SmartBoard Boot (No OTA) ===");

  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], HIGH);
    pinMode(TOUCH_PINS[i], INPUT);
    lastTouchState[i] = false;
    lastTouchTime[i]  = 0;
  }

  pinMode(PIR_PIN,    INPUT);
  pinMode(RCWL_PIN,   INPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  loadPrefs();

  for (int i = 0; i < 8; i++) {
    digitalWrite(RELAY_PINS[i], lights[i].on ? LOW : HIGH);
  }

  scanAndPrintNetworks();
  connectWiFiAtBoot();

  Blynk.config(BLYNK_AUTH_TOKEN);
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.connect(5000);
  }

  server.on("/status",    HTTP_GET,  handle_status);
  server.on("/relay",     HTTP_POST, handle_relay);
  server.on("/mode",      HTTP_POST, handle_mode);
  server.on("/pir",       HTTP_POST, handle_pir);
  server.on("/rcwl",      HTTP_POST, handle_rcwl);
  server.on("/autooff",   HTTP_POST, handle_autooff);
  server.on("/wifi",      HTTP_POST, handle_wifi);
  server.on("/applywifi", HTTP_POST, handle_applywifi);
  server.on("/rename",    HTTP_POST, handle_rename);
  server.on("/terminal",  HTTP_POST, handle_terminal);
  server.onNotFound(handle_notfound);

  server.begin();

  timer.setInterval(120L,  pollTouchSensors);
  timer.setInterval(300L,  pollPresenceSensors);
  timer.setInterval(1000L, handleAutoOffCheck);
  timer.setInterval(8000L, []() {
    if (Blynk.connected()) {
      Blynk.virtualWrite(V8, 1);
      Blynk.virtualWrite(V9, String("SmartBoard > ") + nowTime());
      for (int i = 0; i < 8; i++) {
        Blynk.virtualWrite(V0 + i, lights[i].on ? 1 : 0);
      }
    }
  });

  Serial.println("Setup complete.");
}

// ---------------- LOOP ----------------
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    autoWiFiMaintain();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!Blynk.connected()) {
      Blynk.connect(2000);
    } else {
      Blynk.run();
    }
  }

  server.handleClient();
  timer.run();
  delay(5);
}
