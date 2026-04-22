# TKWifiManager

Библиотека для ESP32 (Arduino framework): Wi-Fi менеджер с captive-порталом, файловым менеджером, OTA, UDP-discovery и расширяемым веб-сервером.

---

## Содержание

- [Что умеет «из коробки»](#что-умеет-из-коробки)
- [Быстрый старт](#быстрый-старт)
- [PlatformIO](#platformio)
- [HTTP-маршруты](#http-маршруты)
- [WebSocket API](#websocket-api)
- [Темы оформления (theme.css / theme.js)](#темы-оформления)
- [Подмена встроенных страниц](#подмена-встроенных-страниц)
- [Добавление своих HTTP-маршрутов](#добавление-своих-http-маршрутов)
- [Пользовательский WS-хук](#пользовательский-ws-хук)
- [Компиляционные макросы](#компиляционные-макросы)
- [UDP-discovery](#udp-discovery)
- [ESPConnect OTA (ESPTools)](#espconnect-ota-esptools)
- [Полный пример](#полный-пример)
- [Частые вопросы](#частые-вопросы)

---

## Что умеет «из коробки»

### Wi-Fi / Captive / сохранённые сети

- **AP + Captive-портал**: поднимает точку доступа `"<префикс>-<HEX_MAC>"`, DNS wildcard и редиректы (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`) на `/wifi`.
- **STA-подключение** к сохранённым сетям (до 16 профилей), хранение в `Preferences`.
- **Не рвёт AP при сканировании** — подключённые клиенты не отваливаются.
- **Страница `/wifi`**: список найденных сетей, ручной ввод SSID/пароля, список сохранённых сетей с удалением, кнопка «Перейти в AP-режим».
- **Watchdog**: каждые 4 с в STA-режиме проверяет соединение; если пропало — автоматически поднимает AP.

### Файловая система (LittleFS по умолчанию)

- Встроенный **менеджер файлов** `/fs`:
  - рекурсивный список файлов (включая поддиректории);
  - открыть / скачать / удалить;
  - **редактор текстовых файлов** с подсветкой синтаксиса (Ace Editor, CDN);
  - drag-and-drop **загрузка в FS** через `/upload?to=/путь/имя`.
- REST API `/api/fs/*`:
  - `GET  /api/fs/list`         — JSON с рекурсивным списком файлов и размерами;
  - `GET  /api/fs/get?path=/..` — содержимое текстового файла (chunked, JSON-экранирование);
  - `POST /api/fs/put?path=/..` — создать / перезаписать файл;
  - `POST /api/fs/delete`       — удалить файл (`body: path=...`);
  - `POST /api/fs/mkdir`        — создать папку.

### OTA (через браузер)

- Страница `/ota` — ручная загрузка `.bin` с ПК (multipart) и, отдельно, проверка/установка с бэкенда ESPConnect (см. [ESPConnect OTA](#espconnect-ota-esptools)).
- `POST /ota` — загрузка `.bin` в прошивку, `Update.begin/write/end`, авто-перезагрузка.

### UDP-discovery

- Слушает порт `TKWM_DISCOVERY_PORT` (по умолчанию `64242`).
- На пакет с префиксом `"TK_DISCOVER:1"` отвечает JSON:
  ```json
  { "id": "MyDevice-A1B2C3", "name": "MyDevice-A1B2C3",
    "ip": "192.168.1.50", "model": "ESP32", "web": "/" }
  ```

### Веб-сервер и WebSocket

- HTTP: `WebServer` (порт 80), легко добавлять свои маршруты.
- WS: `WebSocketsServer` (порт 81), встроенные команды + пользовательский хук.

---

## Быстрый старт

```cpp
#include <TKWifiManager.h>

TKWifiManager wifiMgr(80);

void setup() {
    Serial.begin(115200);
    // 1) префикс SSID  2) true = отформатировать FS при сбое монтирования (только на «пустой» плате; иначе false)
    wifiMgr.begin("MyDevice", false);
    // if (!wifiMgr.isFilesystemOk()) { /* веб из PROGMEM, без полноценного /fs */ }
}

void loop() {
    wifiMgr.loop();
}
```

> **Безопасность:** OTA, FS и смена Wi-Fi в веб-интерфейсе **без пароля**. В ненадёжных сетях и в продакшене ограничьте доступ (свой слой аутентификации, VPN, изолированный AP и т.д.).

После загрузки: если нет сохранённых сетей — поднимается AP `MyDevice-XXXXXX`.  
Откройте `http://192.168.4.1/wifi` для настройки.

---

## PlatformIO

Содержимое **репозитория не задумано** как «один прошиваемый корень» PlatformIO: подключайте библиотеку как зависимость к своему `platformio.ini` или смотрите готовый пример в `extras/PlatformioBasic/`.

Библиотека работает **только с Arduino framework** на ESP32.  
ESP-IDF native не поддерживается (используются `WebServer`, `Preferences`, `LittleFS`, `DNSServer`, `Update` — Arduino-обёртки).

### `platformio.ini` в *вашей* прошивке (минимум)

```ini
[env:esp32dev]
platform  = espressif32
board     = esp32dev
framework = arduino
board_build.partitions = min_spiffs.csv
lib_deps =
    https://github.com/Jkl88/TKWifiManager.git
    links2004/WebSockets @ ^2.4.1
build_flags = -DTKWM_USE_LITTLEFS=1
monitor_speed = 115200
```

### Пример в репозитории

Папка **`extras/PlatformioBasic`** — откройте её в PlatformIO как отдельный проект. Там `lib_deps = file://../..` указывает на корень библиотеки. Команда: `pio run`.

### Загрузка LittleFS (в своём проекте)

Создайте env с `board_build.filesystem = littlefs`, положите файлы в `data/`, например: `pio run -e esp32dev_upload_fs -t uploadfs` (см. `platformio.ini` в `extras/PlatformioBasic` для примера env `esp32dev_upload_fs`).

---

## HTTP-маршруты

| Метод | Путь                   | Назначение |
|------:|------------------------|------------|
| GET   | `/`                    | В AP-режиме → редирект на `/wifi`. Иначе → `/index.html` из FS или встроенная страница. |
| GET   | `/wifi`                | Страница настройки Wi-Fi. |
| GET   | `/fs`                  | Файловый менеджер. |
| GET   | `/ota`                 | OTA-страница. |
| GET   | `/api/fs/list`         | JSON: рекурсивный список файлов `{"files":[{"path":"/...","size":N},...]}`. |
| GET   | `/api/fs/get?path=/..` | Содержимое текстового файла. |
| POST  | `/api/fs/put?path=/..` | Записать текст в файл. |
| POST  | `/api/fs/delete`       | Удалить файл (`path=...`). |
| POST  | `/api/fs/mkdir`        | Создать папку. |
| POST  | `/upload?to=/path.ext` | Загрузить файл в FS (multipart). |
| POST  | `/api/wifi/save`       | Сохранить профиль и подключиться (JSON body). |
| GET   | `/api/wifi/scan`       | REST-сканирование сетей: `{"connected":bool,"ip":"...","nets":[...]}`. |
| GET   | `/api/wifi/saved`      | Список сохранённых сетей `{"nets":["ssid1",...]}`. |
| POST  | `/api/wifi/delete`     | Удалить сохранённую сеть (`ssid=...`). |
| POST  | `/api/reconnect`       | Принудительное переподключение к лучшей известной сети. |
| POST  | `/api/start_ap`        | Перейти в AP-режим. |
| POST  | `/ota`                 | Загрузить прошивку `.bin`. |
| GET   | `/api/ota/info`        | JSON: `controller` (`ESP.getChipModel()`), `currentVersion` (`TKWM_FW_VERSION`). |
| GET   | `/api/ota/config`      | JSON: `host`, `token`, `auto`, `hasCreds` (из `ota.conf` + Preferences). |
| POST  | `/api/ota/check`      | JSON body: `host`, `token` (опц., иначе из `ota.conf`), `skipVersion` (опц.). Ответ: `updateAvailable`, версии, ошибка ESPConnect. |
| POST  | `/api/ota/install`    | Скачать с ESPConnect и прошить (тело как у check). |
| POST  | `/api/ota/save`        | Сохранить `auto` в `Preferences` (JSON: `"auto": true/false`). |

> **`/api/wifi/scan`** — REST-аналог WS-команды `"scan"`. Удобен для простых страниц без WebSocket (см. внешний `wifi.html` в FS).

---

## WebSocket API

**Порт (по умолчанию):** `ws://<host>:81/`. Меняется макросом `TKWM_WS_PORT` (в т.ч. встроенные HTML подставляют тот же номер).

### Встроенные входящие команды (текст)

| Команда    | Ответ библиотеки |
|------------|-----------------|
| `"status"` | `{"type":"status","mode":"AP\|STA","ip":"..."}` |
| `"scan"`   | `{"type":"scan","nets":[{"ssid":"...","rssi":-70,"ch":6,"enc":0\|1},...]}` |

### При подключении нового клиента

Библиотека автоматически отправляет `{"type":"status",...}` новому клиенту.  
> **Важно:** событие `WStype_CONNECTED` **не передаётся** в пользовательский хук — оно перехватывается библиотекой. Если вам нужно отреагировать на подключение, попросите клиент сразу после connect отправить текстовое сообщение (например, `"hello"`).

### Пользовательский хук

Всё, что не является `"scan"` или `"status"`, а также события `WStype_DISCONNECTED`, `WStype_BIN`, `WStype_PING/PONG` — попадают в хук:

```cpp
wifiMgr.setUserWsHook([](uint8_t id, WStype_t type, const uint8_t* payload, size_t len) {
    if (type == WStype_DISCONNECTED) {
        Serial.printf("клиент %u отключился\n", id);
        return;
    }
    if (type != WStype_TEXT) return;
    String s((char*)payload, len);

    if (s == "ping") {
        wifiMgr.ws().sendTXT(id, String("{\"type\":\"pong\",\"t\":") + millis() + "}");
        return;
    }
    // ... ваши команды
    wifiMgr.ws().sendTXT(id, "{\"type\":\"error\",\"msg\":\"unknown\"}");
});
```

### Отправка из прошивки

```cpp
// одному клиенту
wifiMgr.ws().sendTXT(clientId, "{\"type\":\"data\",\"v\":42}");

// всем клиентам
wifiMgr.ws().broadcastTXT("{\"type\":\"alert\",\"msg\":\"hello\"}");

// бинарный фрейм
uint8_t buf[] = {0x01, 0x02};
wifiMgr.ws().broadcastBIN(buf, 2);
```

### Пример: периодический push данных с датчика

```cpp
uint32_t lastPush = 0;

void loop() {
    wifiMgr.loop();

    if (millis() - lastPush > 1000) {
        lastPush = millis();
        float temp = readSensor();
        String j = String("{\"type\":\"sensor\",\"temp\":") + temp + "}";
        wifiMgr.ws().broadcastTXT(j);
    }
}
```

---

## Темы оформления

Все встроенные страницы (`/`, `/wifi`, `/fs`, `/ota`) поддерживают **тёмную и светлую тему** через CSS-переменные.

### Как это работает

Каждая страница (и встроенная в прошивку, и из FS) содержит:

1. Встроенный `<style>` с переменными (тёмная тема по умолчанию + `@media prefers-color-scheme`).
2. `<link rel="stylesheet" href="/theme.css">` — подгружает ваш файл из FS (если загружен).
3. `<script src="/theme.js">` — подгружает переключатель тем из FS (если загружен).

### Уровни приоритета

```
Без файлов в FS:
  → тёмная тема + системная (prefers-color-scheme) работает автоматически

С /theme.css в FS:
  → ваши цвета перекрывают встроенные

С /theme.js в FS:
  → кнопка 🌙 ☀️ 💻 в правом нижнем углу всех страниц
  → выбор сохраняется в localStorage браузера
```

### CSS-переменные темы

| Переменная  | Тёмная     | Светлая    | Назначение |
|-------------|-----------|-----------|------------|
| `--bg`      | `#0b1220` | `#f0f4f8` | Фон страницы |
| `--card`    | `#0d1728` | `#ffffff` | Фон карточки/панели |
| `--surface` | `#0f1a2c` | `#e4eaf2` | Фон полей ввода, элементов |
| `--ink`     | `#e8eef7` | `#1a2236` | Основной текст |
| `--mut`     | `#9fb3d1` | `#5a7090` | Второстепенный текст |
| `--br`      | `#1b2a44` | `#c5d0e0` | Рамки и разделители |
| `--btn`     | `#143057` | `#2563eb` | Фон кнопок |
| `--link`    | `#9fd0ff` | `#1d4ed8` | Ссылки |
| `--ok`      | `#95ffa1` | `#16a34a` | Успех / подключено |
| `--err`     | `#ff9a9a` | `#dc2626` | Ошибка |

### Загрузить в FS для активации переключателя

```
data/
  theme.css   ← цвета (тёмная + светлая + [data-theme])
  theme.js    ← кнопка-переключатель 🌙/☀️/💻
```

### Пример кастомного theme.css (только акценты, без смены тем)

```css
/* Фиолетовые кнопки поверх стандартной тёмной темы */
:root {
    --btn:  #7c3aed;
    --link: #a78bfa;
}
```

---

## Подмена встроенных страниц

Загрузите в FS файл с соответствующим именем — сервер отдаст его вместо встроенного:

| FS-файл       | Заменяет страницу |
|---------------|-------------------|
| `/index.html` | `/` (главная)     |
| `/wifi.html`  | `/wifi`           |
| `/fs.html`    | `/fs`             |
| `/ota.html`   | `/ota`            |

Готовые шаблоны с полной поддержкой тем лежат в папке `src/` репозитория.

---

## Добавление своих HTTP-маршрутов

```cpp
// В setup(), после wifiMgr.begin():

wifiMgr.addRoute("/hello", HTTP_GET, []() {
    String s = "Hello!\nIP: " + wifiMgr.ip().toString();
    wifiMgr.web().send(200, "text/plain; charset=utf-8", s);
});

wifiMgr.addRoute("/api/data", HTTP_GET, []() {
    String json = "{\"temp\":23.5,\"hum\":60}";
    wifiMgr.web().send(200, "application/json", json);
});

wifiMgr.addRoute("/api/cmd", HTTP_POST, []() {
    String body = wifiMgr.web().arg("plain"); // JSON-тело
    // или wifiMgr.web().arg("param")          // form-параметр
    wifiMgr.web().send(200, "application/json", "{\"ok\":true}");
});
```

`wifiMgr.web()` — ссылка на внутренний `WebServer`.  
`wifiMgr.ws()`  — ссылка на внутренний `WebSocketsServer`.

---

## Пользовательский WS-хук

```cpp
wifiMgr.setUserWsHook([](uint8_t id, WStype_t type, const uint8_t* payload, size_t len) {
    // type: WStype_DISCONNECTED, WStype_TEXT, WStype_BIN, WStype_PING, WStype_PONG
    // WStype_CONNECTED сюда НЕ попадает (перехватывает библиотека)
    if (type != WStype_TEXT) return;
    String s((char*)payload, len);
    // ... обработка команд
});
```

---

## Компиляционные макросы

Определите до `#include <TKWifiManager.h>`:

| Макрос | По умолчанию | Описание |
|--------|-------------|----------|
| `TKWM_USE_LITTLEFS` | `1` | `1` — LittleFS, `0` — SPIFFS |
| `TKWM_WS_PORT` | `81` | Порт WebSocket-сервера |
| `TKWM_DISCOVERY_PORT` | `64242` | UDP-порт для discovery |
| `TKWM_DISCOVERY_SIGNATURE` | `"TK_DISCOVER:1"` | Префикс UDP-запроса |
| `TKWM_MAX_CRED` | `16` | Максимум сохранённых Wi-Fi профилей |
| `TKWM_WIFI_COUNTRY` | `"EU"` | Код региона для `esp_wifi_set_country` (например `"00"` — world) |
| `TKWM_FW_VERSION` | `"0.0.0"` | Версия прошивки для сравнения с сервером (в релизе: `build_flags = -DTKWM_FW_VERSION=\\\"1.2.3\\\"` в **вашем** проекте) |
| `TKWM_OTA_INSECURE` | `0` | `1` — `WiFiClientSecure::setInsecure()` для OTA/ESPConnect (только тест/self-signed; не для продакшена) |

```cpp
#define TKWM_WS_PORT    9000
#define TKWM_MAX_CRED   8
#include <TKWifiManager.h>
```

---

## ESPConnect OTA (ESPTools)

Сервер **ESPConnect** (ESPTools) отдаёт прошивки по API. Запросы к интернету выполняет **только ESP32** (C++), токен API не уходит в чужой origin из браузера.

- **Проверка/разрешение скачивания:** `POST {host}/api/firmware/resolve-download` с заголовком `Authorization: Bearer <API JWT>`. В теле JSON: обязательно `controller` (на устройстве — `String(ESP.getChipModel())` без ручного выбора; совпадение с полем *controller* в кабинете ESPTools — ваш ответственность, при необходимости нормализуйте имя в UI сервера или в макросе), опционально `firmware_type` (по умолчанию `firmware`).
- **Скачивание `.bin`:** `GET {host}{download_url}` из ответа (тот же `Bearer`).

**Токен:** в ESPConnect нужен **API JWT, привязанный к проекту** (создаётся в UI, обычно `POST /api/tokens` с `project_name`). **Хост** в веб-форме — базовый URL бэкенда (без завершающего `/`).

**Файл `ota.conf` (LittleFS, путь `/ota.conf`):** строки `key=value` — `host`, `token`, `auto` (`0`/`1`, `true`/`false` и т.д.). Пустые поля на странице `/ota` при открытии подставляются из файла. **Токен** в JSON `/api/ota/config` отдаётся в той же сети, что и веб-интерфейс; не выставляйте портал в публичную сеть без дополнительной защиты.

**Флаги `auto`:** чекбокс на `/ota` сохраняет значение в `Preferences` (namespace `tkw_ota`, ключ `auto`). Если в `ota.conf` задано `auto`, оно **имеет приоритет** над `Preferences` (см. логика в библиотеке).

**JSON API на устройстве (для веб-страницы):** `GET /api/ota/info` (текущая версия, контроллер), `GET /api/ota/config`, `POST /api/ota/check`, `POST /api/ota/install`, `POST /api/ota/save` (только `auto` в теле).

**Версии:** договоритесь об одном формате (например SemVer) между релизами в ESPTools и `TKWM_FW_VERSION`; сравнение в прошивке — **строковое**.

**Сборка встроенного HTML:** `src/ota.html` — эталон для оверлея в FS; встроенный PROGMEM генерируется в `src/TKWifiManager_ota.inc` командой `py src/_gen_ota_inc.py` из корня библиотеки.

---

## UDP-discovery

Менеджер слушает `TKWM_DISCOVERY_PORT` и на пакет с префиксом `TKWM_DISCOVERY_SIGNATURE` отвечает:

```json
{
  "id":    "MyDevice-A1B2C3",
  "name":  "MyDevice-A1B2C3",
  "ip":    "192.168.1.50",
  "model": "ESP32",
  "web":   "/"
}
```

Пример сканера (Python):

```python
import socket, time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.settimeout(2)
sock.sendto(b"TK_DISCOVER:1", ("255.255.255.255", 64242))

while True:
    try:
        data, addr = sock.recvfrom(512)
        print(addr[0], data.decode())
    except socket.timeout:
        break
```

---

## Полный пример

```cpp
// examples/Basic/Basic.ino

#define TKWM_USE_LITTLEFS 1
#include <Arduino.h>
#include <TKWifiManager.h>

TKWifiManager wifiMgr(80);

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif
const int LED_PIN = LED_BUILTIN;
bool      ledOn   = false;
uint32_t  blinkMs = 0, lastBlinkMs = 0;

// ── Пользовательский WS-хук ──────────────────────────────────────────────────
// Команды: "ping", "get-info", "led:on", "led:off", "led:blink:<ms>"
static void myWsHook(uint8_t id, WStype_t type, const uint8_t* payload, size_t len) {
    if (type == WStype_DISCONNECTED) {
        Serial.printf("[WS] клиент %u отключился\n", id);
        return;
    }
    if (type != WStype_TEXT) return;

    String s((char*)payload, len);

    if (s == "ping") {
        wifiMgr.ws().sendTXT(id, String("{\"type\":\"pong\",\"t\":") + millis() + "}");
        return;
    }
    if (s == "get-info") {
        String j = "{\"type\":\"info\",\"mode\":\"";
        j += wifiMgr.inCaptive() ? "AP" : "STA";
        j += "\",\"ip\":\"" + wifiMgr.ip().toString() + "\"";
        j += ",\"rssi\":"    + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
        j += ",\"uptime_ms\":" + String(millis()) + "}";
        wifiMgr.ws().sendTXT(id, j);
        return;
    }
    if (s == "led:on")  { blinkMs = 0; ledOn = true;  digitalWrite(LED_PIN, HIGH); return; }
    if (s == "led:off") { blinkMs = 0; ledOn = false; digitalWrite(LED_PIN, LOW);  return; }
    if (s.startsWith("led:blink:")) {
        long p = s.substring(10).toInt();
        blinkMs = (uint32_t)max(p, 50L);
        lastBlinkMs = millis();
        return;
    }
    wifiMgr.ws().sendTXT(id, "{\"type\":\"error\",\"msg\":\"unknown cmd\"}");
}

// ── Пользовательские HTTP-маршруты ───────────────────────────────────────────
void setupRoutes() {
    wifiMgr.addRoute("/hello", HTTP_GET, []() {
        String s = "Hello from TKWifiManager!\nIP: " + wifiMgr.ip().toString();
        wifiMgr.web().send(200, "text/plain; charset=utf-8", s);
    });

    wifiMgr.addRoute("/api/led", HTTP_POST, []() {
        String cmd = wifiMgr.web().arg("cmd");
        if      (cmd == "on")  { blinkMs=0; ledOn=true;  digitalWrite(LED_PIN,HIGH); }
        else if (cmd == "off") { blinkMs=0; ledOn=false; digitalWrite(LED_PIN,LOW);  }
        else if (cmd == "blink") {
            blinkMs = max((uint32_t)wifiMgr.web().arg("ms").toInt(), (uint32_t)50);
            lastBlinkMs = millis();
        } else {
            wifiMgr.web().send(400, "application/json", "{\"ok\":false}");
            return;
        }
        wifiMgr.web().send(200, "application/json", "{\"ok\":true}");
    });
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    wifiMgr.begin(/*apSsidPrefix=*/"DemoTKWM", /*formatFS=*/true);

    setupRoutes();
    wifiMgr.setUserWsHook(myWsHook);

    Serial.println(F("[EXAMPLE] Готово. /wifi  /fs  /ota  /hello"));
}

uint32_t lastAnnounce = 0;
void loop() {
    wifiMgr.loop();

    // мигание LED
    if (blinkMs && millis() - lastBlinkMs >= blinkMs) {
        lastBlinkMs = millis();
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
    }

    // периодический broadcast статуса (раз в 5 сек)
    if (millis() - lastAnnounce > 5000) {
        lastAnnounce = millis();
        String j = String("{\"type\":\"status\",\"mode\":\"")
            + (wifiMgr.inCaptive() ? "AP" : "STA")
            + "\",\"ip\":\"" + wifiMgr.ip().toString() + "\"}";
        wifiMgr.ws().broadcastTXT(j);
    }
}
```

---

## Частые вопросы

**FS монтируется, но файла нет**  
Убедитесь, что в `Partition Scheme` выбрана разметка с LittleFS (минимум 1 МБ под FS). В Serial-логе при `begin()` должно быть: `[TKWM] FS = LittleFS` и `FS mount: OK`.

**Как сменить тему оформления?**  
Загрузите в FS файл `/theme.css` с нужными значениями CSS-переменных. Для кнопки-переключателя 🌙/☀️/💻 дополнительно загрузите `/theme.js`. Готовые файлы лежат в `src/` репозитория.

**Как заменить встроенные страницы?**  
Загрузите в FS `wifi.html` / `fs.html` / `ota.html` / `index.html` — сервер автоматически отдаст их вместо встроенных.

**Работает ли со SPIFFS?**  
Да: `#define TKWM_USE_LITTLEFS 0` до `#include <TKWifiManager.h>`. LittleFS рекомендуется (более надёжная).

**Как добавить поля в UDP-ответ?**  
Отредактируйте метод `udpTick()` в `TKWifiManager.cpp` — найдите формирование `payload` и добавьте нужные поля.

**Ace-редактор не загружается (нет интернета)**  
Файловый менеджер работает и без него — кнопки открыть/скачать/удалить/создать доступны. Если нужен офлайн-редактор, скачайте `ace.js` и загрузите в FS, затем замените CDN-ссылку в `fs.html`.
