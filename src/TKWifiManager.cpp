#include "TKWifiManager.h"
#include "esp_wifi.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstring>

// Токен порта встроен в JS как число, чтобы встроенные HTML совпадали с макросом TKWM_WS_PORT
#ifndef TKWM_XSTR
#define TKWM_XSTR2(x) #x
#define TKWM_XSTR(x)  TKWM_XSTR2(x)
#endif

// forward declaration (определение — ниже, перед wsRunScanAndPublish)
static void ensureWifiForScan_();
static String tkwmWebServerPostBody_(WebServer& s);

// ===================== ВСТРОЕННЫЕ СТРАНИЦЫ =====================
static const char WIFI_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wi-Fi настройка</title>
<script>(function(){try{var t=localStorage.getItem('tkwm-theme');if(t&&t!=='system')document.documentElement.dataset.theme=t;}catch(_){}})()</script>
<style>
:root{--bg:#0b1220;--card:#0d1728;--surface:#0f1a2c;--ink:#e8eef7;--mut:#9fb3d1;--br:#1b2a44;--btn:#143057;--link:#9fd0ff;--ok:#95ffa1;--err:#ff9a9a}
:root[data-theme="light"]{--bg:#f0f4f8;--card:#fff;--surface:#e4eaf2;--ink:#1a2236;--mut:#5a7090;--br:#c5d0e0;--btn:#2563eb;--link:#1d4ed8;--ok:#16a34a;--err:#dc2626}
@media(prefers-color-scheme:light){:root:not([data-theme]){--bg:#f0f4f8;--card:#fff;--surface:#e4eaf2;--ink:#1a2236;--mut:#5a7090;--br:#c5d0e0;--btn:#2563eb;--link:#1d4ed8;--ok:#16a34a;--err:#dc2626}}
body{margin:0;background:var(--bg);color:var(--ink);font:15px system-ui,-apple-system,Segoe UI,Roboto}
.wrap{max-width:860px;margin:auto;padding:20px}
.card{background:var(--card);border:1px solid var(--br);border-radius:14px;padding:16px}
h1{font-size:18px;margin:0 0 12px}
h2{font-size:16px;margin:14px 0 8px}
.row{display:flex;gap:12px;flex-wrap:wrap;align-items:center}
input,button{padding:10px 12px;border-radius:10px;border:1px solid var(--br);background:var(--surface);color:var(--ink)}
button{background:var(--btn);cursor:pointer}
a{color:var(--link);text-decoration:none}
.list{margin-top:12px}
.net{display:flex;justify-content:space-between;align-items:center;padding:10px;border:1px solid var(--br);border-radius:10px;margin:8px 0;background:var(--surface);cursor:pointer}
.net small{color:var(--mut)}
.badge{color:var(--mut);font-size:12px}
.ok{color:var(--ok)}.err{color:var(--err)}.mut{color:var(--mut)}
</style><link rel="stylesheet" href="/theme.css"><script src="/theme.js"></script></head><body><div class="wrap"><div class="card">
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
  ws = new WebSocket('ws://'+location.hostname+':'+)HTML" TKWM_XSTR(TKWM_WS_PORT) R"HTML(+'/');
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
<title>Файлы</title>
<script>(function(){try{var t=localStorage.getItem('tkwm-theme');if(t&&t!=='system')document.documentElement.dataset.theme=t;}catch(_){}})()</script>
<style>
:root{--bg:#0b1220;--card:#0d1728;--surface:#0f1a2c;--ink:#e8eef7;--mut:#9fb3d1;--br:#1b2a44;--btn:#143057;--link:#9fd0ff;--ok:#95ffa1;--err:#ff9a9a}
:root[data-theme="light"]{--bg:#f0f4f8;--card:#fff;--surface:#e4eaf2;--ink:#1a2236;--mut:#5a7090;--br:#c5d0e0;--btn:#2563eb;--link:#1d4ed8;--ok:#16a34a;--err:#dc2626}
@media(prefers-color-scheme:light){:root:not([data-theme]){--bg:#f0f4f8;--card:#fff;--surface:#e4eaf2;--ink:#1a2236;--mut:#5a7090;--br:#c5d0e0;--btn:#2563eb;--link:#1d4ed8;--ok:#16a34a;--err:#dc2626}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px system-ui,-apple-system,Segoe UI,Roboto}
.wrap{display:grid;grid-template-columns:340px 1fr;min-height:100vh}
.side{border-right:1px solid var(--br);padding:14px}.main{padding:14px}
h1{font-size:18px;margin:0 0 10px}
input,button{padding:8px 10px;border-radius:10px;border:1px solid var(--br);background:var(--surface);color:var(--ink)}
button{background:var(--btn);cursor:pointer}
a{color:var(--link);text-decoration:none}
.list{margin-top:10px;display:flex;flex-direction:column;gap:6px}
.item{display:flex;gap:8px;align-items:center;justify-content:space-between;border:1px solid var(--br);border-radius:10px;padding:8px;background:var(--surface)}
.item .path{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:190px}
.badge{color:var(--mut);font-size:12px}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.tools{display:flex;gap:8px;align-items:center;margin:8px 0}
#editor{position:relative;height:64vh;width:100%;border:1px solid var(--br);border-radius:12px}
.mut{color:var(--mut)}hr{border:0;border-top:1px solid var(--br);margin:12px 0}
.drop{border:2px dashed var(--br);border-radius:12px;padding:16px;text-align:center;margin-top:8px}
.drop.drag{background:var(--surface)}
@media (max-width:900px){.wrap{grid-template-columns:1fr}.side{border-right:0;border-bottom:1px solid var(--br)}}
</style><link rel="stylesheet" href="/theme.css"><script src="/theme.js"></script></head><body>
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

// Встроенный /ota: правьте src/ota.html, затем py src/_gen_ota_inc.py → TKWifiManager_ota.inc
#include "TKWifiManager_ota.inc"

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TK Wi-Fi Manager</title>
<script>(function(){try{var t=localStorage.getItem('tkwm-theme');if(t&&t!=='system')document.documentElement.dataset.theme=t;}catch(_){}})()</script>
<style>
:root{--bg:#0b1220;--card:#0d1728;--surface:#0f1a2c;--ink:#e8eef7;--mut:#9fb3d1;--br:#1b2a44;--btn:#143057;--link:#9fd0ff;--ok:#95ffa1;--err:#ff9a9a}
:root[data-theme="light"]{--bg:#f0f4f8;--card:#fff;--surface:#e4eaf2;--ink:#1a2236;--mut:#5a7090;--br:#c5d0e0;--btn:#2563eb;--link:#1d4ed8;--ok:#16a34a;--err:#dc2626}
@media(prefers-color-scheme:light){:root:not([data-theme]){--bg:#f0f4f8;--card:#fff;--surface:#e4eaf2;--ink:#1a2236;--mut:#5a7090;--br:#c5d0e0;--btn:#2563eb;--link:#1d4ed8;--ok:#16a34a;--err:#dc2626}}
body{margin:0;background:var(--bg);color:var(--ink);font:15px system-ui,-apple-system,Segoe UI,Roboto}
.wrap{max-width:860px;margin:auto;padding:20px}.card{background:var(--card);border:1px solid var(--br);border-radius:14px;padding:16px}
h1{font-size:18px;margin:0 0 12px}button{padding:10px 12px;border-radius:10px;border:1px solid var(--br);background:var(--surface);color:var(--ink);cursor:pointer}
a{color:var(--link);text-decoration:none}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
</style><link rel="stylesheet" href="/theme.css"><script src="/theme.js"></script></head><body><div class="wrap"><div class="card">
<h1>TK Wi-Fi Manager</h1>
<div id="st" class="mut">...</div>
<div id="otah" class="mut" style="display:none;margin-top:8px"><a href="/ota">Доступно обновление прошивки (ESPConnect)</a></div>
<div class="row" style="margin-top:10px">
<a href="/wifi"><button>Wi-Fi</button></a>
<a href="/fs"><button>Файлы</button></a>
<a href="/ota"><button>OTA</button></a>
</div>
<script>
const st=document.getElementById('st'),otah=document.getElementById('otah');
const ws=new WebSocket('ws://'+location.hostname+':'+)HTML" TKWM_XSTR(TKWM_WS_PORT) R"HTML(+'/');
ws.onopen=()=>ws.send('status');
ws.onmessage=e=>{ try{const j=JSON.parse(e.data); if(j.type==='status') st.innerHTML=(j.mode==='AP'?'AP (каптив)':'STA')+' • IP: <b>'+ (j.ip||'-') +'</b>'; }catch(_){ } };
setTimeout(function(){
 try{
  var sk=localStorage.getItem('tkwm_ota_skip')||'';
  fetch('/api/ota/config').then(function(r){ return r.json(); }).then(function(c){
   if(!c||!c.ok||!c.hasCreds||!c.auto) return;
   return fetch('/api/ota/check',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({host:c.host,token:c.token,skipVersion:sk.trim()})});
  }).then(function(x){ if(!x) return; return x.json?x.json():null; }).then(function(j){
   if(j&&j.ok&&j.updateAvailable&&otah) otah.style.display='block';
  });
 }catch(_){ }
},900);
</script>
</div></div></body></html>)HTML";

const char* TKWifiManager::builtinIndex() { return INDEX_HTML; }
const char* TKWifiManager::builtinWifi() { return WIFI_HTML; }
const char* TKWifiManager::builtinFs() { return FS_HTML; }
const char* TKWifiManager::builtinOta() { return OTA_HTML; }

// ===== Устойчивый разбор "ssid" / "password" из тела JSON (без внешних библиотек) =====
static int tkwmHex4_(const char* p) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else
            return -1;
        v = (v << 4) | d;
    }
    return v;
}
static void tkwmUtf8AppendCp_(String& s, uint32_t cp) {
    if (cp < 0x80)
        s += (char)cp;
    else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}
/** true если ключ найден; out — разобранное (пустая строка допустима) */
static bool tkwmJsonGetString(const String& j, const char* key, String& out) {
    const String keyPat = String("\"") + key + String("\"");
    int k = j.indexOf(keyPat);
    if (k < 0) return false;
    k = j.indexOf(':', k);
    if (k < 0) return false;
    int n = (int)j.length();
    int i = k + 1;
    while (i < n && (j[i] == ' ' || j[i] == '\t' || j[i] == '\r' || j[i] == '\n')) i++;
    if (i >= n || j[i] != '\"') return false;
    i++;
    out = "";
    while (i < n) {
        char c = (char)j[i];
        if (c == '\"') return true;
        if (c == '\\' && i + 1 < n) {
            char t = (char)j[i + 1];
            if (t == '\"' || t == '\\' || t == '/') {
                out += t;
                i += 2;
                continue;
            }
            if (t == 'b') { out += '\b'; i += 2; continue; }
            if (t == 'f') { out += '\f'; i += 2; continue; }
            if (t == 'n') { out += '\n'; i += 2; continue; }
            if (t == 'r') { out += '\r'; i += 2; continue; }
            if (t == 't') { out += '\t'; i += 2; continue; }
            if (t == 'u' && i + 6 < n) {
                int h = tkwmHex4_(j.c_str() + i + 2);
                if (h >= 0) {
                    tkwmUtf8AppendCp_(out, (uint32_t)h);
                    i += 6;
                    continue;
                }
            }
        }
        out += c;
        i++;
    }
    return false;
}

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
    loadOtaConf_();

    // Wi-Fi creds
    loadCreds();

    // Попробуем подключиться к лучшей из известных
    bool staOk = tryConnectBestKnown();
    if (!staOk) startAPCaptive();

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
    return true; // HTTP/WS готовы; состояние ФС — isFilesystemOk()
}

void TKWifiManager::loop() {
    if (_otaRestartPending && millis() >= _otaRestartAt) {
        ESP.restart();
    }
    if (_captiveMode) _dns.processNextRequest();
    _server.handleClient();
    _ws.loop();
    udpTick();

    // если в STA пропала сеть — вернёмся в AP
    static uint32_t t = 0;
    if (!_captiveMode && millis() - t > 4000) {
        t = millis();
        if (WiFi.status() != WL_CONNECTED) startAPCaptive();
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
        Serial.print(F("[TKWM] Wi-Fi STA: SSID="));
        Serial.print(_creds[bestIdx].ssid);
        Serial.print(F(" IP="));
        Serial.println(WiFi.localIP().toString());
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
    Serial.print(F("[TKWM] Wi-Fi AP: SSID="));
    Serial.print(_apSsid);
    Serial.print(F(" IP="));
    Serial.println(WiFi.softAPIP().toString());
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
    _server.on("/api/wifi/scan",  HTTP_GET,  [this] { handleWifiScan();      });

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
    _server.on("/api/ota/info", HTTP_GET, [this] { handleOtaInfo(); });
    _server.on("/api/ota/config", HTTP_GET, [this] { handleOtaConfig(); });
    _server.on("/api/ota/check", HTTP_POST, [this] { handleOtaCheck(); });
    _server.on("/api/ota/install", HTTP_POST, [this] { handleOtaInstall(); });
    _server.on("/api/ota/save", HTTP_POST, [this] { handleOtaSaveSettings(); });

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
    const String body = tkwmWebServerPostBody_(_server);
    if (!body.length()) {
        _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"no body\"}");
        return;
    }
    String          ssid, pass;

    if (!tkwmJsonGetString(body, "ssid", ssid)) {
        _server.send(200, "application/json", "{\"ok\":false,\"msg\":\"ssid required\"}");
        return;
    }
    if (!tkwmJsonGetString(body, "password", pass)) pass = "";

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
    startAPCaptive();
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

// Рекурсивный обход директорий для handleFsList
static void fsListDir_(File dir, String& out, bool& first) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) {
            fsListDir_(f, out, first);
        } else {
            if (!first) out += ",";
            first = false;
            out += F("{\"path\":\"");
            // f.name() в LittleFS возвращает полный путь вида /dir/file.txt
            const char* name = f.name();
            for (; *name; ++name) {
                if (*name == '\"' || *name == '\\') { out += '\\'; out += *name; }
                else out += *name;
            }
            out += F("\",\"size\":"); out += String((uint32_t)f.size()); out += '}';
        }
        f.close();
    }
}

void TKWifiManager::handleFsList() {
    if (!_fsOk) { _server.send(500, "application/json", "{\"files\":[]}"); return; }
    String out = "{\"files\":[";
    File root = TKWM_FS.open("/");
    bool first = true;
    fsListDir_(root, out, first);
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
        if (f) {
            if (f.isDirectory()) {
                f.close();
            } else {
                sz = f.size();
                f.close();
                ok = true; // в т.ч. пустой файл (0 байт)
            }
        }
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
        _otaRestartPending = true;
        _otaRestartAt      = millis() + 400;
    }
}

// ============== ESPConnect (сервер ESPTools) OTA ==============
static String tkwmNormHost_(String h) {
    h.trim();
    while (h.length() > 0 && h.endsWith("/")) h.remove(h.length() - 1);
    return h;
}
static void tkwmAppJsonVal_(String& o, const String& s) {
    for (uint32_t i = 0; i < s.length(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            o += '\\';
            o += (char)c;
        }
        else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04X", (unsigned)c);
            o += esc;
        }
        else
            o += (char)c;
    }
}
/** Соединение с HTTP, ответ 400/… про HTTPS — чаще всего указан http://, а порт/виртуалхост ждут TLS. */
static bool tkwmErrSuggestsHttps_(const String& r, const String& err) {
    String t = r + " " + err;
    t.toLowerCase();
    if (t.indexOf("an https server") >= 0) return true;
    if (t.indexOf("to an https") >= 0) return true;
    if (t.indexOf("https port") >= 0) return true;
    if (t.indexOf("plain http") >= 0 && t.indexOf("https") >= 0) return true;
    if (t.indexOf("tls") >= 0) return true;
    return false;
}
/** Тело JSON POST: в разных версиях/клиентах аргумент может называться иначе, чем "plain". */
static String tkwmWebServerPostBody_(WebServer& s) {
    if (s.hasArg("plain")) return s.arg("plain");
    if (s.hasArg("body")) return s.arg("body");
    if (s.hasArg("json")) return s.arg("json");
    for (int i = 0; i < s.args(); i++) {
        if (s.argName(i).length() == 0 && s.arg(i).length() > 0) return s.arg(i);
    }
    return String();
}
/** Символ токеном, как в C (напр. ESP32), чтобы совпадало с ESPConnect. */
static String tkwmOtaController_() {
#ifdef TKWM_OTA_CONTROLLER
#define TKW_OTA_CSTR1(x) #x
#define TKW_OTA_CSTR2(x) TKW_OTA_CSTR1(x)
    String c = String(TKW_OTA_CSTR2(TKWM_OTA_CONTROLLER));
#undef TKW_OTA_CSTR2
#undef TKW_OTA_CSTR1
    return c;
#else
    return String(ESP.getChipModel());
#endif
}

void TKWifiManager::loadOtaConf_() {
    _otaConfLoaded = true;
    _otaFileHost   = "";
    _otaFileToken  = "";
    _otaFileAuto   = -1;
    if (!_fsOk || !TKWM_FS.exists("/ota.conf")) return;
    File f = TKWM_FS.open("/ota.conf", "r");
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        int e = line.indexOf('=');
        if (e < 0) continue;
        String k = line.substring(0, e);
        k.trim();
        String v = line.substring(e + 1);
        v.trim();
        if (k == "host") {
            _otaFileHost = v;
        } else if (k == "token") {
            _otaFileToken = v;
        } else if (k == "auto") {
            v.toLowerCase();
            if (v == "1" || v == "true" || v == "yes" || v == "on")
                _otaFileAuto = 1;
            else
                _otaFileAuto = 0;
        }
    }
    f.close();
}

String TKWifiManager::otaConfigHost_() { return tkwmNormHost_(_otaFileHost); }
String TKWifiManager::otaConfigToken_() { return _otaFileToken; }
bool   TKWifiManager::otaConfigAuto_() {
    if (_otaFileAuto >= 0) return (bool)_otaFileAuto;
    _prefs.begin("tkw_ota", true);
    bool a = _prefs.getBool("auto", false);
    _prefs.end();
    return a;
}

void TKWifiManager::handleOtaInfo() {
    if (!_otaConfLoaded) loadOtaConf_();
    String ctrl = tkwmOtaController_();
    String out  = F("{\"ok\":true,\"controller\":\"");
    tkwmAppJsonVal_(out, ctrl);
    out += F("\",\"currentVersion\":\"");
    tkwmAppJsonVal_(out, String(TKWM_FW_VERSION));
    out += F("\"}");
    _server.send(200, "application/json", out);
}

void TKWifiManager::handleOtaConfig() {
    if (!_otaConfLoaded) loadOtaConf_();
    const String  h  = tkwmNormHost_(_otaFileHost);
    const String& tk = _otaFileToken;
    const bool    au = otaConfigAuto_();
    String        out;
    out.reserve(128 + h.length() + tk.length());
    out = F("{\"ok\":true,\"host\":\"");
    tkwmAppJsonVal_(out, h);
    out += F("\",\"token\":\"");
    tkwmAppJsonVal_(out, tk);
    out += F("\",\"auto\":");
    out += au ? "true" : "false";
    out += F(",\"hasCreds\":");
    out += (h.length() && tk.length()) ? "true" : "false";
    out += "}";
    _server.send(200, "application/json", out);
}

void TKWifiManager::handleOtaSaveSettings() {
    const String b = tkwmWebServerPostBody_(_server);
    if (!b.length()) {
        _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"no body\"}");
        return;
    }
    bool         au  = false;
    int           p  = b.indexOf(F("\"auto\""));
    if (p >= 0) {
        p = b.indexOf(':', p);
        if (p > 0) {
            p++;
            const int n = (int)b.length();
            while (p < n && (b[(unsigned)p] == ' ' || b[(unsigned)p] == '\t' || b[(unsigned)p] == '\r' || b[(unsigned)p] == '\n')) p++;
            if (b.substring(p, p + 4) == "true")
                au = true;
        }
    }
    _prefs.begin("tkw_ota", false);
    _prefs.putBool("auto", au);
    _prefs.end();
    _server.send(200, "application/json", "{\"ok\":true}");
}

// POST resolve-download на ESPConnect; out: firmware_version, download_url, latest_firmware_version, err
static bool tkwmEsptoolsResolve_(const String& base, const String& token, const String& controller, String& fw, String& dl, String& latest, String& err) {
    if (WiFi.status() != WL_CONNECTED) {
        err = "no internet (Wi-Fi not connected)";
        return false;
    }
    String post = F("{\"controller\":\"");
    tkwmAppJsonVal_(post, controller);
    post += F("\",\"firmware_type\":\"firmware\"}");
    const String sufx = F("/api/firmware/resolve-download");
    String         baseN = tkwmNormHost_(base);
    if (baseN.indexOf("://") < 0) {
        err = "bad host";
        return false;
    }
    int    code = 0;
    String r, url;
    for (int att = 0; att < 2; att++) {
        if (att == 1) {
            if (!baseN.startsWith("http://")) break;
            baseN = String("https://") + baseN.substring(7);
        }
        url = baseN + sufx;
        {
            HTTPClient http;
            http.setConnectTimeout(15000);
            http.setTimeout(30000);
            if (url.startsWith("https://")) {
                WiFiClientSecure cl;
#if TKWM_OTA_INSECURE
                cl.setInsecure();
#endif
                if (!http.begin(cl, url)) {
                    err = "http begin failed";
                    return false;
                }
            } else {
                WiFiClient cl;
                if (!http.begin(cl, url)) {
                    err = "http begin failed";
                    return false;
                }
            }
            http.addHeader(F("Authorization"), String(F("Bearer ")) + token);
            http.addHeader(F("Content-Type"), F("application/json"));
            code = http.POST(post);
            r    = http.getString();
            http.end();
        }
        if (code < 0) {
            err = "HTTP error " + String(code);
            return false;
        }
        if (code >= 200 && code < 300) {
            dl = "";
            if (!tkwmJsonGetString(r, "download_url", dl) || dl.isEmpty()) tkwmJsonGetString(r, "downloadUrl", dl);
            if (dl.isEmpty()) {
                err = "no download_url in response";
                if (r.length() && r.length() < 256) {
                    err += ": ";
                    err += (r.length() > 200) ? r.substring(0, 200) : r;
                }
                return false;
            }
            fw = "";
            if (!tkwmJsonGetString(r, "firmware_version", fw) || !fw.length()) tkwmJsonGetString(r, "firmwareVersion", fw);
            latest = "";
            if (!tkwmJsonGetString(r, "latest_firmware_version", latest) || !latest.length())
                tkwmJsonGetString(r, "latestFirmwareVersion", latest);
            return true;
        }
        err = "server HTTP " + String(code);
        if (r.length() && r.length() < 512) err += ": " + r;
        {
            String d;
            if (tkwmJsonGetString(r, "detail", d) && d.length()) err = d;
            if (tkwmJsonGetString(r, "message", d) && d.length()) err = d;
        }
        if (att == 0 && baseN.startsWith("http://") && tkwmErrSuggestsHttps_(r, err)) continue;
        return false;
    }
    return false;
}

void TKWifiManager::handleOtaCheck() {
    const String body = tkwmWebServerPostBody_(_server);
    if (!body.length()) {
        _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"no body\"}");
        return;
    }
    if (!_otaConfLoaded) loadOtaConf_();
    String         hostI, tokenI, skipI;
    tkwmJsonGetString(body, "host", hostI);
    tkwmJsonGetString(body, "token", tokenI);
    tkwmJsonGetString(body, "skipVersion", skipI);
    String h = tkwmNormHost_(hostI);
    if (h.isEmpty()) h = tkwmNormHost_(_otaFileHost);
    String tk = tokenI;
    if (tk.isEmpty()) tk = _otaFileToken;
    if (h.isEmpty() || tk.isEmpty()) {
        _server.send(200, "application/json", "{\"ok\":false,\"msg\":\"host and token required\"}");
        return;
    }
    String          ctrl   = tkwmOtaController_();
    String          fw, dl, latest, e;
    if (!tkwmEsptoolsResolve_(h, tk, ctrl, fw, dl, latest, e)) {
        String o = F("{\"ok\":false,\"msg\":\"");
        tkwmAppJsonVal_(o, e);
        o += F("\"}");
        _server.send(200, "application/json", o);
        return;
    }
    if (!fw.length() && !latest.length()) {
        _server.send(200, "application/json", "{\"ok\":true,\"updateAvailable\":false,\"msg\":\"no version in response\"}");
        return;
    }
    const String cur    = F(TKWM_FW_VERSION);
    String       remoteV = fw.length() ? fw : latest;
    const bool   skipB   = (skipI.length() > 0) && (skipI == remoteV);
    const bool   upd     = (remoteV != cur) && !skipB;
    String       out     = F("{\"ok\":true,\"updateAvailable\":");
    out += upd ? "true" : "false";
    out += F(",\"currentVersion\":\"");
    tkwmAppJsonVal_(out, cur);
    out += F("\",\"remoteVersion\":\"");
    tkwmAppJsonVal_(out, remoteV);
    out += F("\",\"controller\":\"");
    tkwmAppJsonVal_(out, ctrl);
    (void)dl;
    out += F("\"}");
    _server.send(200, "application/json", out);
}

static bool tkwmEsptoolsDownloadOta_(const String& base, const String& token, const String& controller, String& err) {
    if (WiFi.status() != WL_CONNECTED) {
        err = "no internet (Wi-Fi not connected)";
        return false;
    }
    String fw, dl, latest, e2;
    if (!tkwmEsptoolsResolve_(base, token, controller, fw, dl, latest, e2)) {
        err = e2;
        return false;
    }
    String tryUrl = tkwmNormHost_(base) + dl;
    if (!tryUrl.startsWith("http")) {
        err = "bad download URL";
        return false;
    }
    for (int att = 0; att < 2; att++) {
        if (att == 1) {
            if (!tryUrl.startsWith("http://")) break;
            tryUrl = String("https://") + tryUrl.substring(7);
        }
        {
            HTTPClient      http;
            http.setConnectTimeout(15000);
            http.setTimeout(60000);
            int  code   = 0;
            int  len    = 0;
            String rbody;
            if (tryUrl.startsWith("https://")) {
                WiFiClientSecure cl;
#if TKWM_OTA_INSECURE
                cl.setInsecure();
#endif
                if (!http.begin(cl, tryUrl)) {
                    err = "http begin (bin) failed";
                    return false;
                }
            } else {
                WiFiClient cl;
                if (!http.begin(cl, tryUrl)) {
                    err = "http begin (bin) failed";
                    return false;
                }
            }
            http.addHeader(F("Authorization"), String(F("Bearer ")) + token);
            code  = http.GET();
            len   = (int)http.getSize();
            rbody = (code == 200) ? String() : http.getString();
            if (code != 200) {
                http.end();
                err = "GET " + String(code) + " " + rbody;
                if (att == 0 && tryUrl.startsWith("http://") && tkwmErrSuggestsHttps_(rbody, err)) continue;
                return false;
            }
            if (!Update.begin((len > 0) ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
                err = String("Update.begin: ") + Update.errorString();
                http.end();
                return false;
            }
            WiFiClient* stream = http.getStreamPtr();
            if (!stream) {
                err = "no stream";
                Update.abort();
                http.end();
                return false;
            }
            size_t    written = 0;
            uint8_t  buf[1024];
            if (len > 0) {
                size_t n = (size_t)len;
                while (written < n) {
                    size_t want = n - written;
                    if (want > sizeof(buf)) want = sizeof(buf);
                    int r = stream->readBytes((char*)buf, want);
                    if (r <= 0) {
                        delay(2);
                        continue;
                    }
                    if (Update.write(buf, (size_t)r) != (size_t)r) {
                        err = String("write: ") + Update.errorString();
                        Update.abort();
                        http.end();
                        return false;
                    }
                    written += (size_t)r;
                }
            } else {
                while (http.connected() || stream->available()) {
                    size_t av = stream->available();
                    if (!av) { delay(1); continue; }
                    if (av > sizeof(buf)) av = sizeof(buf);
                    int r = stream->readBytes((char*)buf, av);
                    if (r <= 0) continue;
                    if (Update.write(buf, (size_t)r) != (size_t)r) {
                        err = String("write: ") + Update.errorString();
                        Update.abort();
                        http.end();
                        return false;
                    }
                }
            }
            http.end();
        }
        break; // success
    }
    if (!Update.end(true)) {
        err = String("Update.end: ") + Update.errorString();
        return false;
    }
    return true;
}

void TKWifiManager::handleOtaInstall() {
    const String body = tkwmWebServerPostBody_(_server);
    if (!body.length()) {
        _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"no body\"}");
        return;
    }
    if (!_otaConfLoaded) loadOtaConf_();
    String         hostI, tokenI;
    tkwmJsonGetString(body, "host", hostI);
    tkwmJsonGetString(body, "token", tokenI);
    String h  = tkwmNormHost_(hostI);
    if (h.isEmpty()) h = tkwmNormHost_(_otaFileHost);
    String tk = tokenI;
    if (tk.isEmpty()) tk = _otaFileToken;
    if (h.isEmpty() || tk.isEmpty()) {
        _server.send(200, "application/json", "{\"ok\":false,\"msg\":\"host and token required\"}");
        return;
    }
    String   errS;
    String   ctrl = tkwmOtaController_();
    if (!tkwmEsptoolsDownloadOta_(h, tk, ctrl, errS)) {
        String o = F("{\"ok\":false,\"msg\":\"");
        tkwmAppJsonVal_(o, errS);
        o += F("\"}");
        _server.send(200, "application/json", o);
        return;
    }
    _server.send(200, "application/json", F("{\"ok\":true,\"msg\":\"reboot\"}"));
    _otaRestartPending = true;
    _otaRestartAt      = millis() + 500;
}

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

// =================== /api/wifi/scan (REST polling) =====
void TKWifiManager::handleWifiScan() {
    ensureWifiForScan_();
    int n = WiFi.scanNetworks(false, true);
    bool connected = (WiFi.status() == WL_CONNECTED);
    String out;
    out.reserve(64 * max(n, 1) + 64);
    out += F("{\"connected\":");
    out += connected ? F("true") : F("false");
    out += F(",\"ip\":\"");
    out += connected ? WiFi.localIP().toString() : String("0.0.0.0");
    out += F("\",\"nets\":[");
    for (int i = 0; i < n; ++i) {
        if (i) out += ',';
        out += F("{\"ssid\":\"");
        String s = WiFi.SSID(i);
        for (size_t k = 0; k < s.length(); k++) {
            char c = s[k];
            if (c == '\"' || c == '\\') { out += '\\'; out += c; }
            else if ((uint8_t)c < 0x20) { char esc[7]; snprintf(esc, sizeof(esc), "\\u%04X", (unsigned char)c); out += esc; }
            else out += c;
        }
        out += F("\",\"rssi\":"); out += String(WiFi.RSSI(i));
        out += F(",\"ch\":");     out += String(WiFi.channel(i));
        out += F(",\"enc\":");    out += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1);
        out += '}';
    }
    out += "]}";
    _server.send(200, "application/json", out);
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
    wifi_country_t ctry = {};
    strncpy((char*)ctry.cc, TKWM_WIFI_COUNTRY, sizeof(ctry.cc));
    ctry.cc[sizeof(ctry.cc) - 1] = '\0';
    ctry.schan        = 1;
    ctry.nchan        = 13;
    ctry.max_tx_power = 20;
    ctry.policy       = WIFI_COUNTRY_POLICY_MANUAL;
    esp_wifi_set_country(&ctry);
    // выключим power save — скану так легче
    esp_wifi_set_ps(WIFI_PS_NONE);

    // убедимся, что стек реально запущен
    esp_wifi_start();
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
