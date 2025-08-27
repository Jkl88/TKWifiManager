#include <Arduino.h>
#define TKWM_USE_LITTLEFS 1   // 0 = SPIFFS, 1 = LittleFS
#include <TKWifiManager.h>

TKWifiManager wifi;  // порт 80

void setup() {
  Serial.begin(115200);
  delay(200);
  wifi.begin(true);  // монтирует FS, читает сети, STA→AP+Captive
  wifi.addRoute("/api/ping", HTTP_GET, [&](){
    wifi.webServer().send(200, "application/json", "{\"ok\":true}");
  });
}

void loop() {
  wifi.loop();
}
