#include "TKWifiManager.h"

TKWifiManager::TKWifiManager(uint16_t httpPort)
: _httpPort(httpPort), _server(httpPort) {}

bool TKWifiManager::begin(bool formatFSIfNeeded) {
  // === FS ===
  _fsOk = TKWM_FS.begin(true);
  if (!_fsOk) {
    Serial.println(F("[FS] mount failed; formatting..."));
    TKWM_FS.format();
    _fsOk = TKWM_FS.begin(true);
  }
  if (_fsOk) {
    size_t total = TKWM_FS.totalBytes();
    size_t used  = TKWM_FS.usedBytes();
    Serial.printf("[FS] mounted: used %u / %u bytes\n", (unsigned)used, (unsigned)total);
  } else {
    Serial.println(F("[FS] not mounted — FS UI limited"));
  }

  // Load Wi-Fi creds
  loadCreds();

  // Try STA
  bool connected = tryConnectBestKnown();
  if (!connected) startCaptiveAP();

  // Routes
  setupRoutes();
  _server.begin();

  // UDP discovery
  if (_discoveryEnabled) _udp.begin(TKWM_DISCOVERY_PORT);

  return true;
}

void TKWifiManager::loop() {
  if (_captive) _dns.processNextRequest();
  _server.handleClient();
  udpTick();

  static uint32_t t = 0;
  if (!_captive && millis() - t > 5000) {
    t = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (!tryConnectBestKnown()) startCaptiveAP();
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
  if (!ok) startCaptiveAP();
  return ok;
}

// ===== Creds =====
void TKWifiManager::loadCreds() {
  _prefs.begin("tkwifi", true);
  _credCount = _prefs.getInt("count", 0);
  if (_credCount < 0 || _credCount > TKWM_MAX_CRED) _credCount = 0;
  for (int i=0;i<_credCount;i++){
    _creds[i].ssid = _prefs.getString(("s"+String(i)).c_str(), "");
    _creds[i].pass = _prefs.getString(("p"+String(i)).c_str(), "");
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
  _prefs.putString(("s"+String(idx)).c_str(), ssid);
  _prefs.putString(("p"+String(idx)).c_str(), pass);
  _prefs.end();
}

int TKWifiManager::findBySsid(const String& ssid) {
  for (int i=0;i<_credCount;i++) if (_creds[i].ssid == ssid) return i;
  return -1;
}

// ===== Wi-Fi =====
bool TKWifiManager::tryConnectBestKnown(uint32_t timeoutMs) {
  if (_credCount == 0) return false;

  int n = WiFi.scanNetworks(false, true);
  int bestRssi = -9999, bestIdx = -1;
  String bestSsid, bestPass;
  for (int i=0;i<n;i++){
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

void TKWifiManager::startCaptiveAP() {
  _captive = true;
  uint64_t mac = ESP.getEfuseMac();
  char macs[7];
  snprintf(macs, sizeof(macs), "%06X", (uint32_t)(mac & 0xFFFFFF));
  _apSsid = String("TK-Setup-") + macs;

  WiFi.mode(WIFI_AP_STA);
  IPAddress apIP(192,168,4,1), net(255,255,255,0);
  WiFi.softAPConfig(apIP, apIP, net);
  WiFi.softAP(_apSsid.c_str(), nullptr, 6, false, 4);
  _dns.stop();
  _dns.start(53, "*", apIP);
}

String TKWifiManager::ipToStr(IPAddress ip) {
  return String(ip[0])+"."+String(ip[1])+"."+String(ip[2])+"."+String(ip[3]);
}

// ===== UDP =====
void TKWifiManager::udpTick() {
  if (!_discoveryEnabled) return;
  int sz = _udp.parsePacket();
  if (sz <= 0) return;
  char buf[64] = {0};
  int n = _udp.read(buf, sizeof(buf)-1);
  buf[n] = 0;
  if (!String(buf).startsWith(TKWM_DISCOVERY_SIGNATURE)) return;

  String payload = String("{\"id\":\"") + _apSsid + "\",\"name\":\""+ _apSsid +
                   "\",\"ip\":\"" + (_captive ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) +
                   "\",\"model\":\"ESP32\",\"fw\":\"" + String(ESP.getSdkVersion()) +
                   "\",\"web\":\"/\"}";
  _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
  _udp.print(payload);
  _udp.endPacket();
}

// ===== Routes =====
void TKWifiManager::setupRoutes() {
  _server.on("/", HTTP_GET, [this]{ handleRoot(); });
  _server.on("/api/scan", HTTP_GET, [this]{ handleScan(); });
  _server.on("/api/creds", HTTP_GET, [this]{ handleCredsList(); });
  _server.on("/api/creds/add", HTTP_POST, [this]{ handleCredsAdd(); });
  _server.on("/api/creds/del", HTTP_POST, [this]{ handleCredsDel(); });
  _server.on("/api/reconnect", HTTP_POST, [this]{ handleReconnect(); });
  _server.on("/api/start_ap", HTTP_POST, [this]{ handleStartAP(); });

  // OTA
  _server.on("/ota", HTTP_GET, [this]{ handleOtaPage(); });
  _server.on("/ota", HTTP_POST, [this]{ handleOtaUpload(); }, []{});

  // FS
  _server.on("/fs", HTTP_GET, [this]{ handleFsList(); });
  _server.on("/fs/get", HTTP_GET, [this]{ handleFsGet(); });
  _server.on("/fs/put", HTTP_POST, [this]{ handleFsPut(); });
  _server.on("/fs/del", HTTP_POST, [this]{ handleFsDelete(); });
  _server.on("/fs/edit", HTTP_GET, [this]{ handleFsEdit(); });
  _server.on("/fs/edit", HTTP_POST, [this]{ handleFsEdit(); });

  // Captive probes
  _server.on("/generate_204", HTTP_GET, [this]{ _server.send(200, "text/plain", "OK"); });
  _server.on("/gen_204", HTTP_GET, [this]{ _server.send(200, "text/plain", "OK"); });
  _server.on("/hotspot-detect.html", HTTP_GET, [this]{
    _server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='0; url=/'/></head><body>OK</body></html>");
  });
  _server.on("/library/test/success.html", HTTP_GET, [this]{ _server.send(200, "text/plain", "OK"); });
  _server.on("/ncsi.txt", HTTP_GET, [this]{ _server.send(200, "text/plain", "OK"); });
  _server.on("/connecttest.txt", HTTP_GET, [this]{ _server.send(200, "text/plain", "OK"); });

  _server.onNotFound([this]{ handleNotFound(); });
}

void TKWifiManager::handleRoot() {
  _server.send(200, "text/html; charset=utf-8", portalPage());
}

void TKWifiManager::handleScan() {
  uint32_t now = millis();
  if (!_scanInProgress && now - _scanLastKick > 1500) {
    int rc = WiFi.scanNetworks(true, false);
    if (rc == WIFI_SCAN_RUNNING) {
      _scanInProgress = true;
      _scanLastKick = now;
    }
  }
  if (_scanInProgress) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      _scanCache.clear();
      _scanCache.reserve(n);
      for (int i=0;i<n;i++){
        _scanCache.push_back(ScanNet{ String(WiFi.SSID(i)), WiFi.RSSI(i), (int)WiFi.encryptionType(i) });
      }
      WiFi.scanDelete();
      _scanInProgress = false;
    }
  }
  String j = "[";
  for (size_t i=0;i<_scanCache.size();++i){
    if (i) j += ",";
    auto &o = _scanCache[i];
    j += "{\"ssid\":\""+o.ssid+"\",\"rssi\":"+String(o.rssi)+",\"enc\":"+String(o.enc)+"}";
  }
  j += "]";
  _server.send(200, "application/json", j);
}

void TKWifiManager::handleCredsList() {
  String j = "[";
  for (int i=0;i<_credCount;i++){ if (i) j += ","; j += "{\"ssid\":\""+_creds[i].ssid+"\"}"; }
  j += "]";
  _server.send(200, "application/json", j);
}

void TKWifiManager::handleCredsAdd() {
  if (!_server.hasArg("ssid")) { _server.send(400, "text/plain", "No ssid"); return; }
  String ssid = _server.arg("ssid");
  String pass = _server.hasArg("pass") ? _server.arg("pass") : "";
  int idx = findBySsid(ssid);
  if (idx >= 0) { _creds[idx].pass = pass; saveAt(idx, ssid, pass); }
  else {
    if (_credCount >= TKWM_MAX_CRED) { _server.send(507, "text/plain", "full"); return; }
    _creds[_credCount] = {ssid, pass}; saveAt(_credCount, ssid, pass); _credCount++; saveCount();
  }
  _server.send(200, "text/plain", "ok");
}

void TKWifiManager::handleCredsDel() {
  if (!_server.hasArg("idx")) { _server.send(400, "text/plain", "No idx"); return; }
  int idx = _server.arg("idx").toInt();
  if (idx < 0 || idx >= _credCount) { _server.send(400, "text/plain", "bad idx"); return; }
  for (int i=idx;i<_credCount-1;i++){ _creds[i] = _creds[i+1]; saveAt(i, _creds[i].ssid, _creds[i].pass); }
  _prefs.begin("tkwifi", false);
  _prefs.remove(("s"+String(_credCount-1)).c_str());
  _prefs.remove(("p"+String(_credCount-1)).c_str());
  _prefs.end();
  _credCount--; saveCount();
  _server.send(200, "text/plain", "ok");
}

void TKWifiManager::handleReconnect() {
  reconnectBestKnown();
  _server.send(200, "text/plain", "reconnect");
}

void TKWifiManager::handleStartAP() {
  startCaptiveAP();
  _server.send(200, "text/plain", "ap started");
}

// OTA
void TKWifiManager::handleOtaPage() {
  _server.send(200, "text/html; charset=utf-8", otaPage());
}

void TKWifiManager::handleOtaUpload() {
  if (_server.upload().status == UPLOAD_FILE_START) {
    size_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) { _server.send(500, "text/plain", "Update begin failed"); return; }
  } else if (_server.upload().status == UPLOAD_FILE_WRITE) {
    if (Update.write(_server.upload().buf, _server.upload().currentSize) != _server.upload().currentSize) {
      _server.send(500, "text/plain", "Update write failed"); return;
    }
  } else if (_server.upload().status == UPLOAD_FILE_END) {
    if (!Update.end(true)) { _server.send(500, "text/plain", "Update end failed"); return; }
    _server.send(200, "text/plain", "OK, rebooting");
    delay(200);
    ESP.restart();
    return;
  }
  _server.send(400, "text/plain", "No file");
}

// FS
void TKWifiManager::handleFsList() {
  if (!_fsOk) { _server.send(500, "text/plain", "FS not mounted"); return; }
  if (_server.hasArg("html")) {
    String s = htmlHeader("FS") +
      "<h1>Файловая система</h1>"
      "<form method='POST' action='/fs/put' enctype='multipart/form-data'>"
      "<input type='file' name='file'><button>Загрузить</button></form>"
      "<table><tr><th>Имя</th><th>Размер</th><th>Действия</th></tr>";
    File r = TKWM_FS.open("/");
    File f;
    while ( (f = r.openNextFile()) ) {
      String p = String(f.path());
      s += "<tr><td><a href='/fs/get?path="+p+"'>"+p+"</a></td><td>"+String(f.size())+"</td>"
           "<td>"
           "<form style='display:inline' method='GET' action='/fs/edit'><input type='hidden' name='path' value='"+p+"'><button>Редактировать</button></form> "
           "<form style='display:inline' method='POST' action='/fs/del' onsubmit='return confirm(\"Удалить "+p+"?\")'><input type='hidden' name='path' value='"+p+"'><button>Удалить</button></form>"
           "</td></tr>";
      f.close();
    }
    s += "</table>" + htmlFooter();
    _server.send(200, "text/html; charset=utf-8", s);
  } else {
    String j = "[";
    File r = TKWM_FS.open("/");
    File f;
    bool first = true;
    while ( (f = r.openNextFile()) ) {
      if (!first) j += ",";
      first = false;
      j += "{\"path\":\""+String(f.path())+"\",\"size\":"+String(f.size())+"}";
      f.close();
    }
    j += "]";
    _server.send(200, "application/json", j);
  }
}

void TKWifiManager::handleFsGet() {
  if (!_server.hasArg("path")) { _server.send(400, "text/plain", "No path"); return; }
  String p = _server.arg("path");
  if (!TKWM_FS.exists(p)) { _server.send(404, "text/plain", "Not found"); return; }
  File f = TKWM_FS.open(p, "r");
  String mime = "application/octet-stream";
  if (p.endsWith(".html")) mime="text/html";
  else if (p.endsWith(".css")) mime="text/css";
  else if (p.endsWith(".js")) mime="application/javascript";
  else if (p.endsWith(".json")) mime="application/json";
  else if (p.endsWith(".png")) mime="image/png";
  else if (p.endsWith(".jpg")||p.endsWith(".jpeg")) mime="image/jpeg";
  _server.streamFile(f, mime);
  f.close();
}

void TKWifiManager::handleFsPut() {
  HTTPUpload& up = _server.upload();
  static File f;
  if (up.status == UPLOAD_FILE_START) {
    String filename = up.filename;
    if (!filename.startsWith("/")) filename = "/"+filename;
    f = TKWM_FS.open(filename, "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (f) f.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (f) f.close();
    _server.sendHeader("Location", "/fs?html=1", true);
    _server.send(302);
  }
}

void TKWifiManager::handleFsDelete() {
  if (!_server.hasArg("path")) { _server.send(400, "text/plain", "No path"); return; }
  String p = _server.arg("path");
  bool ok = TKWM_FS.remove(p);
  _server.send(200, "text/plain", ok ? "ok" : "fail");
}

void TKWifiManager::handleFsEdit() {
  if (_server.method() == HTTP_GET) {
    if (!_server.hasArg("path")) { _server.send(400, "text/plain", "No path"); return; }
    String p = _server.arg("path");
    String txt;
    if (TKWM_FS.exists(p)) {
      File f = TKWM_FS.open(p, "r");
      while (f.available()) txt += (char)f.read();
      f.close();
    }
    String s = htmlHeader("Редактор") +
      "<h1>Редактор</h1>"
      "<form method='POST' action='/fs/edit'>"
      "<input type='hidden' name='path' value='"+p+"'>"
      "<textarea name='text' style='width:100%;height:60vh;font-family:monospace'>"+txt+"</textarea>"
      "<div style='margin-top:8px'><button>Сохранить</button></div></form>" + htmlFooter();
    _server.send(200, "text/html; charset=utf-8", s);
  } else {
    String p = _server.arg("path");
    String txt = _server.arg("text");
    File f = TKWM_FS.open(p, "w");
    f.print(txt);
    f.close();
    _server.sendHeader("Location", "/fs?html=1", true);
    _server.send(302);
  }
}

void TKWifiManager::handleNotFound() {
  String uri = _server.uri();

  if (_fsOk && TKWM_FS.exists(uri)) {
    File f = TKWM_FS.open(uri, "r");
    String mime = "text/plain";
    if (uri.endsWith(".html")) mime="text/html";
    else if (uri.endsWith(".css")) mime="text/css";
    else if (uri.endsWith(".js")) mime="application/javascript";
    else if (uri.endsWith(".json")) mime="application/json";
    else if (uri.endsWith(".png")) mime="image/png";
    else if (uri.endsWith(".jpg")||uri.endsWith(".jpeg")) mime="image/jpeg";
    _server.streamFile(f, mime);
    f.close();
    return;
  }

  if (_captive) {
    _server.sendHeader("Location", "/", true);
    _server.send(302, "text/plain", "");
    return;
  }

  _server.send(404, "text/html; charset=utf-8", uploadPage404(uri));
}

// ===== HTML =====
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
"</style></head><body><div class='wrap'>");
}

String TKWifiManager::htmlFooter() { return "</div></body></html>"; }

String TKWifiManager::portalPage() {
  String ip = _captive ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  String mode = _captive ? "AP (каптив)" : "STA";
  String s = htmlHeader("TK Wi-Fi Manager");
  s += "<div class='card'><h1>TK Wi-Fi Manager</h1>";
  s += "<p>Режим: "+mode+" • IP: <b>"+ip+"</b></p>";

  s += "<div class='row'><button onclick='scan()'>Сканировать</button>"
        "<form method='POST' action='/api/reconnect' style='display:inline'><button>Переподключить</button></form>"
        "<form method='POST' action='/api/start_ap' style='display:inline'><button>Старт AP</button></form>"
        "<a href='/fs?html=1'><button type='button'>Файлы</button></a>"
        "<a href='/ota'><button type='button'>OTA</button></a></div>";

  s += "<h3>Добавить сеть</h3>"
       "<form class='row' onsubmit='return add(event)'><input id='ssid' placeholder='SSID' required>"
       "<input id='pass' placeholder='Пароль' type='password'><button>Сохранить</button></form>";

  s += "<h3>Сохранённые</h3><div id='known' class='mut'>загрузка...</div>";

  s += "<h3>Доступные сети</h3><div id='scan' class='mut'>нажмите «Сканировать»</div>";

  s += "</div><script>"
  "async function refreshKnown(){let r=await fetch('/api/creds');let a=await r.json();"
  "if(!a.length){known.innerHTML='<span class=mut>пусто</span>';return}"
  "let h='<table><tr><th>SSID</th><th></th></tr>';a.forEach((o,i)=>{h+=`<tr><td>${o.ssid}</td><td>"
  "<form method=\"POST\" action=\"/api/creds/del\" onsubmit=\"return confirm('Удалить?')\">"
  "<input type=hidden name=idx value='${i}'><button>Удалить</button></form></td></tr>`});"
  "h+='</table>';known.innerHTML=h}"
  "async function add(e){e.preventDefault();let s=document.getElementById('ssid').value;let p=document.getElementById('pass').value;"
  "let f=new FormData();f.append('ssid',s);f.append('pass',p);await fetch('/api/creds/add',{method:'POST',body:f});"
  "refreshKnown();document.getElementById('pass').value='';}"
  "async function scan(){scan.innerHTML='скан...';let r=await fetch('/api/scan');let a=await r.json();"
  "if(!a.length){scan.innerHTML='<span class=mut>ничего не найдено</span>';return}"
  "a.sort((x,y)=>y.rssi-x.rssi);let h='';a.forEach(o=>{h+=`<span class=pill>${o.ssid} (${o.rssi} dBm)</span>`});scan.innerHTML=h}"
  "refreshKnown();setInterval(()=>{scan();},2000);"
  "</script>";
  s += htmlFooter();
  return s;
}

String TKWifiManager::otaPage() {
  String s = htmlHeader("OTA") +
  "<div class='card'><h1>OTA обновление</h1>"
  "<form method='POST' action='/ota' enctype='multipart/form-data'>"
  "<input type='file' name='update' required><button>Прошить</button></form>"
  "<p class='mut'>Загрузите .bin прошивки. Устройство перезагрузится автоматически.</p>"
  "</div>"+ htmlFooter();
  return s;
}

String TKWifiManager::uploadPage404(const String& missingPath) {
  String s = htmlHeader("Загрузить файл");
  s += "<div class='card'><h1>Файл не найден: "+missingPath+"</h1>"
       "<p class='mut'>Можете сразу загрузить этот файл в FS, и затем запрашивать его по тому же пути.</p>"
       "<form method='POST' action='/fs/put' enctype='multipart/form-data'>"
       "<input type='file' name='file' required><button>Загрузить</button></form>"
       "<p><a href='/fs?html=1'><button type='button'>Открыть файловый менеджер</button></a></p>"
       "</div>" + htmlFooter();
  return s;
}
