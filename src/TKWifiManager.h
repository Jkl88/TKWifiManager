#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
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

#ifndef TKWM_MAX_CRED
#define TKWM_MAX_CRED 16
#endif

#ifndef TKWM_DISCOVERY_PORT
#define TKWM_DISCOVERY_PORT 64242
#endif

#ifndef TKWM_DISCOVERY_SIGNATURE
#define TKWM_DISCOVERY_SIGNATURE "TK_DISCOVER:1"
#endif

class TKWifiManager {
public:
  using RouteHandler = std::function<void(void)>;

  TKWifiManager(uint16_t httpPort = 80);

  bool begin(bool formatFSIfNeeded = true);
  void loop();

  void addRoute(const String& path, HTTPMethod method, RouteHandler handler);
  WebServer& webServer() { return _server; }

  void setDiscoveryEnabled(bool en) { _discoveryEnabled = en; }
  bool reconnectBestKnown();

  bool inCaptive() const { return _captive; }
  IPAddress localIP() const { return WiFi.localIP(); }

private:
  struct Cred { String ssid, pass; };
  Preferences _prefs;
  Cred _creds[TKWM_MAX_CRED];
  int  _credCount = 0;

  uint16_t  _httpPort;
  DNSServer _dns;
  WebServer _server;
  bool      _captive = false;
  String    _apSsid;
  bool      _fsOk = false;

  WiFiUDP _udp;
  bool    _discoveryEnabled = true;

  // Scan cache (async)
  struct ScanNet { String ssid; int32_t rssi; int enc; };
  std::vector<ScanNet> _scanCache;
  bool _scanInProgress = false;
  uint32_t _scanLastKick = 0;

  void loadCreds();
  void saveCount();
  void saveAt(int idx, const String& ssid, const String& pass);
  int  findBySsid(const String& ssid);

  bool tryConnectBestKnown(uint32_t timeoutMs = 15000);
  void startCaptiveAP();
  static String ipToStr(IPAddress ip);

  void setupRoutes();
  void handleRoot();
  void handleNotFound();
  void handleScan();
  void handleCredsList();
  void handleCredsAdd();
  void handleCredsDel();
  void handleReconnect();
  void handleStartAP();
  void handleOtaPage();
  void handleOtaUpload();
  void handleFsList();
  void handleFsGet();
  void handleFsPut();
  void handleFsDelete();
  void handleFsEdit();
  void serveStaticOrIndex();

  String htmlHeader(const String& title);
  String htmlFooter();
  String portalPage();
  String otaPage();
  String uploadPage404(const String& missingPath);

  void udpTick();
};
