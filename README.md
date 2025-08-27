# TKWifiManager (ESP32)

Самописный Wi‑Fi менеджер для ESP32: AP+Captive (с автооткрытием на Android/iOS/Windows), «живой» скан сетей,
подключение к сохранённым сетям, OTA, файловый менеджер (просмотр/скачивание/удаление/загрузка/редактор),
UDP-ответчик для autodiscovery и возможность добавлять кастомные роуты.

## Быстрый старт

```cpp
#define TKWM_USE_LITTLEFS 0  // 0 = SPIFFS, 1 = LittleFS
#include <TKWifiManager.h>

TKWifiManager wifi;

void setup() {
  Serial.begin(115200);
  wifi.begin(true); // автоформат при первом запуске
  // пример кастомного роутера
  wifi.addRoute("/api/ping", HTTP_GET, [&](){ wifi.webServer().send(200, "application/json", "{"ok":true}"); });
}
void loop() { wifi.loop(); }
```

Откройте `http://<ip>/` — портал настроек. В каптиве SSID вида `TK-Setup-XXXXXX`.

## Опции
- FS: `#define TKWM_USE_LITTLEFS 1` для LittleFS
- UDP порт и сигнатура: `TKWM_DISCOVERY_PORT`, `TKWM_DISCOVERY_SIGNATURE`
- Включение/выключение отклика UDP: `setDiscoveryEnabled(bool)`

## Лицензия
MIT
