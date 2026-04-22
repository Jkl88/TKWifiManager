#pragma once
#include <Arduino.h>
#include <functional>
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

/** Двухсимвольный код региона Wi-Fi для esp_wifi (например "EU", "00" — world) */
#ifndef TKWM_WIFI_COUNTRY
#define TKWM_WIFI_COUNTRY "EU"
#endif

/** Версия прошивки для OTA/ESPConnect (в проекте: -DTKWM_FW_VERSION=\\\"1.2.3\\\") */
#ifndef TKWM_FW_VERSION
#define TKWM_FW_VERSION "0.0.0"
#endif

/** Для self-signed и теста: 1 = WiFiClientSecure::setInsecure (не для продакшена) */
#ifndef TKWM_OTA_INSECURE
#define TKWM_OTA_INSECURE 0
#endif

/**
 * Синхрон с полем *controller* в ESPConnect: один идентификатор токеном (без кавычек),
 * напр. -D TKWM_OTA_CONTROLLER=ESP32 или `custom_upload_controller` в platformio.ini (см. README).
 * Если не задано — в запросах OTA/ESPConnect подставляется `ESP.getChipModel()`.
 */

class TKWifiManager {
public:
    
    explicit TKWifiManager(uint16_t httpPort = 80);

    // formatFSIfNeeded=true — смонтировать FS c форматированием при первом фейле
    // apSsidPrefix — префикс SSID точки, суффикс MAC добавится автоматически
    // Возвращает true, если HTTP/WS подняты; смонтирована ли ФС — см. isFilesystemOk()
    bool begin(const String& apSsidPrefix = "TK-Setup", bool formatFSIfNeeded = true);
    void loop();

    bool isFilesystemOk() const { return _fsOk; }

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

    bool     _otaRestartPending = false;
    uint32_t _otaRestartAt     = 0;

    // ota.conf (кэш после loadOtaConf_)
    String  _otaFileHost, _otaFileToken;
    int8_t  _otaFileAuto = -1; // -1: ключа auto в файле не было
    bool    _otaConfLoaded = false;

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
    // ESPConnect (ESPTools server)
    void   handleOtaInfo();
    void   handleOtaConfig();
    void   handleOtaCheck();
    void   handleOtaInstall();
    void   handleOtaSaveSettings();
    void   loadOtaConf_();
    String otaConfigHost_();   // merge file
    String otaConfigToken_();
    bool   otaConfigAuto_();   // file или Preferences

    // Wi-Fi API/страницы
    void handleWifiPage();
    void handleWifiSave();
    void handleReconnect();   // принудительный подбор лучшей сети
    void handleStartAP();     // вернуться в AP
    void handleWifiListSaved();  // GET /api/wifi/saved
    void handleWifiDelete();     // POST /api/wifi/delete (ssid=...)
    void handleWifiScan();       // GET /api/wifi/scan  (REST-версия для polling-клиентов)


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
