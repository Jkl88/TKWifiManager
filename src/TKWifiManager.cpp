#include "TKWifiManager.h"
#include "esp_wifi.h"

// ===== Конструктор =====
TKWifiManager::TKWifiManager(uint16_t httpPort)
    : _httpPort(httpPort),
    _server(httpPort),
    _ws(TKWM_WS_PORT) {
}

// ===== Публичное =====
bool TKWifiManager::begin(bool formatFSIfNeeded, const char* apPrefix) {
    // --- FS ---
    _fsOk = TKWM_FS.begin(true);
    if (!_fsOk && formatFSIfNeeded) {
        Serial.println(F("[FS] mount failed; formatting..."));
        TKWM_FS.format();
        _fsOk = TKWM_FS.begin(true);
    }
    if (_fsOk) {
        size_t total = TKWM_FS.totalBytes();
        size_t used = TKWM_FS.usedBytes();
        Serial.printf("[FS] mounted: used %u / %u bytes\n", (unsigned)used, (unsigned)total);
    }
    else {
        Serial.println(F("[FS] not mounted — FS UI limited"));
    }

    // --- Wi-Fi creds ---
    loadCreds();

    // --- Пытаемся STA ---
    bool connected = tryConnectBestKnown();
    if (!connected) startCaptiveAP(apPrefix);

    // --- Сервисы ---
    setupRoutes();
    _server.begin();

    // WebSocket
    setupWebSocket();
    _ws.begin();

    // --- Скан надёжность ---
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // --- UDP discovery ---
    if (_discoveryEnabled) _udp.begin(TKWM_DISCOVERY_PORT);

    return true;
}

void TKWifiManager::loop() {
    if (_captive) _dns.processNextRequest();
    _server.handleClient();
    _ws.loop();
    udpTick();

    // Автовозврат в AP, если отвалились
    static uint32_t t = 0;
    if (!_captive && millis() - t > 5000) {
        t = millis();
        if (WiFi.status() != WL_CONNECTED) {
            if (!tryConnectBestKnown()) {
                startCaptiveAP("TK-Setup");
            }
        }
    }

    // Авто-скан: если есть WS-клиенты, каждые ~2с
    if (_wsHasClients && millis() > _nextAutoScanAt && !_scanInProgress) {
        wsKickScan();
        _nextAutoScanAt = millis() + 2000;
    }

    // Завершение async-скана
    if (_scanInProgress) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED) {
            wsKickScan(true); // sync fallback
        }
        else if (n >= 0) {
            _scanCache.clear();
            _scanCache.reserve(n);
            for (int i = 0; i < n; i++) {
                _scanCache.push_back(ScanNet{
                  String(WiFi.SSID(i)),
                  WiFi.RSSI(i),
                  (int)WiFi.encryptionType(i),
                  (uint8_t)WiFi.channel(i)
                    });
            }
            WiFi.scanDelete();
            _scanInProgress = false;
            wsPublishScan();
        }
    }
}

void TKWifiManager::addRoute(const String& path, HTTPMethod method, RouteHandler handler) {
    _server.on(path.c_str(), method, handler);
}

bool TKWifiManager::reconnectBestKnown() {
    if (_captive) {
        _dns.stop();
        WiFi.softAPdisconnect(true);
        _captive = false;
    }
    bool ok = tryConnectBestKnown();
    if (!ok) startCaptiveAP("TK-Setup");
    return ok;
}

// ===== Приватное: creds =====
void TKWifiManager::loadCreds() {
    _prefs.begin("tkwifi", true);
    _credCount = _prefs.getInt("count", 0);
    if (_credCount < 0 || _credCount > TKWM_MAX_CRED) _credCount = 0;
    for (int i = 0; i < _credCount; i++) {
        _creds[i].ssid = _prefs.getString(("s" + String(i)).c_str(), "");
        _creds[i].pass = _prefs.getString(("p" + String(i)).c_str(), "");
    }
    _prefs.end();
}

void TKWifiManager::saveCount() {
    _prefs.begin("tkwifi", false);
    _prefs.putInt("count", _credCount);
    _prefs.end();
}

void TKWifiManager::saveAt(int idx, const String& ssid, const String& pass) {
    if (idx < 0 || idx >= TKWM_MAX_CRED) return;
    _prefs.begin("tkwifi", false);
    _prefs.putString(("s" + String(idx)).c_str(), ssid);
    _prefs.putString(("p" + String(idx)).c_str(), pass);
    _prefs.end();
}

int TKWifiManager::findBySsid(const String& ssid) {
    for (int i = 0; i < _credCount; i++) if (_creds[i].ssid == ssid) return i;
    return -1;
}

// ===== Приватное: Wi-Fi =====
bool TKWifiManager::tryConnectBestKnown(uint32_t timeoutMs) {
    if (_credCount == 0) return false;

    int n = WiFi.scanNetworks(false, true);
    int bestRssi = -9999, bestIdx = -1;
    String bestSsid, bestPass;
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        int idx = findBySsid(s);
        if (idx >= 0 && rssi > bestRssi) {
            bestRssi = rssi; bestIdx = idx;
            bestSsid = _creds[idx].ssid; bestPass = _creds[idx].pass;
        }
    }
    if (bestIdx < 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(bestSsid.c_str(), bestPass.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(250);
    return WiFi.status() == WL_CONNECTED;
}

void TKWifiManager::startCaptiveAP(const char* apPrefix) {
    _captive = true;

    // Уникальный SSID: <prefix>-XXXXXX
    uint64_t mac = ESP.getEfuseMac();
    char macs[7];
    snprintf(macs, sizeof(macs), "%06X", (uint32_t)(mac & 0xFFFFFF));
    _apSsid = String(apPrefix) + "-" + macs;

    WiFi.mode(WIFI_AP_STA);
    IPAddress apIP(192, 168, 4, 1), net(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, net);
    WiFi.softAP(_apSsid.c_str(), nullptr, 6, false, 4);

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    _dns.stop();
    _dns.start(53, "*", apIP);
}

String TKWifiManager::ipToStr(IPAddress ip) {
    return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

// ===== UDP discovery =====
void TKWifiManager::udpTick() {
    if (!_discoveryEnabled) return;
    int sz = _udp.parsePacket();
    if (sz <= 0) return;
    char buf[64] = { 0 };
    int n = _udp.read(buf, sizeof(buf) - 1);
    buf[n] = 0;
    if (!String(buf).startsWith(TKWM_DISCOVERY_SIGNATURE)) return;

    String payload = String("{\"id\":\"") + _apSsid + "\","
        "\"name\":\"" + _apSsid + "\","
        "\"ip\":\"" + (_captive ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\","
        "\"model\":\"ESP32\","
        "\"fw\":\"" + String(ESP.getSdkVersion()) + "\","
        "\"web\":\"/\"}";
    _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
    _udp.print(payload);
    _udp.endPacket();
}

// ===== WebSocket =====
void TKWifiManager::setupWebSocket() {
    _ws.onEvent([this](uint8_t id, WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
        case WStype_CONNECTED: {
            _wsHasClients = true;
            wsPublishStatus(id);
            break;
        }
        case WStype_DISCONNECTED: {
            _wsHasClients = (_ws.connectedClients() > 0);
            break;
        }
        case WStype_TEXT: {
            // Примитивный протокол:
            // "scan"      → запустить скан и прислать {"type":"scan", nets:[...]}
            // "status"    → прислать {"type":"status", ...}
            // "scan_once" → одноразовый sync-скан (быстро проверить)
            String s; s.reserve(length);
            for (size_t i = 0; i < length; i++) s += (char)payload[i];

            if (s == "scan") {
                wsKickScan();
            }
            else if (s == "scan_once") {
                wsKickScan(true); // sync
            }
            else if (s == "status") {
                wsPublishStatus(id);
            }
            else {
                // Пользовательский хендлер (можно отправлять JSON/команды своей логики)
                if (_wsUserHandler) _wsUserHandler(id, type, payload, length);
            }
            break;
        }
        default: {
            if (_wsUserHandler) _wsUserHandler(id, type, payload, length);
            break;
        }
        }
        });
}

void TKWifiManager::wsKickScan(bool forceSyncFallback) {
    if (!forceSyncFallback) {
        if (!_scanInProgress) {
            int rc = WiFi.scanNetworks(true /* async */, true /* hidden */);
            if (rc == WIFI_SCAN_RUNNING) {
                _scanInProgress = true;
                _scanLastKick = millis();
            }
            else {
                // если не запустился — упадём в sync
                forceSyncFallback = true;
            }
        }
    }
    if (forceSyncFallback) {
        int m = WiFi.scanNetworks(false, true);
        _scanCache.clear();
        _scanCache.reserve(m);
        for (int i = 0; i < m; i++) {
            _scanCache.push_back(ScanNet{
              String(WiFi.SSID(i)),
              WiFi.RSSI(i),
              (int)WiFi.encryptionType(i),
              (uint8_t)WiFi.channel(i)
                });
        }
        WiFi.scanDelete();
        _scanInProgress = false;
        wsPublishScan();
    }
}

void TKWifiManager::wsPublishScan() {
    // Ручная сборка JSON (без ArduinoJson)
    String j = "{\"type\":\"scan\",\"nets\":[";
    for (size_t i = 0; i < _scanCache.size(); ++i) {
        if (i) j += ",";
        const auto& o = _scanCache[i];
        j += "{\"ssid\":\"";
        // Экранируем ковычки
        for (size_t k = 0; k < o.ssid.length(); k++) {
            char c = o.ssid[k];
            if (c == '"' || c == '\\') { j += '\\'; j += c; }
            else j += c;
        }
        j += "\",\"rssi\":";
        j += String(o.rssi);
        j += ",\"enc\":";
        j += String(o.enc);
        j += ",\"ch\":";
        j += String(o.ch);
        j += "}";
    }
    j += "]}";
    _ws.broadcastTXT(j);
}

void TKWifiManager::wsPublishStatus(uint8_t clientId) {
    String mode = _captive ? "AP" : "STA";
    String ip = _captive ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    String j = String("{\"type\":\"status\",\"mode\":\"") + mode + "\",\"ip\":\"" + ip + "\",\"ap\":\"" + _apSsid + "\"}";
    _ws.sendTXT(clientId, j);
}

// ===== HTTP Routes =====
void TKWifiManager::setupRoutes() {
    // Главная
    _server.on("/", HTTP_GET, [this] { handleRoot(); });

    // Портал/каптив триггеры
    auto captiveRedirect = [this]() {
        if (_captive) {
            _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/wifi");
            _server.send(302, "text/plain", "");
        }
        else {
            _server.send(204);
        }
        };
    _server.on("/generate_204", HTTP_ANY, captiveRedirect);     // Android
    _server.on("/gen_204", HTTP_ANY, captiveRedirect);     // Chrome
    _server.on("/hotspot-detect.html", HTTP_ANY, captiveRedirect); // Apple
    _server.on("/library/test/success.html", HTTP_ANY, captiveRedirect);
    _server.on("/ncsi.txt", HTTP_ANY, captiveRedirect);         // Windows
    _server.on("/connecttest.txt", HTTP_ANY, captiveRedirect);

    // Встроенные ссылки
    _server.on("/wifi", HTTP_GET, [this]() {
        // Сначала пробуем отдать /wifi.html из FS
        if (fsTryStream("/wifi.html")) return;

        // Фоллбэк — минимальная страница с WS-сканером
        String h = htmlHeader("Wi-Fi");
        h += "<div class='card'><h1>Wi-Fi</h1>"
            "<p class='mut'>Это минимальная страница. Загрузите <code>/wifi.html</code> в FS для кастомного UI.</p>"
            "<div><button id='btnScan'>Сканировать</button> <span id='st'></span></div>"
            "<div id='nets' style='margin-top:10px'></div>"
            "<script>"
            "let ws;function c(){ws=new WebSocket('ws://'+location.hostname+':" + String(TKWM_WS_PORT) + "');"
            "ws.onopen=()=>{st.textContent='WS ok';};"
            "ws.onclose=()=>{st.textContent='WS close'; setTimeout(c,1000)};"
            "ws.onmessage=(ev)=>{let j=JSON.parse(ev.data);"
            " if(j.type==='scan'){let h=''; j.nets.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{h+=`<div class=pill>${n.ssid||'<i>hidden</i>'} (${n.rssi} dBm ch${n.ch})</div>`}); nets.innerHTML=h||'<i class=mut>пусто</i>';}"
            " if(j.type==='status'){console.log('status',j);}"
            "};}"
            "c();"
            "btnScan.onclick=()=>ws.send('scan_once');"
            "</script></div>";
        h += htmlFooter();
        _server.send(200, "text/html; charset=utf-8", h);
        });

    // API creds
    _server.on("/api/creds", HTTP_GET, [this] { handleCredsList(); });
    _server.on("/api/creds/add", HTTP_POST, [this] { handleCredsAdd();  });
    _server.on("/api/creds/del", HTTP_POST, [this] { handleCredsDel();  });
    _server.on("/api/reconnect", HTTP_POST, [this] { handleReconnect(); });
    _server.on("/api/start_ap", HTTP_POST, [this] { handleStartAP();   });

    // FS
    _server.on("/fs", HTTP_GET, [this] { handleFsList(); });
    _server.on("/fs/get", HTTP_GET, [this] { handleFsGet();  });
    _server.on("/fs/put", HTTP_POST, [this] { handleFsPut();  });
    _server.on("/fs/del", HTTP_POST, [this] { handleFsDelete(); });
    _server.on("/fs/edit", HTTP_GET, [this] { handleFsEdit(); });
    _server.on("/fs/edit", HTTP_POST, [this] { handleFsEdit(); });

    // Загрузка произвольного файла в FS (multipart)
    _server.on("/upload", HTTP_POST,
        [this]() {
            String to = _fsUploadTo;
            if (to.isEmpty()) to = "/";
            String resp = "<!doctype html><meta charset='utf-8'>OK. <a href='" + to + "'>Открыть " + to + "</a>";
            _server.send(200, "text/html; charset=utf-8", resp);
            _fsUploadTo = "";
        },
        [this] { handleUpload(); }
    );

    // OTA
    _server.on("/ota", HTTP_GET, [this] { handleOtaPage();   });
    _server.on("/ota", HTTP_POST, [this] { /* ответ в конце */ }, [this] { handleOtaUpload(); });

    // notFound
    _server.onNotFound([this] { handleNotFound(); });
}

// ===== HTTP handlers =====
void TKWifiManager::handleRoot() {
    // Если есть /index.html — отдадим его
    if (fsTryStream("/index.html")) return;

    // Иначе — портал
    _server.send(200, "text/html; charset=utf-8", portalPage());
}

void TKWifiManager::handleNotFound() {
    String uri = _server.uri();

    // Отдать файл из FS если существует
    if (_fsOk && fsTryStream(uri)) return;

    // В каптиве — редирект на /wifi
    if (_captive) {
        _server.sendHeader("Location", "/wifi", true);
        _server.send(302, "text/plain", "");
        return;
    }

    // Иначе — 404 с формой загрузки
    sendUpload404(uri);
}

// ===== FS =====
void TKWifiManager::handleFsList() {
    if (!_fsOk) { _server.send(500, "text/plain", "FS not mounted"); return; }
    if (_server.hasArg("html")) {
        String s = htmlHeader("FS") +
            "<div class='card'><h1>Файловая система</h1>"
            "<form method='POST' action='/upload?to=/' enctype='multipart/form-data'>"
            "<input type='file' name='file'><button>Загрузить</button></form>"
            "<table><tr><th>Имя</th><th>Размер</th><th>Действия</th></tr>";
        File r = TKWM_FS.open("/");
        File f;
        while ((f = r.openNextFile())) {
            String p = String(f.name());
            s += "<tr><td><a href='/fs/get?path=" + p + "'>" + p + "</a></td><td>" + String(f.size()) + "</td>"
                "<td>"
                "<form style='display:inline' method='GET' action='/fs/edit'><input type='hidden' name='path' value='" + p + "'><button>Редактировать</button></form> "
                "<button type='button' onclick=\"delFile('" + p + "')\">Удалить</button>"
                "</td></tr>";
            f.close();
        }
        s += "</table>"
            "<script>"
            "async function delFile(p){if(!confirm('Удалить '+p+'?'))return;"
            "let f=new FormData();f.append('path',p);"
            "let r=await fetch('/fs/del',{method:'POST',body:f});"
            "if(r.ok){location.reload()}else{alert('Ошибка удаления')}}"
            "</script></div>" + htmlFooter();
        _server.send(200, "text/html; charset=utf-8", s);
    }
    else {
        String j = "[";
        File r = TKWM_FS.open("/");
        File f;
        bool first = true;
        while ((f = r.openNextFile())) {
            if (!first) j += ",";
            first = false;
            j += "{\"path\":\"" + String(f.name()) + "\",\"size\":" + String(f.size()) + "}";
            f.close();
        }
        j += "]";
        _server.send(200, "application/json", j);
    }
}

void TKWifiManager::handleFsGet() {
    if (!_server.hasArg("path")) { _server.send(400, "text/plain", "No path"); return; }
    String p = _server.arg("path");
    if (!p.startsWith("/")) p = "/" + p;
    if (!TKWM_FS.exists(p)) { _server.send(404, "text/plain", "Not found"); return; }
    File f = TKWM_FS.open(p, "r");
    _server.streamFile(f, fsContentType(p));
    f.close();
}

void TKWifiManager::handleFsPut() {
    // Сохранение текстового файла: path + body (plain)
    if (!_server.hasArg("path")) { _server.send(400, "application/json", "{\"ok\":false}"); return; }
    String p = _server.arg("path");
    if (!p.startsWith("/")) p = "/" + p;
    String body = _server.arg("plain");
    fsEnsureDirs(p);
    File f = TKWM_FS.open(p, "w");
    if (!f) { _server.send(500, "application/json", "{\"ok\":false,\"msg\":\"open failed\"}"); return; }
    size_t wrote = f.print(body);
    f.close();
    String resp = String("{\"ok\":true,\"wrote\":") + wrote + "}";
    _server.send(200, "application/json", resp);
}

void TKWifiManager::handleFsDelete() {
    if (!_server.hasArg("path")) { _server.send(400, "application/json", "{\"ok\":false}"); return; }
    String p = _server.arg("path");
    if (!p.startsWith("/")) p = "/" + p;
    bool ok = TKWM_FS.remove(p);
    _server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void TKWifiManager::handleFsEdit() {
    if (_server.method() == HTTP_GET) {
        if (!_server.hasArg("path")) { _server.send(400, "text/plain", "No path"); return; }
        String p = _server.arg("path");
        if (!p.startsWith("/")) p = "/" + p;
        String txt;
        if (TKWM_FS.exists(p)) {
            File f = TKWM_FS.open(p, "r");
            if (looksText(f)) while (f.available()) txt += (char)f.read();
            f.close();
        }
        String s = htmlHeader("Редактор") +
            "<div class='card'><h1>Редактор</h1>"
            "<form method='POST' action='/fs/edit'>"
            "<input type='hidden' name='path' value='" + p + "'>"
            "<textarea name='text' style='width:100%;height:60vh;font-family:monospace'>" + txt + "</textarea>"
            "<div style='margin-top:8px'><button>Сохранить</button></div></form></div>" + htmlFooter();
        _server.send(200, "text/html; charset=utf-8", s);
    }
    else {
        String p = _server.arg("path");
        if (!p.startsWith("/")) p = "/" + p;
        String txt = _server.arg("text");
        File f = TKWM_FS.open(p, "w");
        f.print(txt);
        f.close();
        _server.sendHeader("Location", "/fs?html=1", true);
        _server.send(302);
    }
}

void TKWifiManager::handleUpload() {
    HTTPUpload& up = _server.upload();
    if (up.status == UPLOAD_FILE_START) {
        _fsUploadTo = _server.arg("to");
        String name = up.filename;
        if (_fsUploadTo.length()) name = _fsUploadTo;
        if (!name.startsWith("/")) name = "/" + name;
        fsEnsureDirs(name);
        _fsUploadFile = TKWM_FS.open(name, "w");
    }
    else if (up.status == UPLOAD_FILE_WRITE) {
        if (_fsUploadFile) _fsUploadFile.write(up.buf, up.currentSize);
    }
    else if (up.status == UPLOAD_FILE_END) {
        if (_fsUploadFile) _fsUploadFile.close();
    }
}

// ===== OTA =====
void TKWifiManager::handleOtaPage() {
    _server.send(200, "text/html; charset=utf-8", otaPage());
}

void TKWifiManager::handleOtaUpload() {
    static bool   otaInProgress = false;
    static size_t otaWritten = 0;
    static String otaError;

    HTTPUpload& up = _server.upload();
    if (up.status == UPLOAD_FILE_START) {
        otaInProgress = true; otaWritten = 0; otaError = "";
        size_t sz = up.totalSize;
        bool ok = Update.begin(sz ? sz : UPDATE_SIZE_UNKNOWN);
        if (!ok) otaError = String("Update.begin failed: ") + Update.errorString();
    }
    else if (up.status == UPLOAD_FILE_WRITE) {
        if (otaError.isEmpty()) {
            size_t w = Update.write(up.buf, up.currentSize);
            if (w != up.currentSize) otaError = String("Write failed: ") + Update.errorString();
            else otaWritten += w;
        }
    }
    else if (up.status == UPLOAD_FILE_END) {
        if (otaError.isEmpty()) {
            if (!Update.end(true)) otaError = String("Update.end failed: ") + Update.errorString();
        }
        else {
            Update.abort();
        }
        otaInProgress = false;

        if (otaError.length()) {
            String html = String("<!doctype html><meta charset='utf-8'><title>OTA</title>"
                "<h3 style='color:#ff9a9a'>Ошибка OTA</h3><pre>") + otaError + "</pre>";
            _server.send(200, "text/html; charset=utf-8", html);
        }
        else {
            String html = String("<!doctype html><meta charset='utf-8'><title>OTA</title>"
                "<h3 style='color:#95ffa1'>Готово</h3>"
                "<p>Записано: ") + otaWritten + " байт. Перезагрузка...</p>"
                "<script>setTimeout(()=>location.href='/',4000)</script>";
            _server.send(200, "text/html; charset=utf-8", html);
            delay(1500);
            ESP.restart();
        }
    }
}

// ===== API (creds) =====
void TKWifiManager::handleCredsList() {
    String j = "[";
    for (int i = 0; i < _credCount; i++) { if (i) j += ","; j += "{\"ssid\":\"" + _creds[i].ssid + "\"}"; }
    j += "]";
    _server.send(200, "application/json", j);
}

void TKWifiManager::handleCredsAdd() {
    if (!_server.hasArg("ssid")) { _server.send(400, "application/json", "{\"ok\":false}"); return; }
    String ssid = _server.arg("ssid");
    String pass = _server.hasArg("pass") ? _server.arg("pass") : "";
    int idx = findBySsid(ssid);
    if (idx >= 0) { _creds[idx].pass = pass; saveAt(idx, ssid, pass); }
    else {
        if (_credCount >= TKWM_MAX_CRED) { _server.send(507, "application/json", "{\"ok\":false,\"msg\":\"full\"}"); return; }
        _creds[_credCount] = { ssid, pass }; saveAt(_credCount, ssid, pass); _credCount++; saveCount();
    }
    _server.send(200, "application/json", "{\"ok\":true}");
}

void TKWifiManager::handleCredsDel() {
    if (!_server.hasArg("idx")) { _server.send(400, "application/json", "{\"ok\":false}"); return; }
    int idx = _server.arg("idx").toInt();
    if (idx < 0 || idx >= _credCount) { _server.send(400, "application/json", "{\"ok\":false}"); return; }
    for (int i = idx; i < _credCount - 1; i++) { _creds[i] = _creds[i + 1]; saveAt(i, _creds[i].ssid, _creds[i].pass); }
    _prefs.begin("tkwifi", false);
    _prefs.remove(("s" + String(_credCount - 1)).c_str());
    _prefs.remove(("p" + String(_credCount - 1)).c_str());
    _prefs.end();
    _credCount--; saveCount();
    _server.send(200, "application/json", "{\"ok\":true}");
}

void TKWifiManager::handleReconnect() {
    reconnectBestKnown();
    _server.send(200, "application/json", "{\"ok\":true}");
}

void TKWifiManager::handleStartAP() {
    startCaptiveAP("TK-Setup");
    _server.send(200, "application/json", "{\"ok\":true}");
}

// ===== Служебные =====
String TKWifiManager::htmlHeader(const String& title) {
    return String(
        "<!doctype html><html><head><meta charset='utf-8'/>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
        "<title>") + title + String("</title>"
            "<style>"
            ":root{--bg:#0b0d10;--card:#141820;--fg:#e6eaf0;--mut:#8a93a6;--acc:#4ea1ff}"
            "body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,Segoe UI,Roboto,Arial}"
            ".wrap{max-width:860px;margin:0 auto;padding:16px}"
            ".card{background:var(--card);border-radius:16px;padding:16px;box-shadow:0 8px 30px rgba(0,0,0,.35)}"
            "h1{font-size:20px;margin:0 0 12px}"
            "button,input,select{background:#1b2230;color:var(--fg);border:1px solid #2a3345;border-radius:10px;padding:8px 12px}"
            ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
            ".pill{display:inline-block;border:1px solid #2a3345;border-radius:999px;padding:6px 10px;margin:4px 6px 0 0}"
            ".mut{color:var(--mut)} .ok{color:#4cd964} .bad{color:#ff5f57}"
            "table{border-collapse:collapse;width:100%} td,th{border-bottom:1px solid #2a3345;padding:8px;text-align:left}"
            "textarea{width:100%;min-height:320px;background:#0f1219;color:var(--fg);border:1px solid #283142;border-radius:12px;padding:12px}"
            "a{color:#9fd0ff;text-decoration:none}"
            "</style></head><body><div class='wrap'>");
}

String TKWifiManager::htmlFooter() { return "</div></body></html>"; }

String TKWifiManager::portalPage() {
    String ip = _captive ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    String mode = _captive ? "AP (каптив)" : "STA";
    String s = htmlHeader("TK Wi-Fi Manager");
    s += "<div class='card'><h1>TK Wi-Fi Manager</h1>";
    s += "<p>Режим: " + mode + " • IP: <b>" + ip + "</b> • WS: <code>ws://" + ip + ":" + String(TKWM_WS_PORT) + "</code></p>";
    s += "<div class='row'>"
        "<a href='/wifi'><button type='button'>Wi-Fi</button></a>"
        "<a href='/fs?html=1'><button type='button'>Файлы</button></a>"
        "<a href='/ota'><button type='button'>OTA</button></a>"
        "<form method='POST' action='/api/reconnect' style='display:inline'><button>Переподключить</button></form>"
        "<form method='POST' action='/api/start_ap' style='display:inline'><button>Старт AP</button></form>"
        "</div>";
    s += "<p class='mut'>Загрузите свои страницы <code>/index.html</code>, <code>/wifi.html</code>, <code>/fs.html</code> для кастомного UI.</p>";
    s += "</div>";
    s += htmlFooter();
    return s;
}

String TKWifiManager::otaPage() {
    String s = htmlHeader("OTA") +
        "<div class='card'><h1>OTA обновление</h1>"
        "<form method='POST' action='/ota' enctype='multipart/form-data'>"
        "<input type='file' name='update' required accept='.bin'><button>Прошить</button></form>"
        "<p class='mut'>Загрузите .bin прошивки. Устройство перезагрузится автоматически.</p>"
        "</div>" + htmlFooter();
    return s;
}

void TKWifiManager::sendUpload404(const String& missingPath) {
    String p = missingPath;
    if (!p.startsWith("/")) p = "/" + p;

    String html;
    html.reserve(4096);
    html += F("<!doctype html><html lang='ru'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>404 — файла нет</title>"
        "<style>body{font:14px system-ui;background:#0b1220;color:#e8eef7;margin:0;padding:24px}"
        ".card{max-width:720px;margin:auto;background:#0d1728;border:1px solid #1b2a44;border-radius:12px;padding:16px}"
        "h1{font-size:18px;margin:0 0 12px}code{background:#111b2c;padding:2px 6px;border-radius:6px}"
        "input[type=file]{margin:12px 0}button{padding:8px 12px;border-radius:10px;border:1px solid #2f4e7a;background:#143057;color:#e8eef7;cursor:pointer}"
        "a{color:#9fd0ff;text-decoration:none}</style></head><body><div class='card'>");
    html += F("<h1>Файл не найден</h1><p>Запрошен путь: <code>");
    html += p;
    html += F("</code></p><form method='POST' action='/upload?to=");
    html += p;
    html += F("' enctype='multipart/form-data'>"
        "<p>Загрузить файл в этот путь:</p>"
        "<input type='file' name='file' required>"
        "<br><button type='submit'>Загрузить</button></form>"
        "<p style='opacity:.8;margin-top:12px'>Подсказка: для главной страницы загрузите <code>/index.html</code>.</p>"
        "</div></body></html>");
    _server.send(404, "text/html; charset=utf-8", html);
}

// ===== FS helpers =====
bool TKWifiManager::fsTryStream(const String& uri) {
    String path = uri;
    if (!path.startsWith("/")) path = "/" + path;
    if (path.endsWith("/")) path += "index.html";
    if (!TKWM_FS.exists(path)) return false;
    File f = TKWM_FS.open(path, "r");
    if (!f) return false;
    _server.streamFile(f, fsContentType(path));
    f.close();
    return true;
}

String TKWifiManager::fsContentType(const String& path) {
    if (path.endsWith(".htm") || path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".mjs"))  return "text/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif"))  return "image/gif";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".ico"))  return "image/x-icon";
    if (path.endsWith(".woff2"))return "font/woff2";
    if (path.endsWith(".wasm")) return "application/wasm";
    if (path.endsWith(".txt"))  return "text/plain";
    return "application/octet-stream";
}

void TKWifiManager::fsEnsureDirs(const String& path) {
    int i = 1;
    while ((i = path.indexOf('/', i)) > 0) {
        TKWM_FS.mkdir(path.substring(0, i));
        i++;
    }
}

bool TKWifiManager::looksText(File& f) {
    const size_t N = 256;
    uint8_t buf[N];
    size_t n = f.read(buf, N);
    f.seek(0);
    for (size_t i = 0; i < n; i++) if (buf[i] == 0) return false;
    return true;
}
