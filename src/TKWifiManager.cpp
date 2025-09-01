#include "TKWifiManager.h"
#include "esp_wifi.h"

// ===================== ВСТРОЕННЫЕ СТРАНИЦЫ =====================
static const char WIFI_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wi-Fi настройка</title>
<style>
:root{--bg:#0b1220;--card:#0d1728;--ink:#e8eef7;--mut:#9fb3d1;--br:#1b2a44;--btn:#143057}
body{margin:0;background:var(--bg);color:var(--ink);font:15px system-ui,-apple-system,Segoe UI,Roboto}
.wrap{max-width:860px;margin:auto;padding:20px}
.card{background:var(--card);border:1px solid var(--br);border-radius:14px;padding:16px}
h1{font-size:18px;margin:0 0 12px}
h2{font-size:16px;margin:14px 0 8px}
.row{display:flex;gap:12px;flex-wrap:wrap;align-items:center}
input,button{padding:10px 12px;border-radius:10px;border:1px solid var(--br);background:#0f1a2c;color:var(--ink)}
button{background:var(--btn);cursor:pointer}
.list{margin-top:12px}
.net{display:flex;justify-content:space-between;align-items:center;padding:10px;border:1px solid var(--br);border-radius:10px;margin:8px 0;background:#0f1a2c;cursor:pointer}
.net small{color:var(--mut)}
.badge{color:var(--mut);font-size:12px}
.ok{color:#95ffa1}.err{color:#ff9a9a}.mut{color:#9fb3d1}
</style></head><body><div class="wrap"><div class="card">
<h1>Настройка Wi-Fi</h1>

<div class="row">
  <button id="scan">🔄 Обновить список</button>
  <button id="ap">📶 Перейти в AP-режим</button>
  <span id="st" class="mut">WS…</span>
</div>

<h2>Доступные сети</h2>
<div id="list" class="list"></div>

<hr style="border:0;border-top:1px solid var(--br);margin:14px 0">

<h2>Подключиться вручную</h2>
<form id="f" class="row">
  <input id="ssid" placeholder="SSID" required style="flex:1;min-width:180px">
  <input id="pass" placeholder="Пароль" type="password" style="flex:1;min-width:180px">
  <button type="submit">💾 Сохранить</button>
</form>
<div id="msg" class="mut" style="margin-top:8px"></div>

<hr style="border:0;border-top:1px solid var(--br);margin:14px 0">

<h2>Сохранённые сети</h2>
<div id="saved" class="list"></div>

<div class="row" style="margin-top:10px">
  <a class="mut" href="/">Главная</a>
  <a class="mut" href="/fs">Файлы</a>
  <a class="mut" href="/ota">OTA</a>
</div>
</div></div>

<script>
const $=s=>document.querySelector(s);
const list=$("#list"), st=$("#st"), ssid=$("#ssid"), pass=$("#pass"), msg=$("#msg"),
      scanB=$("#scan"), saved=$("#saved"), apBtn=$("#ap");
let ws;

function connectWS(){
  ws = new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen = ()=>{ st.textContent='WS ok'; ws.send('status'); ws.send('scan'); loadSaved(); };
  ws.onclose = ()=>{ st.textContent='WS close'; setTimeout(connectWS,800); };
  ws.onmessage = e=>{
    let j; try{ j=JSON.parse(e.data);}catch(_){return;}
    if (j.type==='status'){
      st.innerHTML = (j.mode==='AP'?'AP (каптив)':'STA') + ' • IP: <b>'+ (j.ip||'-') + '</b>';
    } else if (j.type==='scan'){
      renderScan(j.nets||[]);
    }
  };
}
function esc(s){return String(s).replace(/[&<>"'`=\/]/g,m=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;","`":"&#x60;","=":"&#x3D;","/":"&#x2F;"}[m]))}

function renderScan(nets){
  nets.sort((a,b)=>b.rssi-a.rssi);
  list.innerHTML='';
  if(!nets.length){ list.innerHTML='<i class="mut">ничего не найдено</i>'; return; }
  nets.forEach(n=>{
    const el=document.createElement('div'); el.className='net';
    const enc=(n.enc===0)?'open':'🔒';
    el.innerHTML=`<div><b>${esc(n.ssid||'(скрытая)')}</b><br><small>${enc}, ch ${n.ch}</small></div><small>${n.rssi} dBm</small>`;
    el.onclick=()=>{ if(n.ssid){ ssid.value=n.ssid; pass.focus(); } else ssid.focus(); };
    list.appendChild(el);
  });
}

async function loadSaved(){
  try{
    const r = await fetch('/api/wifi/saved');
    const j = await r.json();
    saved.innerHTML = '';
    (j.nets||[]).forEach(s=>{
      const row=document.createElement('div'); row.className='net'; row.style.cursor='default';
      row.innerHTML = `<div><b>${esc(s)}</b><br><small class="mut">сохранено</small></div>
                       <button data-del style="cursor:pointer">🗑️</button>`;
      row.querySelector('[data-del]').onclick = async ()=>{
        if(!confirm('Удалить «'+s+'» ?')) return;
        const r = await fetch('/api/wifi/delete', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'ssid='+encodeURIComponent(s) });
        const jj = await r.json();
        if (jj.ok) loadSaved();
      };
      saved.appendChild(row);
    });
    if(!(j.nets||[]).length){
      saved.innerHTML = '<i class="mut">нет сохранённых сетей</i>';
    }
  } catch(_){
    saved.innerHTML = '<span class="err">ошибка загрузки списка</span>';
  }
}

scanB.onclick = ()=>{ if(ws && ws.readyState===1) ws.send('scan'); };
apBtn.onclick  = async ()=>{
  apBtn.disabled = true;
  try{
    const r = await fetch('/api/start_ap', {method:'POST'});
    const j = await r.json();
    if (j.ok && ws && ws.readyState===1) ws.send('status');
  }catch(_){}
  apBtn.disabled = false;
};

document.getElementById('f').addEventListener('submit', async ev=>{
  ev.preventDefault(); msg.textContent='Сохраняю и подключаюсь...';
  const body = JSON.stringify({ssid:ssid.value.trim(), password:pass.value});
  try{
    const r = await fetch('/api/wifi/save', {method:'POST', headers:{'Content-Type':'application/json'}, body});
    const j = await r.json();
    if(!j.ok){ msg.innerHTML='<span class="err">'+(j.msg||'Ошибка')+'</span>'; return; }
    if(j.connected){ msg.innerHTML='<span class="ok">Подключено! IP: <b>'+ (j.ip||'-') +'</b></span>'; }
    else{ msg.innerHTML='<span class="err">Не удалось подключиться. Проверьте пароль.</span>'; }
    loadSaved();
    if(ws && ws.readyState===1){ ws.send('status'); ws.send('scan'); }
  }catch(_){ msg.innerHTML='<span class="err">Ошибка запроса</span>'; }
});

connectWS();
</script></body></html>)HTML";

static const char FS_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Файлы (LittleFS)</title>
<style>
:root{--bg:#0b1220;--card:#0d1728;--ink:#e8eef7;--mut:#9fb3d1;--br:#1b2a44;--btn:#143057}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px system-ui,-apple-system,Segoe UI,Roboto}
.wrap{display:grid;grid-template-columns:340px 1fr;min-height:100vh}
.side{border-right:1px solid var(--br);padding:14px}.main{padding:14px}
h1{font-size:18px;margin:0 0 10px}
input,button{padding:8px 10px;border-radius:10px;border:1px solid var(--br);background:#0f1a2c;color:var(--ink)}
button{background:var(--btn);cursor:pointer}
.list{margin-top:10px;display:flex;flex-direction:column;gap:6px}
.item{display:flex;gap:8px;align-items:center;justify-content:space-between;border:1px solid var(--br);border-radius:10px;padding:8px;background:#0f1a2c}
.item .path{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:190px}
.badge{color:var(--mut);font-size:12px}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.tools{display:flex;gap:8px;align-items:center;margin:8px 0}
#editor{position:relative;height:64vh;width:100%;border:1px solid var(--br);border-radius:12px}
.mut{color:var(--mut)}hr{border:0;border-top:1px solid var(--br);margin:12px 0}
.drop{border:2px dashed var(--br);border-radius:12px;padding:16px;text-align:center;margin-top:8px}
.drop.drag{background:#0f1a2c}
@media (max-width:900px){.wrap{grid-template-columns:1fr}.side{border-right:0;border-bottom:1px solid var(--br)}}
</style></head><body>
<div class="wrap">
  <div class="side">
    <h1>Файлы</h1>
    <div class="tools">
      <button id="refresh">🔄 Обновить</button>
      <a class="mut" href="/wifi">Wi-Fi</a>
      <a class="mut" href="/ota">OTA</a>
      <a class="mut" href="/">Главная</a>
    </div>
    <div class="row">
      <input id="newPath" placeholder="/новый_файл.txt" style="flex:1;min-width:180px">
      <button id="create">➕ Создать</button>
    </div>
    <div class="drop" id="drop">Перетащите файлы сюда или открой<input id="up" type="file" multiple></div>
    <div class="list" id="list"></div>
  </div>
  <div class="main">
    <div class="row">
      <div>Открыт: <b id="curPath">—</b></div>
      <div class="badge" id="curInfo"></div>
      <span style="flex:1"></span>
      <button id="save" disabled>💾 Сохранить</button>
      <a id="download" class="mut" href="#" download>⬇️ Скачать</a>
    </div>
    <div id="editor"></div>
    <div class="mut" style="margin-top:6px">Подсветка Ace (CDN). Для бинарных/больших файлов редактирование отключено.</div>
  </div>
</div>
<script src="https://cdn.jsdelivr.net/npm/ace-builds@1.32.9/src-min/ace.js"></script>
<script>
const $=s=>document.querySelector(s); const listEl=$("#list"), drop=$("#drop"), up=$("#up"),
      refreshBtn=$("#refresh"), createBtn=$("#create"), newPath=$("#newPath"),
      curPathEl=$("#curPath"), curInfoEl=$("#curInfo"), saveBtn=$("#save"), downloadA=$("#download");
let editor=null, currentPath="", currentBinary=false;
function aceReady(){return window.ace&&ace.edit}
function initEditor(){ if(!aceReady())return; editor=ace.edit("editor"); editor.session.setUseWorker(false); editor.setOption("wrap",true); editor.setTheme("ace/theme/one_dark"); editor.session.setMode("ace/mode/text"); editor.on('change',()=>{ if(currentPath && !currentBinary) saveBtn.disabled=false; }); }
function modeByExt(p){ p=(p||"").toLowerCase();
 if(p.endsWith(".html")||p.endsWith(".htm"))return"ace/mode/html";
 if(p.endsWith(".css"))return"ace/mode/css";
 if(p.endsWith(".js")||p.endsWith(".mjs"))return"ace/mode/javascript";
 if(p.endsWith(".json"))return"ace/mode/json";
 if(p.endsWith(".md"))return"ace/mode/markdown";
 if(p.endsWith(".ini")||p.endsWith(".conf"))return"ace/mode/ini";
 if(p.endsWith(".svg")||p.endsWith(".xml"))return"ace/mode/xml";
 if(p.endsWith(".yaml")||p.endsWith(".yml"))return"ace/mode/yaml";
 if(p.endsWith(".c")||p.endsWith(".h")||p.endsWith(".cpp")||p.endsWith(".hpp")||p.endsWith(".ino"))return"ace/mode/c_cpp";
 return"ace/mode/text";
}
async function api(p,o){const r=await fetch(p,o);return r.json();}
function fmtSize(b){return b>1048576?(b/1048576).toFixed(2)+" MB":b>1024?(b/1024).toFixed(1)+" KB":b+" B";}
async function refreshList(){
  listEl.innerHTML=""; const j=await api("/api/fs/list");
  (j.files||[]).sort((a,b)=>a.path.localeCompare(b.path)).forEach(f=>{
    const row=document.createElement("div"); row.className="item";
    row.innerHTML=`<div class="path" title="${f.path}">${f.path}</div>
      <div class="row"><span class="badge">${fmtSize(f.size)}</span>
      <button data-open>✏️</button><a class="mut" href="${encodeURI(f.path)}" download>⬇️</a>
      <button data-del>🗑️</button></div>`;
    row.querySelector("[data-open]").onclick=()=>openFile(f.path,f.size);
    row.querySelector("[data-del]").onclick=()=>delFile(f.path);
    listEl.appendChild(row);
  });
}
async function openFile(path,size){
  currentPath=path; curPathEl.textContent=path;
  downloadA.href=encodeURI(path); downloadA.download=path.split("/").pop();
  const j=await api("/api/fs/get?path="+encodeURIComponent(path));
  if(!j.ok){ currentBinary=true; if(editor)editor.setValue("",-1);
    curInfoEl.textContent=j.binary?`Бинарный/большой (${fmtSize(j.size||0)}) — редактирование отключено.`:"Невозможно открыть";
    saveBtn.disabled=true; return; }
  currentBinary=false;
  if(editor){ editor.session.setMode(modeByExt(path)); editor.setValue(j.text||"", -1); }
  curInfoEl.textContent="Открыт для редактирования"; saveBtn.disabled=true;
}
async function save(){
  if(!currentPath||currentBinary)return;
  const text = editor?editor.getValue():"";
  const r=await fetch("/api/fs/put?path="+encodeURIComponent(currentPath),{method:"POST",body:text});
  const j=await r.json(); if(j.ok){ saveBtn.disabled=true; await refreshList(); curInfoEl.textContent="Сохранено"; }
}
async function delFile(path){
  if(!confirm("Удалить "+path+" ?"))return;
  const j=await api("/api/fs/delete",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"path="+encodeURIComponent(path)});
  if(j.ok){ if(path===currentPath){currentPath="";curPathEl.textContent="—";if(editor)editor.setValue("",-1);} refreshList(); }
}
async function createFile(){
  let p=newPath.value.trim(); if(!p)return; if(!p.startsWith("/"))p="/"+p;
  const j=await fetch("/api/fs/put?path="+encodeURIComponent(p),{method:"POST",body:""}); const jj=await j.json();
  if(jj.ok){ newPath.value=""; await refreshList(); openFile(p,0); }
}
async function uploadFiles(files){
  for(const f of files){ const fd=new FormData(); fd.append("file",f,f.name); const to="/"+f.name;
    await fetch("/upload?to="+encodeURIComponent(to),{method:"POST",body:fd}); }
  await refreshList();
}
refreshBtn.onclick=refreshList; createBtn.onclick=createFile; saveBtn.onclick=save;
up.addEventListener("change",async e=>{ await uploadFiles(e.target.files); up.value=""; });
["dragenter","dragover"].forEach(t=>drop.addEventListener(t,e=>{e.preventDefault();drop.classList.add("drag");}));
["dragleave","drop"].forEach(t=>drop.addEventListener(t,e=>{e.preventDefault();drop.classList.remove("drag");}));
drop.addEventListener("drop",e=>uploadFiles(e.dataTransfer.files));
window.addEventListener("load",()=>{initEditor();refreshList();});
</script></body></html>)HTML";

static const char OTA_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OTA обновление</title>
<style>
:root{--bg:#0b1220;--card:#0d1728;--ink:#e8eef7;--mut:#9fb3d1;--br:#1b2a44;--btn:#143057;--ok:#95ffa1;--err:#ff9a9a}
body{margin:0;background:var(--bg);color:var(--ink);font:15px system-ui,-apple-system,Segoe UI,Roboto}
.wrap{max-width:720px;margin:auto;padding:20px}.card{background:var(--card);border:1px solid var(--br);border-radius:14px;padding:16px}
h1{font-size:18px;margin:0 0 12px}
input,button{padding:10px 12px;border-radius:10px;border:1px solid var(--br);background:#0f1a2c;color:var(--ink)}
button{background:var(--btn);cursor:pointer}
.bar{height:12px;border:1px solid var(--br);border-radius:999px;overflow:hidden;background:#0f1a2c;margin-top:10px}
.fill{height:100%;width:0%}.ok{color:var(--ok)}.err{color:var(--err)}.mut{color:var(--mut)}
</style></head><body><div class="wrap"><div class="card">
<h1>OTA обновление прошивки</h1>
<form id="f" class="row"><input id="bin" type="file" accept=".bin" required><button id="go">🚀 Обновить</button>
<a class="mut" href="/">Главная</a>    <a class="mut" href="/fs">Файлы</a>    <a class="mut" href="/wifi">Wi-Fi</a></form>
<div class="bar"><div class="fill" id="fill"></div></div><div id="log" class="mut" style="margin-top:8px"></div>
</div></div>
<script>
const $=s=>document.querySelector(s); const form=$("#f"),bin=$("#bin"),go=$("#go"),fill=$("#fill"),log=$("#log");
form.addEventListener("submit",ev=>{
  ev.preventDefault(); const file=bin.files&&bin.files[0]; if(!file)return; go.disabled=true; log.textContent="Загрузка...";
  const xhr=new XMLHttpRequest(); xhr.upload.onprogress=e=>{ if(e.lengthComputable) fill.style.width=Math.round(e.loaded*100/e.total)+"%"; };
  xhr.onreadystatechange=()=>{ if(xhr.readyState===4){ go.disabled=false;
    if(xhr.status===200){ fill.style.width="100%"; log.innerHTML="<span class='ok'>Готово. Перезагрузка...</span>"; }
    else{ log.innerHTML="<span class='err'>Ошибка: "+xhr.status+" "+xhr.statusText+"</span>"; } } };
  const fd=new FormData(); fd.append("file",file,file.name); xhr.open("POST","/ota"); xhr.send(fd);
});
</script></body></html>)HTML";

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TK Wi-Fi Manager</title>
<style>
:root{--bg:#0b1220;--card:#0d1728;--ink:#e8eef7;--mut:#9fb3d1;--br:#1b2a44;--btn:#143057}
body{margin:0;background:var(--bg);color:var(--ink);font:15px system-ui,-apple-system,Segoe UI,Roboto}
.wrap{max-width:860px;margin:auto;padding:20px}.card{background:var(--card);border:1px solid var(--br);border-radius:14px;padding:16px}
h1{font-size:18px;margin:0 0 12px}button{padding:10px 12px;border-radius:10px;border:1px solid var(--br);background:#0f1a2c;color:var(--ink);cursor:pointer}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
</style></head><body><div class="wrap"><div class="card">
<h1>TK Wi-Fi Manager</h1>
<div id="st" class="mut">...</div>
<div class="row" style="margin-top:10px">
<a href="/wifi"><button>Wi-Fi</button></a>
<a href="/fs"><button>Файлы</button></a>
<a href="/ota"><button>OTA</button></a>
</div>
<script>
const st=document.getElementById('st'); const ws=new WebSocket('ws://'+location.hostname+':81/');
ws.onopen=()=>ws.send('status');
ws.onmessage=e=>{ try{const j=JSON.parse(e.data); if(j.type==='status') st.innerHTML=(j.mode==='AP'?'AP (каптив)':'STA')+' • IP: <b>'+ (j.ip||'-') +'</b>'; }catch(_){ } };
</script>
</div></div></body></html>)HTML";

const char* TKWifiManager::builtinIndex() { return INDEX_HTML; }
const char* TKWifiManager::builtinWifi() { return WIFI_HTML; }
const char* TKWifiManager::builtinFs() { return FS_HTML; }
const char* TKWifiManager::builtinOta() { return OTA_HTML; }

// ========================= Реализация ==========================
TKWifiManager::TKWifiManager(uint16_t httpPort)
    : _httpPort(httpPort), _server(httpPort), _ws(TKWM_WS_PORT) {
}

bool TKWifiManager::begin(const String& apSsidPrefix, bool formatFSIfNeeded) {
    _apSsidPrefix = apSsidPrefix;
#if TKWM_USE_LITTLEFS
    Serial.println(F("[TKWM] FS = LittleFS"));
#else
    Serial.println(F("[TKWM] FS = SPIFFS"));
#endif

    _fsOk = TKWM_FS.begin(true);
    if (!_fsOk && formatFSIfNeeded) {
        Serial.println(F("[TKWM] FS mount failed, formatting..."));
        TKWM_FS.format();
        _fsOk = TKWM_FS.begin(true);
    }
    Serial.printf("[TKWM] FS mount: %s\n", _fsOk ? "OK" : "FAIL");
    
    // Wi-Fi creds
    loadCreds();

    // Попробуем подключиться к лучшей из известных
    bool staOk = tryConnectBestKnown();
    if (!staOk) startAPCaptive(apSsidPrefix);

    // Маршруты и WS
    setupRoutes();
    _server.begin();
    setupWebSocket();
    _ws.begin();

    // Power-save OFF — стабильнее скан
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    // UDP discovery
    _udp.begin(TKWM_DISCOVERY_PORT);
    return true;
}

void TKWifiManager::loop() {
    if (_captiveMode) _dns.processNextRequest();
    _server.handleClient();
    _ws.loop();
    udpTick();

    // если в STA пропала сеть — вернёмся в AP
    static uint32_t t = 0;
    if (!_captiveMode && millis() - t > 4000) {
        t = millis();
        if (WiFi.status() != WL_CONNECTED) startAPCaptive(_apSsid.c_str()); // префикс уже содержит suffix — не критично
    }
}

// ======================= Creds ========================
void TKWifiManager::loadCreds() {
    _prefs.begin("tkw_net", true);
    _credN = _prefs.getInt("count", 0);
    if (_credN < 0 || _credN > TKWM_MAX_CRED) _credN = 0;
    for (int i = 0; i < _credN; i++) {
        _creds[i].ssid = _prefs.getString((String("s") + i).c_str(), "");
        _creds[i].pass = _prefs.getString((String("p") + i).c_str(), "");
    }
    _prefs.end();
}
void TKWifiManager::saveCount() {
    _prefs.begin("tkw_net", false);
    _prefs.putInt("count", _credN);
    _prefs.end();
}
void TKWifiManager::saveAt(int idx, const String& ssid, const String& pass) {
    if (idx < 0 || idx >= TKWM_MAX_CRED) return;
    _prefs.begin("tkw_net", false);
    _prefs.putString((String("s") + idx).c_str(), ssid);
    _prefs.putString((String("p") + idx).c_str(), pass);
    _prefs.end();
}
int TKWifiManager::findBySsid(const String& ssid) const {
    for (int i = 0; i < _credN; i++) if (_creds[i].ssid == ssid) return i;
    return -1;
}

// ======================= Wi-Fi ========================
bool TKWifiManager::tryConnectBestKnown(uint32_t timeoutMs) {
    if (_credN == 0) return false;
    // sync scan (AP не выключаем)
    int n = WiFi.scanNetworks(/*async*/false, /*hidden*/true);
    int bestRssi = -9999, bestIdx = -1;
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        int idx = findBySsid(s);
        if (idx >= 0) {
            int rssi = WiFi.RSSI(i);
            if (rssi > bestRssi) { bestRssi = rssi; bestIdx = idx; }
        }
    }
    if (bestIdx < 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(50);
    WiFi.begin(_creds[bestIdx].ssid.c_str(), _creds[bestIdx].pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        _captiveMode = false;
        _dns.stop();
        return true;
    }
    return false;
}

void TKWifiManager::startAPCaptive() {
    _captiveMode = true;

    // уникальный SSID: <prefix>-XXXXXX
    if (!_apSsid.length()) {
        uint32_t suf = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
        char macs[7]; snprintf(macs, sizeof(macs), "%06X", suf);
        _apSsid = _apSsidPrefix + "-" + macs;
    }

    WiFi.mode(WIFI_AP_STA);
    IPAddress ip(192, 168, 4, 1), gw(192, 168, 4, 1), mask(255, 255, 255, 0);
    WiFi.softAPConfig(ip, gw, mask);
    WiFi.softAP(_apSsid.c_str()); // без пароля, как в вашем коде
    _dns.start(53, "*", ip);
}

// ===================== Web/Routes =====================
void TKWifiManager::setupRoutes() {
    // главная
    _server.on("/", HTTP_GET, [this] { handleRoot(); });

    // captive детекторы
    _server.on("/generate_204", HTTP_ANY, [this] { handleCaptiveProbe(); });
    _server.on("/hotspot-detect.html", HTTP_ANY, [this] { handleCaptiveProbe(); });
    _server.on("/ncsi.txt", HTTP_ANY, [this] { handleCaptiveProbe(); });

    // Wi-Fi
    _server.on("/wifi", HTTP_GET, [this] { handleWifiPage(); });
    _server.on("/api/wifi/save", HTTP_POST, [this] { handleWifiSave(); });
    _server.on("/api/reconnect", HTTP_POST, [this] { handleReconnect(); });
    _server.on("/api/start_ap", HTTP_POST, [this] { handleStartAP(); });
    _server.on("/api/wifi/saved", HTTP_GET, [this] { handleWifiListSaved(); });
    _server.on("/api/wifi/delete", HTTP_POST, [this] { handleWifiDelete();    });

    // FS API
    _server.on("/api/fs/list", HTTP_GET, [this] { handleFsList();   });
    _server.on("/api/fs/get", HTTP_GET, [this] { handleFsGet();    });
    _server.on("/api/fs/put", HTTP_POST, [this] { handleFsPut();    });
    _server.on("/api/fs/delete", HTTP_POST, [this] { handleFsDelete(); });
    _server.on("/api/fs/mkdir", HTTP_POST, [this] { handleFsMkdir();  });

    // FS страница
    _server.on("/fs", HTTP_GET, [this]() {
        if (_fsOk && streamIfExists("/fs.html")) return;
        _server.send(200, "text/html; charset=utf-8", builtinFs());
        });

    // Загрузка (multipart). Путь обязателен через ?to=/полный/путь/имя
    _server.on("/upload", HTTP_POST, [this] { handleUploadDone(); }, [this] { handleUpload(); });

    // OTA
    _server.on("/ota", HTTP_GET, [this] { handleOtaPage(); });
    _server.on("/ota", HTTP_POST, [this] { handleOtaFinish(); }, [this] { handleOtaUpload(); });

    // 404
    _server.onNotFound([this] { handleNotFound(); });
}

void TKWifiManager::setupWebSocket() {
    _ws.onEvent([this](uint8_t id, WStype_t t, uint8_t* p, size_t l) {
        switch (t) {
        case WStype_CONNECTED: wsSendStatus(id); break;
        case WStype_TEXT: {
            String s; s.reserve(l);
            for (size_t i = 0; i < l; i++) s += (char)p[i];
            if (s == "scan")       wsRunScanAndPublish();
            else if (s == "status") wsSendStatus(id);
            else if (_userWsHook)   _userWsHook(id, t, p, l);
        } break;
        default:
            if (_userWsHook) _userWsHook(id, t, p, l);
        }
        });
}

// ===================== HTTP handlers ===================
void TKWifiManager::handleRoot() {
    if (_fsOk && streamIfExists("/index.html")) return;
    _server.send(200, "text/html; charset=utf-8", builtinIndex());
}

void TKWifiManager::handleCaptiveProbe() {
    if (_captiveMode) {
        _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/wifi");
        _server.send(302, "text/plain", "");
    }
    else {
        _server.send(204);
    }
}

void TKWifiManager::handleWifiPage() {
    if (_fsOk && streamIfExists("/wifi.html")) return;
    _server.send(200, "text/html; charset=utf-8", builtinWifi());
}

void TKWifiManager::handleWifiSave() {
    if (!_server.hasArg("plain")) { _server.send(400, "application/json", "{\"ok\":false}"); return; }
    String body = _server.arg("plain");
    String ssid, pass;

    // простой JSON без ArduinoJson
    int ps = body.indexOf("\"ssid\"");
    if (ps >= 0) {
        int q1 = body.indexOf('"', body.indexOf(':', ps) + 1);
        int q2 = body.indexOf('"', q1 + 1);
        if (q1 >= 0 && q2 > q1) ssid = body.substring(q1 + 1, q2);
    }
    ps = body.indexOf("\"password\"");
    if (ps >= 0) {
        int q1 = body.indexOf('"', body.indexOf(':', ps) + 1);
        int q2 = body.indexOf('"', q1 + 1);
        if (q1 >= 0 && q2 > q1) pass = body.substring(q1 + 1, q2);
    }

    if (ssid.isEmpty()) { _server.send(200, "application/json", "{\"ok\":false,\"msg\":\"ssid empty\"}"); return; }

    int idx = findBySsid(ssid);
    if (idx < 0) {
        if (_credN >= TKWM_MAX_CRED) { _server.send(200, "application/json", "{\"ok\":false,\"msg\":\"full\"}"); return; }
        _creds[_credN] = { ssid, pass }; saveAt(_credN, ssid, pass); _credN++; saveCount();
    }
    else {
        _creds[idx].pass = pass; saveAt(idx, ssid, pass);
    }

    bool ok = tryConnectBestKnown();
    if (ok) {
        String out = String("{\"ok\":true,\"connected\":true,\"ip\":\"") + WiFi.localIP().toString() + "\"}";
        _server.send(200, "application/json", out);
    }
    else {
        _server.send(200, "application/json", "{\"ok\":true,\"connected\":false}");
    }
}

void TKWifiManager::handleReconnect() {
    bool ok = tryConnectBestKnown();
    if (ok) {
        String out = String("{\"ok\":true,\"ip\":\"") + WiFi.localIP().toString() + "\"}";
        _server.send(200, "application/json", out);
    }
    else {
        _server.send(200, "application/json", "{\"ok\":false}");
    }
}

void TKWifiManager::handleStartAP() {
    startAPCaptive(_apSsid.c_str());
    _server.send(200, "application/json", "{\"ok\":true}");
    String mode = "AP";
    String ipS = WiFi.softAPIP().toString();
    String j = String("{\"type\":\"status\",\"mode\":\"") + mode + "\",\"ip\":\"" + ipS + "\"}";
    _ws.broadcastTXT(j);
}

void TKWifiManager::handleWifiListSaved() {
    // Ответ: { "ok": true, "nets": [ "ssid1", "ssid2", ... ] }
    String out;
    out.reserve(_credN * 24 + 32);
    out += F("{\"ok\":true,\"nets\":[");
    for (int i = 0; i < _credN; ++i) {
        if (i) out += ',';
        out += '\"';
        const String& s = _creds[i].ssid;
        for (size_t k = 0; k < s.length(); ++k) {
            char c = s[k];
            if (c == '\"' || c == '\\') { out += '\\'; out += c; }
            else if ((uint8_t)c < 0x20) {
                char esc[7]; snprintf(esc, sizeof(esc), "\\u%04X", (unsigned char)c);
                out += esc;
            }
            else out += c;
        }
        out += '\"';
    }
    out += "]}";
    _server.send(200, "application/json", out);
}

void TKWifiManager::handleWifiDelete() {
    // ожидаем form-urlencoded: ssid=<name>
    String ssid = _server.arg("ssid");
    if (ssid.isEmpty()) { _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"ssid empty\"}"); return; }

    int idx = findBySsid(ssid);
    if (idx < 0) { _server.send(200, "application/json", "{\"ok\":true,\"removed\":false}"); return; }

    // сдвигаем массив
    for (int i = idx; i < _credN - 1; ++i) _creds[i] = _creds[i + 1];
    _credN--;

    // перезапишем хранилище заново (простая и надёжная тактика)
    _prefs.begin("tkw_net", false);
    _prefs.putInt("count", _credN);
    for (int i = 0; i < _credN; ++i) {
        _prefs.putString((String("s") + i).c_str(), _creds[i].ssid);
        _prefs.putString((String("p") + i).c_str(), _creds[i].pass);
    }
    // подчистим «хвост» старых ключей (не обязательно, но приятно)
    for (int i = _credN; i < TKWM_MAX_CRED; ++i) {
        _prefs.remove((String("s") + i).c_str());
        _prefs.remove((String("p") + i).c_str());
    }
    _prefs.end();

    _server.send(200, "application/json", "{\"ok\":true,\"removed\":true}");

    // если мы были подключены к только что удалённой сети — это на совести пользователя.
    // при желании можно тут вызвать startAPCaptive/_ws.broadcastTXT("status"), но не обязательно.
}

// ===== FS API =====
void TKWifiManager::handleFsList() {
    if (!_fsOk) { _server.send(500, "application/json", "{\"files\":[]}"); return; }
    String out = "{\"files\":[";
    File root = TKWM_FS.open("/");
    bool first = true;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        if (!first) out += ",";
        first = false;
        out += "{\"path\":\""; out += f.name(); out += "\",\"size\":"; out += String((uint32_t)f.size()); out += "}";
    }
    out += "]}";
    _server.send(200, "application/json", out);
}

void TKWifiManager::handleFsGet() {
    String path = _server.arg("path");
    if (!path.startsWith("/")) path = "/" + path;
    if (!_fsOk || !TKWM_FS.exists(path)) {
        _server.send(404, "application/json", "{\"ok\":false,\"msg\":\"not found\"}");
        return;
    }

    File f = TKWM_FS.open(path, "r");
    size_t sz = f.size();
    if (sz > 256 * 1024 || !looksText(f)) {
        f.close();
        String resp = String("{\"ok\":false,\"binary\":true,\"size\":") + sz + "}";
        _server.send(200, "application/json", resp);
        return;
    }

    // ——— Стримовый ответ (chunked) с корректным JSON-экранированием текста ———
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", ""); // заголовки

    auto sendChunk = [this](const String& s) { _server.sendContent(s); };

    sendChunk(F("{\"ok\":true,\"text\":\""));

    // читаем по кускам и экранируем спецсимволы
    const size_t BUFSZ = 1024;
    uint8_t buf[BUFSZ];
    while (f.available()) {
        size_t n = f.read(buf, BUFSZ);
        String out; out.reserve(n * 2); // с запасом под экранирование
        for (size_t i = 0; i < n; ++i) {
            char c = (char)buf[i];
            switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                // JSON требует экранировать все управляющие < 0x20
                if ((uint8_t)c < 0x20) {
                    char esc[7];
                    snprintf(esc, sizeof(esc), "\\u%04X", (unsigned char)c);
                    out += esc;
                }
                else {
                    out += c;
                }
            }
        }
        sendChunk(out);
    }
    f.close();

    sendChunk(F("\"}"));
}

void TKWifiManager::handleFsPut() {
    String path = _server.arg("path");
    if (!path.startsWith("/")) path = "/" + path;
    String body = _server.arg("plain");
    ensureDirs(path);
    if (!_fsOk) { _server.send(500, "application/json", "{\"ok\":false}"); return; }
    File f = TKWM_FS.open(path, "w");
    if (!f) { _server.send(500, "application/json", "{\"ok\":false,\"msg\":\"open failed\"}"); return; }
    size_t w = f.print(body);
    f.close();
    String out = String("{\"ok\":true,\"wrote\":") + w + "}";
    _server.send(200, "application/json", out);
}

void TKWifiManager::handleFsDelete() {
    String path = _server.arg("path");
    if (!path.startsWith("/")) path = "/" + path;
    bool ok = _fsOk && TKWM_FS.remove(path);
    _server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void TKWifiManager::handleFsMkdir() {
    String path = _server.arg("path");
    if (!path.startsWith("/")) path = "/" + path;
    bool ok = _fsOk && TKWM_FS.mkdir(path);
    _server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ===== Upload =====
void TKWifiManager::handleUpload() {
    static size_t _written = 0;

    HTTPUpload& up = _server.upload();
    if (up.status == UPLOAD_FILE_START) {
        _written = 0;
        _uploadToPath = _server.arg("to");              // ожидаем полный путь с именем
        if (_uploadToPath.isEmpty()) _uploadToPath = "/";
        if (!_uploadToPath.startsWith("/")) _uploadToPath = "/" + _uploadToPath;

        ensureDirs(_uploadToPath);

        if (!_fsOk) {
            Serial.println(F("[TKWM] Upload start, but FS not mounted"));
            return;
        }
        _uploadFile = TKWM_FS.open(_uploadToPath, "w");
        if (!_uploadFile) {
            Serial.printf("[TKWM] Upload open failed: %s\n", _uploadToPath.c_str());
        }
        else {
            Serial.printf("[TKWM] Upload start: %s (totalSize=%u)\n",
                _uploadToPath.c_str(), (unsigned)up.totalSize);
        }
    }
    else if (up.status == UPLOAD_FILE_WRITE) {
        if (_uploadFile) {
            size_t w = _uploadFile.write(up.buf, up.currentSize);
            _written += w;
            if (w != up.currentSize) {
                Serial.printf("[TKWM] Upload partial write: %u/%u\n",
                    (unsigned)w, (unsigned)up.currentSize);
            }
        }
    }
    else if (up.status == UPLOAD_FILE_END) {
        if (_uploadFile) {
            _uploadFile.close();
            Serial.printf("[TKWM] Upload end: wrote=%u\n", (unsigned)_written);
        }
        else {
            Serial.println(F("[TKWM] Upload end with no file handle"));
        }
    }
    else if (up.status == UPLOAD_FILE_ABORTED) {
        if (_uploadFile) _uploadFile.close();
        Serial.println(F("[TKWM] Upload aborted"));
    }
}

void TKWifiManager::handleUploadDone() {
    // Проверим, что файл реально существует и не нулевого размера
    String to = _uploadToPath.length() ? _uploadToPath : "/";
    bool ok = false;
    size_t sz = 0;

    if (_fsOk && TKWM_FS.exists(to)) {
        File f = TKWM_FS.open(to, "r");
        if (f) { sz = f.size(); f.close(); }
        ok = (sz > 0 || to.endsWith("/")); // для директорий размер 0 норм
    }

    String resp = "<!doctype html><meta charset='utf-8'>";
    if (ok) {
        resp += "OK (" + String((unsigned)sz) + " bytes). ";
    }
    else {
        resp += "Загрузка завершилась, но файл не найден или пуст. ";
    }
    resp += "<a href='" + to + "'>Открыть " + to + "</a>";
    _server.send(200, "text/html; charset=utf-8", resp);
    _uploadToPath = "";
}

// ===== OTA =====
void TKWifiManager::handleOtaPage() {
    if (_fsOk && streamIfExists("/ota.html")) return;
    _server.send(200, "text/html; charset=utf-8", builtinOta());
}

void TKWifiManager::handleOtaUpload() {
    static bool   inProg = false;
    static size_t wrote = 0;
    static String err;

    HTTPUpload& up = _server.upload();
    if (up.status == UPLOAD_FILE_START) {
        inProg = true; wrote = 0; err = "";
        size_t sz = up.totalSize;
        if (!Update.begin(sz ? sz : UPDATE_SIZE_UNKNOWN)) {
            err = String("Update.begin failed: ") + Update.errorString();
        }
    }
    else if (up.status == UPLOAD_FILE_WRITE) {
        if (err.isEmpty()) {
            size_t w = Update.write(up.buf, up.currentSize);
            if (w != up.currentSize) err = String("Write failed: ") + Update.errorString();
            else wrote += w;
        }
    }
    else if (up.status == UPLOAD_FILE_END) {
        if (err.isEmpty()) { if (!Update.end(true)) err = String("Update.end failed: ") + Update.errorString(); }
        else Update.abort();
        inProg = false;
    }
    else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort(); inProg = false; err = "Aborted";
    }
}

void TKWifiManager::handleOtaFinish() {
    // этот handler вызывается после handleOtaUpload()
    // отдадим html-результат и, если успех — перезагрузимся
    String html;
    if (Update.hasError()) {
        html = String("<!doctype html><meta charset='utf-8'><title>OTA</title>"
            "<h3 style='color:#ff9a9a'>Ошибка OTA</h3><pre>") + Update.errorString() + "</pre>";
        _server.send(200, "text/html; charset=utf-8", html);
    }
    else {
        html = String("<!doctype html><meta charset='utf-8'><title>OTA</title>"
            "<h3 style='color:#95ffa1'>Готово</h3>"
            "<p>Перезагрузка...</p><script>setTimeout(()=>location.href='/',4000)</script>");
        _server.send(200, "text/html; charset=utf-8", html);
        delay(1500);
        ESP.restart();
    }
}

// ===== notFound =====
void TKWifiManager::handleNotFound() {
    String uri = _server.uri();
    if (_fsOk && streamIfExists(uri)) return;
    if (_captiveMode) {
        _server.sendHeader("Location", "/wifi", true);
        _server.send(302, "text/plain", "");
        return;
    }
    sendUpload404(uri);
}

// =================== WebSocket helpers =================
void TKWifiManager::wsSendStatus(uint8_t clientId) {
    String mode = _captiveMode ? "AP" : "STA";
    String ipS = (_captiveMode ? WiFi.softAPIP() : WiFi.localIP()).toString();
    String j = String("{\"type\":\"status\",\"mode\":\"") + mode + "\",\"ip\":\"" + ipS + "\"}";
    _ws.sendTXT(clientId, j);
}

static void ensureWifiForScan_() {
    // режим AP+STA (не выключаем AP)
    wifi_mode_t cur;
    esp_wifi_get_mode(&cur);
    if (cur != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    // задать страну/политику — чтобы скан ходил по всем 1..13
    wifi_country_t ctry = {
      .cc = "EU",              // или "00" (world), но "EU" стабильно даёт 1..13
      .schan = 1,
      .nchan = 13,
      .max_tx_power = 20,
      .policy = WIFI_COUNTRY_POLICY_MANUAL
    };
    esp_wifi_set_country(&ctry);
    // выключим power save — скану так легче
    esp_wifi_set_ps(WIFI_PS_NONE);

    // убедимся, что стек реально запущен
    esp_wifi_start();
}

// --- сериализация ssid с экранированием в JSON ---
static inline void appendEscapedSsid_(String& out, const uint8_t* ssid, uint8_t len) {
    out += '\"';
    for (uint8_t i = 0; i < len; ++i) {
        char c = (char)ssid[i];
        if (c == '\"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c < 0x20) {
            char esc[7]; snprintf(esc, sizeof(esc), "\\u%04X", (unsigned char)c);
            out += esc;
        }
        else out += c;
    }
    out += '\"';
}

// --- основной сканер (три попытки) ---
void TKWifiManager::wsRunScanAndPublish() {
    ensureWifiForScan_();    
    int n = WiFi.scanNetworks(false, true);
    String out;
    out.reserve(64 * max(n, 1) + 32);
    out += F("{\"type\":\"scan\",\"nets\":[");
    for (int i = 0; i < n; ++i) {
        if (i) out += ',';
        out += '{';
        out += F("\"ssid\":\"");
        String s = WiFi.SSID(i);
        for (size_t k = 0; k < s.length(); k++) { char c = s[k]; if (c == '\"' || c == '\\') { out += '\\'; out += c; } else if ((uint8_t)c < 0x20) { char esc[7]; snprintf(esc, sizeof(esc), "\\u%04X", (unsigned char)c); out += esc; } else out += c; }
        out += F("\",\"rssi\":"); out += String(WiFi.RSSI(i));
        out += F(",\"ch\":");     out += String(WiFi.channel(i));
        out += F(",\"enc\":");    out += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1);
        out += '}';
    }
    out += "]}";
    _ws.broadcastTXT(out);    
}

// =================== UDP discovery =====================
void TKWifiManager::udpTick() {
    int sz = _udp.parsePacket();
    if (sz <= 0) return;
    char buf[64] = { 0 };
    int n = _udp.read(buf, sizeof(buf) - 1);
    buf[n] = 0;
    if (!String(buf).startsWith(TKWM_DISCOVERY_SIGNATURE)) return;
    String payload = String("{\"id\":\"") + _apSsid + "\",\"name\":\"" + _apSsid + "\",\"ip\":\"" + ip().toString() + "\",\"model\":\"ESP32\",\"web\":\"/\"}";
    _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
    _udp.print(payload);
    _udp.endPacket();
}

// =================== FS helpers ========================
String TKWifiManager::contentType(const String& path) {
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

void TKWifiManager::ensureDirs(const String& path) {
    int i = 1;
    while ((i = path.indexOf('/', i)) > 0) {
        TKWM_FS.mkdir(path.substring(0, i));
        i++;
    }
}

bool TKWifiManager::looksText(File& f) {
    const size_t N = 256; uint8_t buf[N]; size_t n = f.read(buf, N); f.seek(0);
    for (size_t i = 0; i < n; i++) if (buf[i] == 0) return false;
    return true;
}

bool TKWifiManager::streamIfExists(const String& uri) {
    String path = uri;
    if (!path.startsWith("/")) path = "/" + path;
    if (path.endsWith("/")) path += "index.html";
    if (!TKWM_FS.exists(path)) return false;
    File f = TKWM_FS.open(path, "r");
    if (!f) return false;
    _server.streamFile(f, contentType(path));
    f.close();
    return true;
}

void TKWifiManager::sendUpload404(const String& missingPath) {
    String p = missingPath; if (!p.startsWith("/")) p = "/" + p;
    String html;
    html += F("<!doctype html><meta charset='utf-8'><title>404 — файла нет</title>"
        "<body style='font:14px system-ui;background:#0b1220;color:#e8eef7;padding:24px'>"
        "<div style='max-width:720px;margin:auto;background:#0d1728;border:1px solid #1b2a44;border-radius:12px;padding:16px'>");
    html += F("<h3>Файл не найден</h3><p>Запрошен путь: <code>");
    html += p;
    html += F("</code></p><form method='POST' action='/upload?to=");
    html += p;
    html += F("' enctype='multipart/form-data'><input type='file' name='file' required> <button>Загрузить</button></form>"
        "<p style='opacity:.8;margin-top:12px'>Подсказка: для главной загрузите <code>/index.html</code>.</p></div></body>");
    _server.send(404, "text/html; charset=utf-8", html);
}
