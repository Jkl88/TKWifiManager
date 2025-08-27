#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <Update.h>
#include <FS.h>

#ifndef TKWM_USE_LITTLEFS
#include <SPIFFS.h>
#define TKWM_FS SPIFFS
#else
#include <LittleFS.h>
#define TKWM_FS LittleFS
#endif

// ===== Настройки по умолчанию =====
#ifndef TKWM_MAX_CRED
#define TKWM_MAX_CRED 16
#endif

#ifndef TKWM_DISCOVERY_PORT
#define TKWM_DISCOVERY_PORT 64242
#endif

#ifndef TKWM_DISCOVERY_SIGNATURE
#define TKWM_DISCOVERY_SIGNATURE "TK_DISCOVER:1"
#endif

#ifndef TKWM_WS_PORT
#define TKWM_WS_PORT 81
#endif

// ====== Класс ======
class TKWifiManager {
public:
	using RouteHandler = std::function<void(void)>;
	using WsHandler = std::function<void(uint8_t clientId, WStype_t type, const uint8_t* payload, size_t length)>;

	TKWifiManager(uint16_t httpPort = 80);

	// Инициализация
	// formatFSIfNeeded: форматировать FS, если не монтируется
	// apPrefix: префикс SSID точки (дальше добавится -XXXXXX по MAC)
	bool begin(bool formatFSIfNeeded = true, const char* apPrefix = "TK-Setup");

	// В main loop
	void loop();

	// Добавление своих HTTP-роутов (до/после begin — не важно)
	void addRoute(const String& path, HTTPMethod method, RouteHandler handler);

	// Доступ к серверам/состояниям
	WebServer& webServer() { return _server; }
	WebSocketsServer& wsServer() { return _ws; }

	// Пользовательский обработчик WebSocket (для своей логики)
	void setWsUserHandler(WsHandler h) { _wsUserHandler = std::move(h); }

	// Вкл/выкл UDP discover
	void setDiscoveryEnabled(bool en) { _discoveryEnabled = en; }

	// Принудительно переподключиться к сохранённым (лучшая по RSSI)
	bool reconnectBestKnown();

	// Состояние
	bool      inCaptive() const { return _captive; }
	IPAddress localIP()   const { return WiFi.localIP(); }
	String    apSsid()    const { return _apSsid; }

private:
	// ===== Данные =====
	struct Cred { String ssid, pass; };
	Preferences  _prefs;
	Cred         _creds[TKWM_MAX_CRED];
	int          _credCount = 0;

	uint16_t     _httpPort;
	DNSServer    _dns;
	WebServer    _server;
	WebSocketsServer _ws;      // порт TKWM_WS_PORT
	bool         _captive = false;
	String       _apSsid;
	bool         _fsOk = false;

	WiFiUDP      _udp;
	bool         _discoveryEnabled = true;

	// WebSocket
	WsHandler    _wsUserHandler = nullptr;
	bool         _wsHasClients = false;

	// Скан Wi-Fi (через WS)
	struct ScanNet { String ssid; int32_t rssi; int enc; uint8_t ch; };
	std::vector<ScanNet> _scanCache;
	bool         _scanInProgress = false;
	uint32_t     _scanLastKick = 0;
	uint32_t     _nextAutoScanAt = 0;

	// FS upload
	File         _fsUploadFile;
	String       _fsUploadTo;

	// ===== Приватные методы =====
	void loadCreds();
	void saveCount();
	void saveAt(int idx, const String& ssid, const String& pass);
	int  findBySsid(const String& ssid);

	bool tryConnectBestKnown(uint32_t timeoutMs = 15000);
	void startCaptiveAP(const char* apPrefix);
	static String ipToStr(IPAddress ip);

	void setupRoutes();
	void setupWebSocket();

	// HTTP handlers
	void handleRoot();
	void handleNotFound();
	void handleFsList();          // HTML + JSON
	void handleFsGet();
	void handleFsPut();           // editor (text)
	void handleFsDelete();        // AJAX delete
	void handleFsEdit();          // editor page/save
	void handleUpload();          // multipart /upload
	void handleOtaPage();
	void handleOtaUpload();

	// API (Wi-Fi creds)
	void handleCredsList();
	void handleCredsAdd();
	void handleCredsDel();
	void handleReconnect();
	void handleStartAP();

	// Captive helpers
	void sendUpload404(const String& missingPath);
	bool fsTryStream(const String& uri);
	static String fsContentType(const String& path);
	static void  fsEnsureDirs(const String& path);
	static bool  looksText(File& f);

	// HTML
	String htmlHeader(const String& title);
	String htmlFooter();
	String portalPage();
	String otaPage();

	// UDP discovery
	void udpTick();

	// WS scan helper
	void wsKickScan(bool forceSyncFallback = false);
	void wsPublishScan();
	void wsPublishStatus(uint8_t clientId); // отправить статус только новому клиенту
};
