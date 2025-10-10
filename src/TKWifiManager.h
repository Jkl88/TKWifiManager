#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <Update.h>
#include <WiFiUdp.h>
#include <FS.h>

#ifndef TKWM_USE_LITTLEFS
#define TKWM_USE_LITTLEFS 1
#endif

#if TKWM_USE_LITTLEFS
#include <LittleFS.h>
#define TKWM_FS LittleFS
#else
#include <SPIFFS.h>
#define TKWM_FS SPIFFS
#endif

// ===== Настройки =====
#ifndef TKWM_WS_PORT
#define TKWM_WS_PORT 81
#endif

#ifndef TKWM_DISCOVERY_PORT
#define TKWM_DISCOVERY_PORT 64242
#endif

#ifndef TKWM_DISCOVERY_SIGNATURE
#define TKWM_DISCOVERY_SIGNATURE "TK_DISCOVER:1"
#endif

#ifndef TKWM_MAX_CRED
#define TKWM_MAX_CRED 16
#endif

class TKWifiManager {
public:
    
    explicit TKWifiManager(uint16_t httpPort = 80);

    // formatFSIfNeeded=true — смонтировать FS c форматированием при первом фейле
    // apSsidPrefix — префикс SSID точки, суффикс MAC добавится автоматически
    bool begin(const String& apSsidPrefix = "TK-Setup", bool formatFSIfNeeded = true);
    void loop();

    // доступ к веб-объектам/состоянию
    WebServer& web() { return _server; }
    WebSocketsServer& ws() { return _ws; }
    bool inCaptive()  const { return _captiveMode; }
    IPAddress ip()    const { return _captiveMode ? WiFi.softAPIP() : WiFi.localIP(); }

    // возможность добавить свои роуты
    using Route = std::function<void(void)>;
    void addRoute(const String& path, HTTPMethod method, Route handler) {
        _server.on(path.c_str(), method, handler);
    }

    // хук для пользовательских WS сообщений (не «scan/status»)
    using WsHook = std::function<void(uint8_t, WStype_t, const uint8_t*, size_t)>;
    void setUserWsHook(WsHook h) { _userWsHook = std::move(h); }

private:
    // ===== хранилище сетей =====
    struct Cred { String ssid, pass; };
    Preferences _prefs;
    Cred        _creds[TKWM_MAX_CRED];
    int         _credN = 0;

    // ===== веб =====
    uint16_t        _httpPort;
    WebServer       _server;
    WebSocketsServer _ws;
    DNSServer       _dns;
    bool            _captiveMode = false;
    String          _apSsid;      // уникальный SSID (prefix-XXXXXX)
    bool            _fsOk = false;
    String _apSsidPrefix;
    // upload (состояние multipart)
    File   _uploadFile;
    String _uploadToPath; // полный итоговый путь файла для ответа

    // UDP discovery
    WiFiUDP _udp;

    // пользовательский WS-хук
    WsHook _userWsHook = nullptr;

    // ===== внутреннее =====
    void  loadCreds();
    void  saveCount();
    void  saveAt(int idx, const String& ssid, const String& pass);
    int   findBySsid(const String& ssid) const;

    bool  tryConnectBestKnown(uint32_t timeoutMs = 12000);
    void  startAPCaptive();

    // ==== роутинг/обработчики ====
    void setupRoutes();
    void setupWebSocket();

    void handleRoot();
    void handleNotFound();

    // Captive detectors
    void handleCaptiveProbe();

    // FS API + страницы
    void handleFsList();
    void handleFsGet();
    void handleFsPut();
    void handleFsDelete();
    void handleFsMkdir();
    void handleUpload();     // multipart body handler
    void handleUploadDone(); // финальный ответ

    // OTA
    void handleOtaPage();
    void handleOtaUpload();
    void handleOtaFinish();

    // Wi-Fi API/страницы
    void handleWifiPage();
    void handleWifiSave();
    void handleReconnect();   // принудительный подбор лучшей сети
    void handleStartAP();     // вернуться в AP
    void handleWifiListSaved();  // GET /api/wifi/saved
    void handleWifiDelete();     // POST /api/wifi/delete (ssid=...)


    // WS служебное
    void wsSendStatus(uint8_t clientId);
    void wsRunScanAndPublish(); // sync scan (AP не выключаем)

    // UDP discovery
    void udpTick();

    // FS helpers
    static String contentType(const String& path);
    static void   ensureDirs(const String& path);
    static bool   looksText(File& f);
    bool streamIfExists(const String& uri);
    void sendUpload404(const String& missingPath);

    // Встроенные страницы (если в FS нет файлов)
    static const char* builtinIndex();
    static const char* builtinWifi(); // WS-сканер
    static const char* builtinFs();
    static const char* builtinOta();
};
