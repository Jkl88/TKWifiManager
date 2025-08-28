# TKWifiManager — README

## Что умеет библиотека «из коробки»

### Wi-Fi / Captive / сохранённые сети
- **AP+Captive портал**: поднимает точку доступа `"<префикс>-<HEX_MAC>"`, DNS wildcard и редиректы (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`) на `/wifi`.
- **STA-подключение** к сохранённым сетям (N профилей), хранение в `Preferences`.
- **Не рвёт AP при сканировании** — клиент не отваливается.
- **Страница `/wifi`**: список найденных сетей (WS), ручной ввод SSID/пароля, список сохранённых сетей (удаление), кнопка «Перейти в AP-режим».

### Файловая система (LittleFS по умолчанию)
- Встроенный **менеджер файлов** `/fs`:
  - список файлов;
  - открыть/скачать/удалить;
  - **редактор текстовых файлов** (Ace);
  - drag&drop **загрузка в FS** (мультифайлы) через `/upload?to=/путь/имя`.
- REST-эндпоинты `/api/fs/*`:
  - `GET /api/fs/list` — список файлов;
  - `GET /api/fs/get?path=/...` — отдать текст (безопасное JSON-экранирование, chunked);
  - `POST /api/fs/put?path=/...` — создать/перезаписать текст;
  - `POST /api/fs/delete` — удалить (body: `path=...`);
  - `POST /api/fs/mkdir` — создать папку (опционально).

### OTA (через веб)
- Страница `/ota` (форма с прогресс-баром).
- `POST /ota` — обработчик прошивки (`Update.begin/…/end`) + авто-ребут.
- Логи и диагностика ошибок OTA (в HTML и Serial).

### UDP-ответ («личный» ответ на broadcast)
- Слушает UDP-запросы (например, `whois`/`discovery` на фиксированном порту) и **отвечает JSON-ом** с model/name/IP/версией.

### Веб-сервер и WS
- HTTP: `WebServer` (порт 80), **легко добавлять свои маршруты**.
- WS: `WebSocketsServer` (порт 81), **пользовательский хук** для команд, плюс встроенные:
  - входящие: `"status"`, `"scan"`;
  - исходящие: `{"type":"status",...}` и `{"type":"scan","nets":[...]}`.

---

## Список встроенных HTTP-роутов (по умолчанию)

| Метод | Путь                   | Назначение |
|------:|------------------------|------------|
| GET   | `/`                    | Если AP/captive — редирект на `/wifi`. Иначе пытается отдать `/index.html` из FS; если нет — отдаёт встроенную «404-upload». |
| GET   | `/wifi`                | Встроенная страница настройки Wi-Fi. |
| GET   | `/fs`                  | Менеджер файлов. |
| GET   | `/ota`                 | OTA-страница. |
| GET   | `/api/fs/list`         | JSON со списком файлов. |
| GET   | `/api/fs/get?path=/..` | Возвращает содержимое текстового файла. |
| POST  | `/api/fs/put?path=/..` | Записывает текст в файл. |
| POST  | `/api/fs/delete`       | Удаляет файл. |
| POST  | `/api/fs/mkdir`        | Создаёт папку. |
| POST  | `/upload?to=/path.ext` | Загружает файл в FS. |
| POST  | `/api/wifi/save`       | Сохраняет профиль Wi-Fi и подключается. |
| POST  | `/api/reconnect`       | Форсирует переподключение. |
| POST  | `/api/start_ap`        | Переходит в AP-режим. |
| GET   | `/api/wifi/saved`      | Список сохранённых сетей. |
| POST  | `/api/wifi/delete`     | Удаляет сохранённую сеть. |

---

## WebSocket: входящие/исходящие

**Порт:** `ws://<host>:81/`

Встроенные входящие:
- `"status"` → `{"type":"status","mode":"AP|STA","ip":"..."}`
- `"scan"` → `{"type":"scan","nets":[...]}`

Можно задать пользовательский обработчик через `setUserWsHook()`.

---

## AP-префикс

- `begin()` принимает `apSsidPrefix` и запоминает его.
- `startAPCaptive()` использует `<prefix>-<MAC>`.

---

## Подмена встроенных страниц

Положите в FS файл `wifi.html`, `fs.html` или `ota.html` — и они заменят встроенные страницы.

---

## Добавление своих HTTP-маршрутов

```cpp
wifiMgr.addRoute("/hello", HTTP_GET, [](){
  wifiMgr.web().send(200, "text/plain", "Hello!");
});
wifiMgr.addRoute("/api/something", HTTP_POST, [](){
  String arg = wifiMgr.web().arg("foo");
  wifiMgr.web().send(200, "application/json", "{\"ok\":true}");
});
```
`wifiMgr.web()` — это ссылка на внутренний `WebServer`.

## UDP-ответ на broadcast (для вашего Android-сканера)

Менеджер по умолчанию слушает «свой» порт (например, `TKWM_DISCOVERY_PORT`) и отвечает JSON’ом вида:

`{   "type": "whoami",   "model": "MyDevice",   "name": "Room-1",   "ip": "192.168.1.50",   "ver": "1.2.3" }`

Вы можете:

- изменить порт/формат ответа;
    
- добавить поля (например, `mac`, `uptime_ms`, `features`).
    

(Если вы ещё не включали в своей сборке — в менеджере есть метод инициализации UDP responder; покажу где это в коде, если нужно.)

---

## Полный пример: пользовательские роуты + свой WS-хендлер

```cpp
// Example_TKWM_Documented.ino
// Демонстрация возможностей TKWifiManager: встроенные страницы, свои HTTP-роуты,
// свой WS-обработчик, периодические WS-уведомления, управление AP-префиксом.

#define TKWM_USE_LITTLEFS 1   // библиотека по умолчанию собирается с LittleFS
#include <Arduino.h>
#include "TKWifiManager.h"

TKWifiManager wifiMgr(80);

// ===== Простой управляемый «девайс» — LED =====
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif
const int LED_PIN = LED_BUILTIN;
volatile bool ledOn = false;
uint32_t blinkMs = 0, lastBlink = 0;

// ===== Пользовательский WS-обработчик =====
// Поддерживает команды (текст):
//  - "ping"               → ответ {"type":"pong","t":<millis>}
//  - "get-info"           → ответ {"type":"info", ...}
//  - "led:on" / "led:off" → управлять LED
//  - "led:blink:250"      → мигание с периодом 250мс
static void myWsHandler(uint8_t id, WStype_t t, const uint8_t* payload, size_t len) {
  if (t != WStype_TEXT) return;
  String s; s.reserve(len);
  for (size_t i=0;i<len;i++) s += (char)payload[i];

  if (s == "ping") {
    wifiMgr.ws().sendTXT(id, String("{\"type\":\"pong\",\"t\":") + millis() + "}");
    return;
  }

  if (s == "get-info") {
    String mode = wifiMgr.inCaptive() ? "AP" : "STA";
    String ip   = wifiMgr.ip().toString();
    int rssi    = (WiFi.status()==WL_CONNECTED) ? WiFi.RSSI() : 0;
    String j = "{";
    j += "\"type\":\"info\",";
    j += "\"mode\":\""+mode+"\",";
    j += "\"ip\":\""+ip+"\",";
    j += "\"rssi\":"+String(rssi)+",";
    j += "\"uptime_ms\":"+String(millis());
    j += "}";
    wifiMgr.ws().sendTXT(id, j);
    return;
  }

  if (s == "led:on")  { blinkMs = 0; ledOn = true;  digitalWrite(LED_PIN, HIGH); return; }
  if (s == "led:off") { blinkMs = 0; ledOn = false; digitalWrite(LED_PIN, LOW);  return; }

  if (s.startsWith("led:blink:")) {
    long p = s.substring(10).toInt();
    if (p < 50) p = 50;
    blinkMs = (uint32_t)p;
    lastBlink = millis();
    return;
  }

  wifiMgr.ws().sendTXT(id, "{\"type\":\"error\",\"msg\":\"unknown cmd\"}");
}

// ===== Свои HTTP-роуты =====
void setupCustomRoutes() {
  // GET /hello → «Hello + статус»
  wifiMgr.addRoute("/hello", HTTP_GET, [](){
    String mode = (WiFi.getMode()==WIFI_MODE_AP) ? "AP" : (WiFi.getMode()==WIFI_MODE_STA ? "STA" : "AP+STA");
    String s = "Hello from TKWifiManager!\n";
    s += "Mode: " + mode + "\n";
    s += "IP:   " + wifiMgr.ip().toString() + "\n";
    wifiMgr.web().send(200, "text/plain; charset=utf-8", s);
  });

  // POST /api/led?cmd=on|off|blink&ms=...
  wifiMgr.addRoute("/api/led", HTTP_POST, [](){
    String cmd = wifiMgr.web().arg("cmd");
    if (cmd == "on") {
      blinkMs=0; ledOn=true; digitalWrite(LED_PIN,HIGH);
      wifiMgr.web().send(200,"application/json","{\"ok\":true,\"state\":\"on\"}");
    } else if (cmd == "off") {
      blinkMs=0; ledOn=false; digitalWrite(LED_PIN,LOW);
      wifiMgr.web().send(200,"application/json","{\"ok\":true,\"state\":\"off\"}");
    } else if (cmd == "blink") {
      uint32_t p = (uint32_t)wifiMgr.web().arg("ms").toInt(); if (p<50) p=50;
      blinkMs=p; lastBlink=millis();
      wifiMgr.web().send(200,"application/json", String("{\"ok\":true,\"state\":\"blink\",\"period\":")+p+"}");
    } else {
      wifiMgr.web().send(400,"application/json","{\"ok\":false,\"msg\":\"bad cmd\"}");
    }
  });

  // необязательный: страница «мини-панель» (если не кладёте свой index.html)
  wifiMgr.addRoute("/panel", HTTP_GET, [](){
    String html = "<!doctype html><meta charset='utf-8'><title>Panel</title>"
                  "<h3>Panel</h3>"
                  "<button onclick=\"fetch('/api/led?cmd=on',{method:'POST'})\">LED ON</button> "
                  "<button onclick=\"fetch('/api/led?cmd=off',{method:'POST'})\">LED OFF</button> "
                  "<button onclick=\"fetch('/api/led?cmd=blink&ms=200',{method:'POST'})\">BLINK</button> "
                  "<p><a href='/wifi'>Wi-Fi</a> • <a href='/fs'>FS</a> • <a href='/ota'>OTA</a></p>";
    wifiMgr.web().send(200,"text/html; charset=utf-8",html);
  });
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // ВАЖНО: передайте префикс SSID — он сохранится и будет использоваться при ручном старте AP
  wifiMgr.begin(/*formatFS=*/true, /*apSsidPrefix=*/"DemoTKWM");

  // Подключаем свои HTTP-маршруты
  setupCustomRoutes();

  // Подключаем свой WS-хендлер (вместе с встроенными "status"/"scan")
  wifiMgr.setUserWsHook(myWsHandler);

  Serial.println(F("[EXAMPLE] готово. Откройте /wifi  /fs  /ota  /panel  /hello"));
}

uint32_t lastAnnounce = 0;
void loop() {
  wifiMgr.loop();

  // Локальная логика: мигание светодиодом
  if (blinkMs) {
    uint32_t now = millis();
    if (now - lastBlink >= blinkMs) {
      lastBlink = now;
      ledOn = !ledOn;
      digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
    }
  }

  // Периодически анонсируем статус по WS (раз в 5 сек)
  if (millis() - lastAnnounce > 5000) {
    lastAnnounce = millis();
    String mode = wifiMgr.inCaptive() ? "AP" : "STA";
    String ip   = wifiMgr.ip().toString();
    String j = String("{\"type\":\"status\",\"mode\":\"")+mode+"\",\"ip\":\""+ip+"\"}";
    wifiMgr.ws().broadcastTXT(j);
  }
}
```

## Частые вопросы / подсказки

- **FS «загружается», но файла нет.**  
    Убедитесь, что используется **LittleFS** (по умолчанию включено в библиотеке). В логе при `begin()` пишется: `[TKWM] FS = LittleFS` и `FS mount: OK`. В `boards.txt`/Partition Scheme — разметка совместимая с LittleFS.
      
- **Как подменить встроенную `/wifi`/`/fs`/`/ota`?**  
    Просто положите в FS одноимённые файлы `wifi.html`/`fs.html`/`ota.html`. Сервер отдаст их вместо встроенных.
    
- **Как добавить «свой» UDP-ответ для Android-сканера?**  
    Либо используйте встроенный responder (порт/формат можно изменить), либо создайте свой `WiFiUDP` и слушайте broadcast, отвечая JSON’ом. Могу прислать готовый фрагмент под ваш формат.
    