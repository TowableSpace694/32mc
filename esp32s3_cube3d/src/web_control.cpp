#include "web_control.h"

#include "controls.h"
#include "mc_client.h"

namespace game {

namespace {

void handleRoot() {
  static const char kHtml[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>ESP32 Voxel Controller</title>
  <style>
    body { margin:0; background:#10161c; color:#e8f0f8; font-family:Consolas,monospace; }
    .wrap { max-width:920px; margin:0 auto; padding:14px; }
    .card { background:#15202b; border:1px solid #2b3f55; border-radius:10px; padding:12px; margin-bottom:12px; }
    h2 { margin:0 0 8px 0; font-size:18px; }
    .row { display:grid; grid-template-columns:170px 1fr auto; gap:8px; margin:6px 0; }
    input { background:#0f1720; color:#d8e5f2; border:1px solid #35506c; border-radius:8px; padding:8px; width:100%; box-sizing:border-box; }
    button { background:#2b7dd3; border:1px solid #4a9aec; color:#fff; border-radius:8px; padding:8px 12px; cursor:pointer; }
    .small { color:#97b4d1; font-size:12px; line-height:1.45; }
    #log { white-space:pre-wrap; background:#0e161d; border:1px solid #2b3f55; border-radius:8px; padding:8px; min-height:90px; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <h2>ESP32 Voxel Controller</h2>
      <div id="meta" class="small">loading...</div>
      <div class="small">Default keys: Arrows look, WASD move, Space jump, Z/X or 1..5 switch hotbar. Local break/place is disabled in online-only mode.</div>
    </div>

    <div class="card">
      <h2>Minecraft Link</h2>
      <div class="row"><span>mc_host</span><input id="mc_host"><span></span></div>
      <div class="row"><span>mc_port</span><input id="mc_port"><span></span></div>
      <div class="row"><span>mc_name</span><input id="mc_name"><span></span></div>
      <div class="row"><span>mc_auto</span><input id="mc_auto"><span class="small">1 auto / 0 disable</span></div>
      <button onclick="saveMcCfg()">Save MC Config</button>
      <button onclick="reconnectMc()">Reconnect MC</button>
    </div>

    <div class="card">
      <h2>Key Mapping</h2>
      <div class="row"><span>turn_left</span><input id="turn_left"><button onclick="focusKey('turn_left')">Capture</button></div>
      <div class="row"><span>turn_right</span><input id="turn_right"><button onclick="focusKey('turn_right')">Capture</button></div>
      <div class="row"><span>look_up</span><input id="look_up"><button onclick="focusKey('look_up')">Capture</button></div>
      <div class="row"><span>look_down</span><input id="look_down"><button onclick="focusKey('look_down')">Capture</button></div>
      <div class="row"><span>move_fwd</span><input id="move_fwd"><button onclick="focusKey('move_fwd')">Capture</button></div>
      <div class="row"><span>move_back</span><input id="move_back"><button onclick="focusKey('move_back')">Capture</button></div>
      <div class="row"><span>strafe_left</span><input id="strafe_left"><button onclick="focusKey('strafe_left')">Capture</button></div>
      <div class="row"><span>strafe_right</span><input id="strafe_right"><button onclick="focusKey('strafe_right')">Capture</button></div>
      <div class="row"><span>jump</span><input id="jump"><button onclick="focusKey('jump')">Capture</button></div>
      <div class="row"><span>break_block</span><input id="break_block"><button onclick="focusKey('break_block')">Capture</button></div>
      <div class="row"><span>place_block</span><input id="place_block"><button onclick="focusKey('place_block')">Capture</button></div>
      <div class="row"><span>inv_prev</span><input id="inv_prev"><button onclick="focusKey('inv_prev')">Capture</button></div>
      <div class="row"><span>inv_next</span><input id="inv_next"><button onclick="focusKey('inv_next')">Capture</button></div>
      <div class="row"><span>slot_1</span><input id="slot_1"><button onclick="focusKey('slot_1')">Capture</button></div>
      <div class="row"><span>slot_2</span><input id="slot_2"><button onclick="focusKey('slot_2')">Capture</button></div>
      <div class="row"><span>slot_3</span><input id="slot_3"><button onclick="focusKey('slot_3')">Capture</button></div>
      <div class="row"><span>slot_4</span><input id="slot_4"><button onclick="focusKey('slot_4')">Capture</button></div>
      <div class="row"><span>slot_5</span><input id="slot_5"><button onclick="focusKey('slot_5')">Capture</button></div>
      <button onclick="saveMap()">Save Key Mapping</button>
    </div>

    <div class="card">
      <h2>Live Keyboard</h2>
      <div class="small">Keep this page focused while playing. On blur, all pressed keys are released automatically.</div>
      <div id="log"></div>
    </div>
  </div>
<script>
const actions = ['turn_left','turn_right','look_up','look_down','move_fwd','move_back','strafe_left','strafe_right','jump','break_block','place_block','inv_prev','inv_next','slot_1','slot_2','slot_3','slot_4','slot_5'];
let pressed = new Set();
let captureTarget = '';
const blockMap = {0:'__',1:'GR',2:'DR',3:'ST',4:'WD',5:'SA',6:'OR',7:'BD'};

function log(s){
  const el = document.getElementById('log');
  el.textContent = (s + "\n" + el.textContent).slice(0, 3200);
}

async function api(url){
  const r = await fetch(url, {cache:'no-store'});
  return await r.json();
}

async function loadState(){
  const s = await api('/api/state');
  const invSel = (s.inv_selected ?? 0) + 1;
  const inv = s.inv || [];
  const ids = s.inv_block || [];
  const invSlots = inv.map((count, i) => `${blockMap[ids[i] ?? 0] || '__'}:${count}`).join(' ');
  const selectedName = blockMap[ids[Math.max(0, invSel - 1)] ?? 0] || '__';
  document.getElementById('meta').textContent = `ip=${s.ip} wifi=${s.wifi} mc=${s.mc_state} ${s.mc_host}:${s.mc_port} as ${s.mc_name} fps=${s.fps} yaw=${s.yaw.toFixed(2)} pitch=${s.pitch.toFixed(2)} y=${s.cam_y.toFixed(2)} sel=${invSel}:${selectedName} bag=[${invSlots}]`;

  for(const a of actions){
    const el = document.getElementById(a);
    if(el) el.value = s.map[a] || '';
  }

  if (document.activeElement !== document.getElementById('mc_host')) {
    document.getElementById('mc_host').value = s.mc_host || '';
  }
  if (document.activeElement !== document.getElementById('mc_port')) {
    document.getElementById('mc_port').value = String(s.mc_port ?? 25565);
  }
  if (document.activeElement !== document.getElementById('mc_name')) {
    document.getElementById('mc_name').value = s.mc_name || '';
  }
  if (document.activeElement !== document.getElementById('mc_auto')) {
    document.getElementById('mc_auto').value = s.mc_auto ? '1' : '0';
  }
}

function focusKey(action){
  captureTarget = action;
  document.getElementById(action).focus();
  log(`capture ${action}: press a key...`);
}

async function saveMap(){
  for(const a of actions){
    const key = encodeURIComponent(document.getElementById(a).value.trim());
    await api(`/api/map?action=${a}&key=${key}`);
  }
  log('mapping saved');
}

async function saveMcCfg(){
  const host = encodeURIComponent((document.getElementById('mc_host').value || '').trim());
  const port = encodeURIComponent((document.getElementById('mc_port').value || '').trim());
  const name = encodeURIComponent((document.getElementById('mc_name').value || '').trim());
  const autoRaw = (document.getElementById('mc_auto').value || '1').trim().toLowerCase();
  const auto = (autoRaw === '0' || autoRaw === 'false' || autoRaw === 'off') ? '0' : '1';
  await api(`/api/mc_cfg?host=${host}&port=${port}&name=${name}&auto=${auto}`);
  await api('/api/mc_reconnect');
  log('mc config saved, reconnecting...');
}

async function reconnectMc(){
  await api('/api/mc_reconnect');
  log('mc reconnect requested');
}

function onDown(code){
  if(pressed.has(code)) return;
  pressed.add(code);
  fetch(`/api/key?code=${encodeURIComponent(code)}&down=1`, {cache:'no-store'});
}

function onUp(code){
  if(!pressed.has(code)) return;
  pressed.delete(code);
  fetch(`/api/key?code=${encodeURIComponent(code)}&down=0`, {cache:'no-store'});
}

window.addEventListener('keydown', (e)=>{
  if(captureTarget){
    document.getElementById(captureTarget).value = e.code;
    log(`${captureTarget} -> ${e.code}`);
    captureTarget = '';
    e.preventDefault();
    return;
  }
  onDown(e.code);
  e.preventDefault();
});

window.addEventListener('keyup', (e)=>{
  onUp(e.code);
  e.preventDefault();
});

window.addEventListener('blur', ()=>{
  pressed.clear();
  fetch('/api/release_all', {cache:'no-store'});
});

setInterval(()=>{ fetch('/api/ping', {cache:'no-store'}); }, 700);
setInterval(loadState, 1000);
loadState();
</script>
</body>
</html>
)HTML";

  server.send(200, "text/html; charset=utf-8", kHtml);
}

void handleState() {
  String out;
  out.reserve(900);
  int remoteCount = 0;
  for (int i = 0; i < kRemotePlayerMax; ++i) {
    if (s_remotePlayers[i].active) {
      remoteCount++;
    }
  }
  out += "{";
  out += "\"ok\":true,";
  out += "\"ip\":\"";
  out += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("0.0.0.0");
  out += "\",";
  out += "\"wifi\":\"";
  out += wifiStateName();
  out += "\",";
  out += "\"fps\":";
  out += String(s_fps);
  out += ",";
  out += "\"yaw\":";
  out += String(s_yaw, 4);
  out += ",";
  out += "\"pitch\":";
  out += String(s_pitch, 4);
  out += ",";
  out += "\"cam_x\":";
  out += String(s_camX, 3);
  out += ",";
  out += "\"cam_y\":";
  out += String(s_camY, 3);
  out += ",";
  out += "\"cam_z\":";
  out += String(s_camZ, 3);
  out += ",";
  out += "\"inv_selected\":";
  out += String(s_selectedSlot);
  out += ",";
  out += "\"inv\":[";
  for (int i = 0; i < kInvSlots; ++i) {
    if (i) {
      out += ",";
    }
    out += String(s_hotbar[i].count);
  }
  out += "],";
  out += "\"inv_block\":[";
  for (int i = 0; i < kInvSlots; ++i) {
    if (i) {
      out += ",";
    }
    out += String(s_hotbar[i].blockId);
  }
  out += "],";
  out += "\"mc_state\":\"";
  out += s_mcState;
  out += "\",";
  out += "\"mc_ready\":";
  out += mcReadyForGameplay() ? "true" : "false";
  out += ",";
  out += "\"mc_remote\":";
  out += String(remoteCount);
  out += ",";
  out += "\"mc_host\":\"";
  out += s_mcHost;
  out += "\",";
  out += "\"mc_port\":";
  out += String(s_mcPort);
  out += ",";
  out += "\"mc_name\":\"";
  out += s_mcPlayerName;
  out += "\",";
  out += "\"mc_auto\":";
  out += s_mcAutoConnect ? "true" : "false";
  out += ",";
  out += "\"dbg_move_fwd\":";
  out += actionDown("move_fwd") ? "true" : "false";
  out += ",";
  out += "\"dbg_any_input\":";
  out += anyActionActive() ? "true" : "false";
  out += ",";
  out += "\"map\":{";
  for (size_t i = 0; i < kBindingCount; ++i) {
    if (i) {
      out += ",";
    }
    out += "\"";
    out += s_bindings[i].action;
    out += "\":\"";
    out += s_bindings[i].keyCode;
    out += "\"";
  }
  out += "}}";
  server.send(200, "application/json", out);
}

bool parseBoolArg(const String &raw, bool defaultValue) {
  if (raw.length() == 0) {
    return defaultValue;
  }
  if (raw == "1" || raw == "true" || raw == "TRUE" || raw == "on" || raw == "ON") {
    return true;
  }
  if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
    return false;
  }
  return defaultValue;
}

void handleMcCfg() {
  String host = server.arg("host");
  String name = server.arg("name");
  host.trim();
  name.trim();

  uint16_t port = kMcDefaultPort;
  const String portArg = server.arg("port");
  if (portArg.length() > 0) {
    const long p = portArg.toInt();
    if (p <= 0 || p > 65535) {
      server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad_port\"}");
      return;
    }
    port = static_cast<uint16_t>(p);
  } else {
    port = s_mcPort;
  }

  const bool autoConnect = parseBoolArg(server.arg("auto"), s_mcAutoConnect);
  if (name.length() == 0) {
    name = s_mcPlayerName;
  }
  if (host.length() == 0) {
    host = s_mcHost;
  }

  mcSetConfig(host, port, name, autoConnect);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleMcReconnect() {
  mcForceReconnect();
  server.send(200, "application/json", "{\"ok\":true}");
}

KeyBinding *findBindingByAction(const String &action) {
  for (size_t i = 0; i < kBindingCount; ++i) {
    if (action == s_bindings[i].action) {
      return &s_bindings[i];
    }
  }
  return nullptr;
}

void handleMap() {
  const String action = server.arg("action");
  String key = server.arg("key");
  key.trim();
  KeyBinding *b = findBindingByAction(action);
  if (b == nullptr || key.length() == 0 || key.length() > 24) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  b->keyCode = key;
  b->active = false;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleKey() {
  const String code = server.arg("code");
  const bool down = server.arg("down") == "1";
  if (code.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  setActionByKey(code, down);
  s_lastInputMs = millis();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePing() {
  s_lastInputMs = millis();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleReleaseAll() {
  clearAllActions();
  s_lastInputMs = millis();
  server.send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

String wifiStateName() {
  switch (WiFi.status()) {
    case WL_CONNECTED:
      return "CONNECTED";
    case WL_IDLE_STATUS:
      return "IDLE";
    case WL_DISCONNECTED:
      return "DISCONNECTED";
    case WL_CONNECTION_LOST:
      return "LOST";
    case WL_CONNECT_FAILED:
      return "FAILED";
    case WL_NO_SSID_AVAIL:
      return "NO_SSID";
    default:
      return "UNKNOWN";
  }
}

void wifiConnectNow() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(kWifiSsid, kWifiPass);
  s_lastWifiAttemptMs = millis();
}

void setupWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/map", HTTP_GET, handleMap);
  server.on("/api/key", HTTP_GET, handleKey);
  server.on("/api/ping", HTTP_GET, handlePing);
  server.on("/api/release_all", HTTP_GET, handleReleaseAll);
  server.on("/api/mc_cfg", HTTP_GET, handleMcCfg);
  server.on("/api/mc_reconnect", HTTP_GET, handleMcReconnect);
  server.begin();
}

void updateWifi() {
  const wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    return;
  }
  if (millis() - s_lastWifiAttemptMs >= kWifiRetryMs) {
    wifiConnectNow();
  }
}

}  // namespace game
