// Тот же пример, что examples/Basic/Basic.ino (при смене примера правьте оба варианта).

#define TKWM_USE_LITTLEFS 1
#include <Arduino.h>
#include <TKWifiManager.h>

TKWifiManager wifiMgr(80);

#if defined(LED_BUILTIN)
  const int LED_PIN = LED_BUILTIN;
#else
  const int LED_PIN = 2;
#endif
volatile bool ledState   = false;
uint32_t      blinkPeriodMs = 0;
uint32_t      lastBlinkMs   = 0;
uint32_t      lastAnnounceMs = 0;

static String deviceInfoJson() {
  String mode = wifiMgr.inCaptive() ? "AP" : "STA";
  String ip   = wifiMgr.ip().toString();
  int    rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  String j    = "{";
  j += "\"type\":\"info\",";
  j += "\"mode\":\"" + mode + "\",";
  j += "\"ip\":\"" + ip + "\",";
  j += "\"rssi\":" + String(rssi) + ",";
  j += "\"uptime_ms\":" + String(millis());
  j += "}";
  return j;
}

static void myWsHook(uint8_t id, WStype_t type, const uint8_t* payload, size_t len) {
  if (type != WStype_TEXT) return;
  String s;
  s.reserve(len);
  for (size_t i = 0; i < len; i++) s += (char)payload[i];
  if (s == "ping") {
    wifiMgr.ws().sendTXT(id, String("{\"type\":\"pong\",\"t\":") + millis() + "}");
    return;
  }
  if (s == "get-info") {
    String j = deviceInfoJson();
    wifiMgr.ws().sendTXT(id, j);
    return;
  }
  if (s == "led:on") {
    blinkPeriodMs = 0;
    ledState = true;
    digitalWrite(LED_PIN, HIGH);
    wifiMgr.ws().sendTXT(id, "{\"type\":\"led\",\"state\":\"on\"}");
    return;
  }
  if (s == "led:off") {
    blinkPeriodMs = 0;
    ledState = false;
    digitalWrite(LED_PIN, LOW);
    wifiMgr.ws().sendTXT(id, "{\"type\":\"led\",\"state\":\"off\"}");
    return;
  }
  if (s.startsWith("led:blink:")) {
    long ms = s.substring(10).toInt();
    if (ms < 50) ms = 50;
    blinkPeriodMs = (uint32_t)ms;
    lastBlinkMs   = millis();
    wifiMgr.ws().sendTXT(id, String("{\"type\":\"led\",\"state\":\"blink\",\"period\":") + blinkPeriodMs + "}");
    return;
  }
  wifiMgr.ws().sendTXT(id, "{\"type\":\"error\",\"msg\":\"unknown cmd\"}");
}

void setupCustomRoutes() {
  wifiMgr.addRoute("/hello", HTTP_GET, []() {
    String s = "Hello from TKWifiManager!\n";
    s += "Mode: ";
    s += (WiFi.getMode() == WIFI_MODE_STA ? "STA" : (WiFi.getMode() == WIFI_MODE_AP ? "AP" : "AP+STA"));
    s += "\nIP:   ";
    s += wifiMgr.ip().toString();
    s += "\n";
    wifiMgr.web().send(200, "text/plain; charset=utf-8", s);
  });
  wifiMgr.addRoute("/api/led", HTTP_POST, []() {
    String cmd = wifiMgr.web().arg("cmd");
    if (cmd == "on") {
      blinkPeriodMs = 0;
      ledState      = true;
      digitalWrite(LED_PIN, HIGH);
      wifiMgr.web().send(200, "application/json", "{\"ok\":true,\"state\":\"on\"}");
    } else if (cmd == "off") {
      blinkPeriodMs = 0;
      ledState      = false;
      digitalWrite(LED_PIN, LOW);
      wifiMgr.web().send(200, "application/json", "{\"ok\":true,\"state\":\"off\"}");
    } else if (cmd == "blink") {
      uint32_t ms = (uint32_t)wifiMgr.web().arg("ms").toInt();
      if (ms < 50) ms = 50;
      blinkPeriodMs = ms;
      lastBlinkMs   = millis();
      wifiMgr.web().send(200, "application/json", String("{\"ok\":true,\"state\":\"blink\",\"period\":") + ms + "}");
    } else {
      wifiMgr.web().send(400, "application/json", "{\"ok\":false,\"msg\":\"bad cmd\"}");
    }
  });
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  wifiMgr.begin("DemoTKWM", false);
  setupCustomRoutes();
  wifiMgr.setUserWsHook(myWsHook);
  Serial.println(F("[EXAMPLE] ready. /wifi /fs /hello"));
}

void loop() {
  wifiMgr.loop();
  if (blinkPeriodMs) {
    uint32_t now = millis();
    if (now - lastBlinkMs >= blinkPeriodMs) {
      lastBlinkMs = now;
      ledState    = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  }
  if (millis() - lastAnnounceMs > 5000) {
    lastAnnounceMs = millis();
    String j = deviceInfoJson();
    wifiMgr.ws().broadcastTXT(j);
  }
}
