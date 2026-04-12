#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <Wire.h>
#include <LittleFS.h>

// ─── CONFIGURATION ────────────────────────────────────────────────────────────
const char* AP_SSID      = "SmartBell-Setup";
const char* AP_PASS      = "bellsystem";
const char* MDNS_HOST    = "smartbell";
const uint8_t RELAY_PIN  = 14;
const uint8_t LED_PIN    =  2;
const char* SCHEDULE_FILE = "/sch.json";
const char* SETTINGS_FILE = "/cfg.json";

// ─── GLOBALS ──────────────────────────────────────────────────────────────────
RTC_DS3231       rtc;
ESP8266WebServer server(80);

struct Settings {
  char ssid[64]     = "";
  char wifiPass[64] = "";
  char user[32]     = "admin";
  char pass[32]     = "admin123";
  int  timezone     = 5;
  int  tzMinutes    = 0;
} cfg;

struct BellEntry {
  uint8_t  dayMask;
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  strokes;
  uint16_t strokeDur;
  uint16_t strokeGap;
  char     label[32];
  bool     enabled;
};

const int MAX_BELLS = 64;
BellEntry bells[MAX_BELLS];
int       bellCount       = 0;
bool      wifiConnected   = false;
unsigned long lastBellCheck   = 0;
int           lastMinuteFired = -1;

// ─── EMBEDDED DASHBOARD HTML (PROGMEM) ────────────────────────────────────────
// Stored in flash, not RAM. ~28KB of flash used.
const char DASHBOARD_HTML[] PROGMEM = R"====(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1"/>
<title>Smart Bell System</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0b0f1a;--sur:#111827;--sur2:#1a2235;--bdr:#1e2d45;--bdr2:#263652;
  --acc:#3b82f6;--acc2:#2563eb;--aglow:rgba(59,130,246,.22);
  --grn:#10b981;--gglow:rgba(16,185,129,.18);
  --red:#ef4444;--rglow:rgba(239,68,68,.18);
  --amb:#f59e0b;
  --txt:#e2e8f0;--txt2:#94a3b8;--txt3:#64748b;
  --mono:'Courier New',monospace;
  --r:10px;--r2:7px;--shd:0 4px 20px rgba(0,0,0,.4);
}
html{scroll-behavior:smooth}
body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--txt);min-height:100vh;font-size:14px;overflow-x:hidden}
body::before{content:'';position:fixed;inset:0;background:radial-gradient(ellipse 600px 400px at 80% 5%,rgba(59,130,246,.05) 0%,transparent 70%);pointer-events:none;z-index:0}

/* LAYOUT */
.app{display:flex;min-height:100vh;position:relative;z-index:1}
.sidebar{width:220px;min-height:100vh;background:var(--sur);border-right:1px solid var(--bdr);display:flex;flex-direction:column;position:fixed;left:0;top:0;bottom:0;z-index:100;transition:transform .3s}
.brand{padding:20px 16px 16px;border-bottom:1px solid var(--bdr)}
.brand-icon{width:38px;height:38px;background:linear-gradient(135deg,var(--acc),var(--grn));border-radius:9px;display:flex;align-items:center;justify-content:center;font-size:20px;margin-bottom:8px;box-shadow:0 0 18px var(--aglow)}
.brand-name{font-size:14px;font-weight:700}
.brand-sub{font-size:10px;color:var(--txt3);font-family:var(--mono);margin-top:2px}
.snav{flex:1;padding:12px 10px;display:flex;flex-direction:column;gap:3px}
.ni{display:flex;align-items:center;gap:9px;padding:9px 11px;border-radius:var(--r2);cursor:pointer;transition:all .2s;color:var(--txt2);font-size:13px;font-weight:500;border:1px solid transparent}
.ni:hover{background:var(--sur2);color:var(--txt);border-color:var(--bdr)}
.ni.active{background:rgba(59,130,246,.1);color:var(--acc);border-color:rgba(59,130,246,.2)}
.ni-ic{font-size:15px;width:18px;text-align:center}
.sbot{padding:12px 10px;border-top:1px solid var(--bdr)}
.sclock{text-align:center;padding:10px;background:var(--sur2);border-radius:var(--r2);border:1px solid var(--bdr)}
.ctime{font-family:var(--mono);font-size:20px;font-weight:bold;color:var(--acc);letter-spacing:2px}
.cdate{font-size:10px;color:var(--txt3);margin-top:3px}
.cday{font-size:11px;color:var(--txt2);margin-top:2px;font-weight:600}

/* MAIN */
.main{margin-left:220px;flex:1;min-height:100vh;display:flex;flex-direction:column}
.topbar{height:56px;padding:0 24px;background:rgba(17,24,39,.85);backdrop-filter:blur(10px);border-bottom:1px solid var(--bdr);display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:90}
.tb-title{font-size:15px;font-weight:600}
.tb-right{display:flex;align-items:center;gap:10px}
.schip{display:flex;align-items:center;gap:5px;padding:4px 10px;border-radius:20px;font-size:11px;font-weight:600}
.schip.on{background:var(--gglow);color:var(--grn);border:1px solid rgba(16,185,129,.3)}
.schip.off{background:var(--rglow);color:var(--red);border:1px solid rgba(239,68,68,.3)}
.sdot{width:5px;height:5px;border-radius:50%;background:currentColor;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
.btn-lo{padding:5px 12px;border-radius:var(--r2);background:transparent;border:1px solid var(--bdr2);color:var(--txt2);font-size:11px;cursor:pointer;transition:all .2s;font-family:inherit}
.btn-lo:hover{background:var(--rglow);color:var(--red);border-color:rgba(239,68,68,.3)}
.content{padding:24px;flex:1;max-width:1100px}

/* CARDS */
.card{background:var(--sur);border:1px solid var(--bdr);border-radius:var(--r);padding:20px;box-shadow:var(--shd)}
.card-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px;padding-bottom:14px;border-bottom:1px solid var(--bdr)}
.card-ttl{font-size:14px;font-weight:600;display:flex;align-items:center;gap:7px}

/* STATS */
.sgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:14px;margin-bottom:24px}
.scard{background:var(--sur);border:1px solid var(--bdr);border-radius:var(--r);padding:18px 16px 14px;position:relative;overflow:hidden}
.scard::before{content:'';position:absolute;top:0;left:0;right:0;height:2px}
.scard.bl::before{background:linear-gradient(90deg,var(--acc),transparent)}
.scard.gr::before{background:linear-gradient(90deg,var(--grn),transparent)}
.scard.am::before{background:linear-gradient(90deg,var(--amb),transparent)}
.scard.re::before{background:linear-gradient(90deg,var(--red),transparent)}
.sv{font-family:var(--mono);font-size:26px;font-weight:bold;color:var(--txt);margin-bottom:3px}
.sl{font-size:11px;color:var(--txt3)}
.si{position:absolute;right:14px;top:14px;font-size:22px;opacity:.18}

/* SECTIONS */
.sec{display:none}.sec.active{display:block}

/* BUTTONS */
.btn{display:inline-flex;align-items:center;gap:5px;padding:8px 16px;border-radius:var(--r2);font-family:inherit;font-size:13px;font-weight:500;cursor:pointer;transition:all .2s;border:1px solid transparent}
.btn-p{background:var(--acc);color:#fff}.btn-p:hover{background:var(--acc2);box-shadow:0 0 18px var(--aglow)}
.btn-g{background:var(--grn);color:#fff}.btn-g:hover{background:#059669}
.btn-d{background:transparent;color:var(--red);border-color:rgba(239,68,68,.3)}.btn-d:hover{background:var(--rglow)}
.btn-gh{background:transparent;color:var(--txt2);border-color:var(--bdr2)}.btn-gh:hover{background:var(--sur2);color:var(--txt)}
.btn-a{background:var(--amb);color:#000}.btn-a:hover{background:#d97706}
.btn-sm{padding:5px 10px;font-size:11px}

/* TABLE */
.sctrl{display:flex;align-items:center;gap:10px;margin-bottom:16px;flex-wrap:wrap}
.dpills{display:flex;gap:5px;flex-wrap:wrap}
.dpill{padding:4px 11px;border-radius:20px;font-size:11px;font-weight:700;cursor:pointer;border:1px solid var(--bdr2);color:var(--txt2);transition:all .2s;font-family:var(--mono)}
.dpill.active{background:var(--acc);color:#fff;border-color:var(--acc)}
.tw{overflow-x:auto}
table{width:100%;border-collapse:collapse;font-size:13px}
thead th{padding:9px 12px;text-align:left;font-size:10px;font-weight:700;letter-spacing:.08em;color:var(--txt3);text-transform:uppercase;border-bottom:1px solid var(--bdr);background:var(--sur2)}
tbody tr{border-bottom:1px solid var(--bdr);transition:background .15s}
tbody tr:hover{background:var(--sur2)}
tbody td{padding:11px 12px;vertical-align:middle}
.tbadge{font-family:var(--mono);font-size:14px;font-weight:bold;color:var(--acc)}
.dbadges{display:flex;gap:2px;flex-wrap:wrap}
.db{padding:2px 6px;border-radius:3px;font-size:9px;font-weight:700;font-family:var(--mono)}
.db.on{background:rgba(59,130,246,.15);color:var(--acc)}
.db.off{background:var(--sur2);color:var(--txt3)}
.patt{display:inline-flex;align-items:center;gap:3px;padding:3px 9px;border-radius:20px;font-size:11px;background:var(--sur2);color:var(--txt2);border:1px solid var(--bdr);font-family:var(--mono)}
.tog{position:relative;display:inline-block;width:34px;height:18px}
.tog input{opacity:0;width:0;height:0}
.tslide{position:absolute;inset:0;border-radius:18px;background:var(--bdr2);cursor:pointer;transition:.3s}
.tslide::before{content:'';position:absolute;width:12px;height:12px;left:3px;top:3px;border-radius:50%;background:#fff;transition:.3s}
input:checked+.tslide{background:var(--grn)}
input:checked+.tslide::before{transform:translateX(16px)}
.abts{display:flex;gap:5px}
.empty{text-align:center;padding:40px 20px;color:var(--txt3)}
.empty-ic{font-size:40px;margin-bottom:10px;opacity:.3}

/* MODAL */
.overlay{position:fixed;inset:0;background:rgba(0,0,0,.7);backdrop-filter:blur(4px);z-index:200;display:flex;align-items:center;justify-content:center;padding:16px;opacity:0;pointer-events:none;transition:.2s}
.overlay.open{opacity:1;pointer-events:all}
.modal{background:var(--sur);border:1px solid var(--bdr2);border-radius:14px;padding:24px;width:100%;max-width:500px;transform:translateY(14px);transition:.2s;max-height:90vh;overflow-y:auto}
.overlay.open .modal{transform:none}
.mttl{font-size:15px;font-weight:700;margin-bottom:18px;display:flex;align-items:center;gap:7px}
.fgrid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.fg{display:flex;flex-direction:column;gap:5px}
.fg.full{grid-column:1/-1}
.flbl{font-size:11px;font-weight:700;color:var(--txt2);letter-spacing:.05em;text-transform:uppercase}
.finp{padding:9px 11px;background:var(--sur2);border:1px solid var(--bdr2);border-radius:var(--r2);color:var(--txt);font-family:inherit;font-size:13px;outline:none;transition:border .2s;width:100%}
.finp:focus{border-color:var(--acc);box-shadow:0 0 0 3px var(--aglow)}
.finp option{background:var(--sur2)}
.dcbs{display:flex;flex-wrap:wrap;gap:7px;margin-top:3px}
.dcbl{display:flex;align-items:center;gap:4px;cursor:pointer;padding:5px 10px;border-radius:var(--r2);border:1px solid var(--bdr2);font-size:11px;font-weight:700;color:var(--txt2);transition:all .2s;font-family:var(--mono)}
.dcbl:has(input:checked){background:rgba(59,130,246,.15);color:var(--acc);border-color:rgba(59,130,246,.3)}
.dcbl input{display:none}
.macts{display:flex;gap:8px;margin-top:20px;justify-content:flex-end}

/* TIMETABLE */
.tgrid{display:grid;grid-template-columns:repeat(6,1fr);gap:10px;margin-top:6px}
.dcol{background:var(--sur2);border:1px solid var(--bdr);border-radius:var(--r2);overflow:hidden}
.dch{padding:8px;text-align:center;font-size:10px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;background:rgba(59,130,246,.07);border-bottom:1px solid var(--bdr);color:var(--acc)}
.dcb{padding:6px;display:flex;flex-direction:column;gap:3px;min-height:70px}
.te{padding:5px 7px;border-radius:5px;font-size:10px;background:rgba(59,130,246,.1);border:1px solid rgba(59,130,246,.15)}
.te.dis{background:var(--sur);border-color:var(--bdr);opacity:.45}
.teti{font-family:var(--mono);font-weight:bold;color:var(--acc);font-size:10px}
.tela{color:var(--txt2);font-size:9px;margin-top:1px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}

/* TEST BELL */
.tpanel{background:var(--sur2);border:1px solid var(--bdr);border-radius:var(--r);padding:18px;margin-bottom:18px}
.trow{display:grid;grid-template-columns:1fr 1fr 1fr auto;gap:14px;align-items:end}
.bellani{display:flex;align-items:center;justify-content:center;font-size:30px}
.bellani.ring{animation:ringbell .3s ease-in-out infinite alternate}
@keyframes ringbell{from{transform:rotate(-15deg)}to{transform:rotate(15deg)}}
.rwrap{display:flex;align-items:center;gap:8px}
input[type=range]{flex:1;appearance:none;height:3px;background:var(--bdr2);border-radius:3px;outline:none}
input[type=range]::-webkit-slider-thumb{appearance:none;width:14px;height:14px;border-radius:50%;background:var(--acc);cursor:pointer}
.rval{font-family:var(--mono);font-size:12px;color:var(--acc);min-width:48px;text-align:right}

/* SETTINGS */
.sgrd{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:18px}
.irow{display:flex;justify-content:space-between;align-items:center;padding:9px 0;border-bottom:1px solid var(--bdr);font-size:13px}
.irow:last-child{border-bottom:none}
.ik{color:var(--txt2)}
.iv{color:var(--txt);font-family:var(--mono);font-size:11px}

/* LOGIN */
#lscr{position:fixed;inset:0;background:var(--bg);z-index:500;display:flex;align-items:center;justify-content:center}
.lcard{background:var(--sur);border:1px solid var(--bdr2);border-radius:18px;padding:36px;width:100%;max-width:360px;box-shadow:0 20px 60px rgba(0,0,0,.5)}
.llogo{text-align:center;margin-bottom:24px}
.llic{width:58px;height:58px;background:linear-gradient(135deg,var(--acc),var(--grn));border-radius:14px;margin:0 auto 10px;display:flex;align-items:center;justify-content:center;font-size:30px;box-shadow:0 0 28px var(--aglow)}
.lttl{font-size:20px;font-weight:700}
.lsub{font-size:12px;color:var(--txt3);margin-top:3px}
.lerr{background:var(--rglow);border:1px solid rgba(239,68,68,.3);color:var(--red);padding:9px 12px;border-radius:var(--r2);font-size:12px;margin-bottom:14px;display:none}

/* TOASTS */
.toasts{position:fixed;bottom:20px;right:20px;display:flex;flex-direction:column;gap:7px;z-index:400}
.toast{padding:10px 16px;border-radius:var(--r2);font-size:12px;border:1px solid;display:flex;align-items:center;gap:7px;animation:slidein .3s ease;max-width:280px;box-shadow:var(--shd)}
@keyframes slidein{from{transform:translateX(100%);opacity:0}to{transform:none;opacity:1}}
.toast.ok{background:var(--gglow);border-color:rgba(16,185,129,.3);color:var(--grn)}
.toast.er{background:var(--rglow);border-color:rgba(239,68,68,.3);color:var(--red)}
.toast.inf{background:var(--aglow);border-color:rgba(59,130,246,.3);color:var(--acc)}

/* HAMBURGER */
.hbg{display:none;flex-direction:column;gap:4px;cursor:pointer;padding:4px}
.hbg span{width:20px;height:2px;background:var(--txt2);border-radius:2px;transition:.3s}

/* RESPONSIVE */
@media(max-width:900px){.tgrid{grid-template-columns:repeat(3,1fr)}}
@media(max-width:768px){
  .sidebar{transform:translateX(-100%)}
  .sidebar.open{transform:none;box-shadow:4px 0 20px rgba(0,0,0,.5)}
  .main{margin-left:0}
  .hbg{display:flex}
  .fgrid{grid-template-columns:1fr}
  .trow{grid-template-columns:1fr 1fr}
  .sgrid{grid-template-columns:repeat(2,1fr)}
  .content{padding:16px}
  .tgrid{grid-template-columns:repeat(2,1fr)}
}
</style>
</head>
<body>

<!-- LOGIN -->
<div id="lscr">
  <div class="lcard">
    <div class="llogo">
      <div class="llic">🔔</div>
      <div class="lttl">Smart Bell</div>
      <div class="lsub">School Bell Management System</div>
    </div>
    <div class="lerr" id="lerr">Invalid username or password.</div>
    <div class="fg" style="margin-bottom:12px">
      <label class="flbl">Username</label>
      <input class="finp" id="lu" type="text" placeholder="admin" autocomplete="username"/>
    </div>
    <div class="fg" style="margin-bottom:18px">
      <label class="flbl">Password</label>
      <input class="finp" id="lp" type="password" placeholder="••••••••" autocomplete="current-password"/>
    </div>
    <button class="btn btn-p" style="width:100%;justify-content:center;padding:11px" onclick="doLogin()">🔐 Sign In</button>
    <p style="text-align:center;margin-top:12px;font-size:10px;color:var(--txt3)">Default: admin / admin123</p>
  </div>
</div>

<!-- APP -->
<div class="app" id="app" style="display:none">
  <aside class="sidebar" id="sidebar">
    <div class="brand">
      <div class="brand-icon">🔔</div>
      <div class="brand-name">Smart Bell</div>
      <div class="brand-sub">v3.0 — ESP8266</div>
    </div>
    <nav class="snav">
      <div class="ni active" onclick="go('dash')" data-s="dash"><span class="ni-ic">📊</span>Dashboard</div>
      <div class="ni" onclick="go('sched')" data-s="sched"><span class="ni-ic">📅</span>Schedules</div>
      <div class="ni" onclick="go('tt')" data-s="tt"><span class="ni-ic">🗓️</span>Timetable</div>
      <div class="ni" onclick="go('bell')" data-s="bell"><span class="ni-ic">🔔</span>Bell Control</div>
      <div class="ni" onclick="go('cfg')" data-s="cfg"><span class="ni-ic">⚙️</span>Settings</div>
    </nav>
    <div class="sbot">
      <div class="sclock">
        <div class="ctime" id="ct">--:--:--</div>
        <div class="cdate" id="cd">Loading...</div>
        <div class="cday" id="cdy"></div>
      </div>
    </div>
  </aside>

  <div class="main">
    <div class="topbar">
      <div style="display:flex;align-items:center;gap:10px">
        <div class="hbg" onclick="document.getElementById('sidebar').classList.toggle('open')">
          <span></span><span></span><span></span>
        </div>
        <div class="tb-title" id="ttl">Dashboard</div>
      </div>
      <div class="tb-right">
        <div class="schip off" id="wchip"><span class="sdot"></span><span id="wtxt">Connecting...</span></div>
        <button class="btn-lo" onclick="doLogout()">Sign Out</button>
      </div>
    </div>

    <div class="content">

      <!-- DASHBOARD -->
      <div class="sec active" id="sec-dash">
        <div class="sgrid">
          <div class="scard bl"><div class="sv" id="s1">0</div><div class="sl">Total Schedules</div><div class="si">📅</div></div>
          <div class="scard gr"><div class="sv" id="s2">0</div><div class="sl">Active Bells</div><div class="si">✅</div></div>
          <div class="scard am"><div class="sv" id="s3">0</div><div class="sl">Today's Bells</div><div class="si">🔔</div></div>
          <div class="scard re"><div class="sv" id="s4">—</div><div class="sl">Next Bell</div><div class="si">⏰</div></div>
        </div>
        <div class="card">
          <div class="card-hdr">
            <div class="card-ttl">⏰ Today's Schedule</div>
            <div style="font-size:11px;color:var(--txt3)" id="todaylbl">—</div>
          </div>
          <div id="todaylist"><div class="empty"><div class="empty-ic">📭</div><p>No bells today</p></div></div>
        </div>
      </div>

      <!-- SCHEDULES -->
      <div class="sec" id="sec-sched">
        <div class="sctrl">
          <button class="btn btn-p" onclick="openM()">＋ Add Bell</button>
          <button class="btn btn-gh" onclick="loadSch()">↻ Refresh</button>
          <div class="dpills" id="dpills">
            <div class="dpill active" data-d="all" onclick="fday('all')">All</div>
            <div class="dpill" data-d="0" onclick="fday(0)">Mon</div>
            <div class="dpill" data-d="1" onclick="fday(1)">Tue</div>
            <div class="dpill" data-d="2" onclick="fday(2)">Wed</div>
            <div class="dpill" data-d="3" onclick="fday(3)">Thu</div>
            <div class="dpill" data-d="4" onclick="fday(4)">Fri</div>
            <div class="dpill" data-d="5" onclick="fday(5)">Sat</div>
          </div>
        </div>
        <div class="card" style="padding:0;overflow:hidden">
          <div class="tw">
            <table>
              <thead><tr><th>Label</th><th>Time</th><th>Days</th><th>Pattern</th><th>On/Off</th><th>Actions</th></tr></thead>
              <tbody id="stbody"></tbody>
            </table>
          </div>
        </div>
      </div>

      <!-- TIMETABLE -->
      <div class="sec" id="sec-tt">
        <div class="card">
          <div class="card-hdr">
            <div class="card-ttl">🗓️ Weekly Timetable</div>
            <button class="btn btn-gh btn-sm" onclick="renderTT()">↻ Refresh</button>
          </div>
          <div class="tgrid" id="ttgrid"></div>
        </div>
      </div>

      <!-- BELL CONTROL -->
      <div class="sec" id="sec-bell">
        <div class="card" style="margin-bottom:18px">
          <div class="card-hdr">
            <div class="card-ttl">🔔 Test Bell</div>
            <div class="bellani" id="bani">🔔</div>
          </div>
          <div class="tpanel">
            <div class="trow">
              <div class="fg">
                <label class="flbl">Strokes</label>
                <div class="rwrap">
                  <input type="range" min="1" max="10" value="1" id="ts" oninput="document.getElementById('tsv').textContent=this.value"/>
                  <span class="rval" id="tsv">1</span>
                </div>
              </div>
              <div class="fg">
                <label class="flbl">Duration (sec)</label>
                <div class="rwrap">
                  <input type="range" min="1" max="60" step="1" value="2" id="td" oninput="document.getElementById('tdv').textContent=this.value+'s'"/>
                  <span class="rval" id="tdv">2s</span>
                </div>
              </div>
              <div class="fg">
                <label class="flbl">&nbsp;</label>
                <button class="btn btn-a" onclick="testBell()">▶ Ring Now</button>
              </div>
            </div>
          </div>
        </div>
        <div class="card">
          <div class="card-hdr"><div class="card-ttl">🕐 Set RTC Time</div></div>
          <div class="fgrid" style="max-width:440px">
            <div class="fg"><label class="flbl">Date</label><input class="finp" type="date" id="mdate"/></div>
            <div class="fg"><label class="flbl">Time</label><input class="finp" type="time" id="mtime" step="1"/></div>
          </div>
          <div style="margin-top:14px;display:flex;gap:8px;flex-wrap:wrap">
            <button class="btn btn-p" onclick="setRTC()">💾 Set RTC</button>
            <button class="btn btn-gh" onclick="setBrowserTime()">📍 Use My Time</button>
          </div>
        </div>
      </div>

      <!-- SETTINGS -->
      <div class="sec" id="sec-cfg">
        <div class="sgrd">
          <div class="card">
            <div class="card-hdr"><div class="card-ttl">ℹ️ System Info</div></div>
            <div class="irow"><span class="ik">WiFi</span><span class="iv" id="ci-wifi">—</span></div>
            <div class="irow"><span class="ik">IP Address</span><span class="iv" id="ci-ip">—</span></div>
            <div class="irow"><span class="ik">SSID</span><span class="iv" id="ci-ssid">—</span></div>
            <div class="irow"><span class="ik">mDNS</span><span class="iv">smartbell.local</span></div>
          </div>
          <div class="card">
            <div class="card-hdr"><div class="card-ttl">🔑 Credentials</div></div>
            <div class="fg" style="margin-bottom:12px"><label class="flbl">Username</label><input class="finp" id="cu" type="text"/></div>
            <div class="fg" style="margin-bottom:14px"><label class="flbl">New Password</label><input class="finp" id="cp" type="password" placeholder="Leave blank to keep current"/></div>
            <button class="btn btn-p" onclick="saveCreds()">💾 Save</button>
          </div>
          <div class="card">
            <div class="card-hdr"><div class="card-ttl">🌍 Timezone</div></div>
            <div class="fg" style="margin-bottom:12px">
              <label class="flbl">UTC Offset</label>
              <select class="finp" id="ctz">
                <option value="-12">UTC-12</option><option value="-11">UTC-11</option>
                <option value="-10">UTC-10</option><option value="-9">UTC-9</option>
                <option value="-8">UTC-8 (PST)</option><option value="-7">UTC-7</option>
                <option value="-6">UTC-6 (CST)</option><option value="-5">UTC-5 (EST)</option>
                <option value="-4">UTC-4</option><option value="-3">UTC-3</option>
                <option value="-2">UTC-2</option><option value="-1">UTC-1</option>
                <option value="0">UTC+0 (GMT)</option><option value="1">UTC+1</option>
                <option value="2">UTC+2</option><option value="3">UTC+3</option>
                <option value="4">UTC+4</option><option value="5" selected>UTC+5 (PKT)</option>
                <option value="6">UTC+6</option><option value="7">UTC+7</option>
                <option value="8">UTC+8</option><option value="9">UTC+9 (JST)</option>
                <option value="10">UTC+10</option><option value="11">UTC+11</option>
                <option value="12">UTC+12</option>
              </select>
            </div>
            <div class="fg" style="margin-bottom:14px">
              <label class="flbl">Extra Minutes (e.g. +30 for India)</label>
              <select class="finp" id="ctzm">
                <option value="0">0 min</option>
                <option value="30">+30 min</option>
                <option value="45">+45 min</option>
              </select>
            </div>
            <button class="btn btn-p" onclick="saveTZ()">💾 Save Timezone</button>
          </div>
          <div class="card">
            <div class="card-hdr"><div class="card-ttl">📶 WiFi Setup</div></div>
            <div class="fg" style="margin-bottom:12px"><label class="flbl">SSID</label><input class="finp" id="wssid" type="text" placeholder="Network name"/></div>
            <div class="fg" style="margin-bottom:14px"><label class="flbl">Password</label><input class="finp" id="wpass" type="password" placeholder="WiFi password"/></div>
            <button class="btn btn-p" onclick="saveWifi()">📶 Save & Reboot</button>
            <p style="margin-top:8px;font-size:10px;color:var(--txt3)">⚠ Device will restart.</p>
          </div>
        </div>
        <div class="card" style="margin-top:18px">
          <div class="card-hdr"><div class="card-ttl">💾 Backup & Restore</div></div>
          <div style="display:flex;gap:10px;flex-wrap:wrap">
            <button class="btn btn-gh" onclick="exportJSON()">⬇ Export JSON</button>
            <label class="btn btn-gh" style="cursor:pointer">⬆ Import JSON<input type="file" accept=".json" style="display:none" onchange="importJSON(event)"/></label>
          </div>
        </div>
      </div>

    </div>
  </div>
</div>

<!-- MODAL -->
<div class="overlay" id="mov" onclick="closeMifBG(event)">
  <div class="modal">
    <div class="mttl" id="mttl">🔔 Add Bell</div>
    <div class="fgrid">
      <div class="fg full"><label class="flbl">Label</label><input class="finp" id="ml" type="text" placeholder="e.g. Period 1, Lunch Break"/></div>
      <div class="fg"><label class="flbl">Hour (0-23)</label><input class="finp" id="mh" type="number" min="0" max="23" value="8"/></div>
      <div class="fg"><label class="flbl">Minute (0-59)</label><input class="finp" id="mm" type="number" min="0" max="59" value="0"/></div>
      <div class="fg"><label class="flbl">Strokes</label><input class="finp" id="mst" type="number" min="1" max="10" value="1"/></div>
      <div class="fg"><label class="flbl">Duration (seconds)</label><input class="finp" id="mdu" type="number" min="1" max="60" step="1" value="2"/></div>
      <div class="fg full">
        <label class="flbl">Days</label>
        <div class="dcbs">
          <label class="dcbl"><input type="checkbox" data-b="0" checked/> Mon</label>
          <label class="dcbl"><input type="checkbox" data-b="1" checked/> Tue</label>
          <label class="dcbl"><input type="checkbox" data-b="2" checked/> Wed</label>
          <label class="dcbl"><input type="checkbox" data-b="3" checked/> Thu</label>
          <label class="dcbl"><input type="checkbox" data-b="4" checked/> Fri</label>
          <label class="dcbl"><input type="checkbox" data-b="5" checked/> Sat</label>
        </div>
      </div>
    </div>
    <div class="macts">
      <button class="btn btn-gh" onclick="closeM()">Cancel</button>
      <button class="btn btn-p" onclick="saveEntry()">💾 Save</button>
    </div>
  </div>
</div>

<!-- TOASTS -->
<div class="toasts" id="toasts"></div>

<script>
const DN=['Mon','Tue','Wed','Thu','Fri','Sat'];
const DF=['Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
const STITLE={'dash':'Dashboard','sched':'Bell Schedules','tt':'Weekly Timetable','bell':'Bell Control','cfg':'Settings'};
let scheds=[],editId=null,fdayBit='all',sysInfo={};

window.onload=()=>{
  fetch('/api/settings').then(r=>{if(r.ok)return r.json();throw 0})
  .then(d=>{sysInfo=d;enter()}).catch(()=>{});
  document.getElementById('lp').onkeydown=e=>{if(e.key==='Enter')doLogin()};
  setBrowserTime();
};

function doLogin(){
  const u=document.getElementById('lu').value,p=document.getElementById('lp').value;
  fetch('/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:u,password:p})})
  .then(r=>{if(r.ok)return r.json();throw 0}).then(()=>enter())
  .catch(()=>{const e=document.getElementById('lerr');e.style.display='block';setTimeout(()=>e.style.display='none',3000)});
}
function enter(){
  document.getElementById('lscr').style.display='none';
  document.getElementById('app').style.display='flex';
  loadSettings();loadSch();startClock();
}
function doLogout(){fetch('/logout').finally(()=>location.reload())}

function startClock(){tick();setInterval(tick,1000);setInterval(loadSch,30000)}
function tick(){
  fetch('/api/time').then(r=>r.json()).then(d=>{
    const hh=pad(d.hour),mm=pad(d.minute),ss=pad(d.second);
    document.getElementById('ct').textContent=`${hh}:${mm}:${ss}`;
    document.getElementById('cd').textContent=`${d.year}-${pad(d.month)}-${pad(d.day)}`;
    const days=['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
    document.getElementById('cdy').textContent=days[d.dow]||'';
    updateStats(d);
  }).catch(()=>{});
}

function go(name){
  document.querySelectorAll('.sec').forEach(s=>s.classList.remove('active'));
  document.getElementById('sec-'+name).classList.add('active');
  document.querySelectorAll('.ni').forEach(n=>n.classList.remove('active'));
  document.querySelector(`[data-s="${name}"]`).classList.add('active');
  document.getElementById('ttl').textContent=STITLE[name];
  if(name==='tt')renderTT();
  if(name==='cfg')fillSettings();
  document.getElementById('sidebar').classList.remove('open');
}

function loadSch(){
  fetch('/api/schedules').then(r=>r.json()).then(d=>{scheds=d;renderTable();renderTT()}).catch(()=>{});
}

function renderTable(){
  const tb=document.getElementById('stbody');
  let rows=scheds;
  if(fdayBit!=='all'){const b=1<<parseInt(fdayBit);rows=scheds.filter(s=>s.dayMask&b);}
  if(!rows.length){tb.innerHTML=`<tr><td colspan="6"><div class="empty"><div class="empty-ic">📭</div><p>No schedules.</p></div></td></tr>`;return;}
  rows=rows.slice().sort((a,b)=>a.hour*60+a.minute-(b.hour*60+b.minute));
  tb.innerHTML=rows.map(b=>{
    const id=scheds.indexOf(b);
    const dbs=DN.map((d,i)=>`<span class="db ${(b.dayMask>>i)&1?'on':'off'}">${d}</span>`).join('');
    return `<tr>
      <td><strong>${esc(b.label)}</strong></td>
      <td><span class="tbadge">${pad(b.hour)}:${pad(b.minute)}</span></td>
      <td><div class="dbadges">${dbs}</div></td>
      <td><span class="patt">🔔${b.strokes}× ${b.strokeDur}s</span></td>
      <td><label class="tog"><input type="checkbox" ${b.enabled?'checked':''} onchange="toggleEn(${id},this.checked)"/><span class="tslide"></span></label></td>
      <td><div class="abts">
        <button class="btn btn-gh btn-sm" onclick="openM(${id})">✏️</button>
        <button class="btn btn-d btn-sm" onclick="delE(${id})">🗑</button>
      </div></td>
    </tr>`;
  }).join('');
}

function fday(b){
  fdayBit=b;
  document.querySelectorAll('.dpill').forEach(p=>p.classList.toggle('active',p.dataset.d==String(b)));
  renderTable();
}

function toggleEn(id,v){scheds[id].enabled=v;pushSch()}
function delE(id){if(!confirm(`Delete "${scheds[id].label}"?`))return;scheds.splice(id,1);pushSch()}

function pushSch(){
  fetch('/api/schedules',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(scheds)})
  .then(r=>r.json()).then(d=>{if(d.ok){toast('Saved!','ok');renderTable();renderTT();}else toast('Save failed','er');})
  .catch(()=>toast('Connection error','er'));
}

function openM(id=null){
  editId=id;
  const isE=id!==null;
  document.getElementById('mttl').textContent=isE?'✏️ Edit Bell':'🔔 Add Bell';
  if(isE){
    const b=scheds[id];
    document.getElementById('ml').value=b.label;
    document.getElementById('mh').value=b.hour;
    document.getElementById('mm').value=b.minute;
    document.getElementById('mst').value=b.strokes;
    document.getElementById('mdu').value=b.strokeDur;
    document.querySelectorAll('.dcbl input').forEach(c=>c.checked=!!((b.dayMask>>parseInt(c.dataset.b))&1));
  }else{
    document.getElementById('ml').value='';
    document.getElementById('mh').value=8;
    document.getElementById('mm').value=0;
    document.getElementById('mst').value=1;
    document.getElementById('mdu').value=2;
    document.querySelectorAll('.dcbl input').forEach(c=>c.checked=true);
  }
  document.getElementById('mov').classList.add('open');
}
function closeM(){document.getElementById('mov').classList.remove('open')}
function closeMifBG(e){if(e.target.id==='mov')closeM()}

function saveEntry(){
  const label=document.getElementById('ml').value.trim()||'Bell';
  const hour=+document.getElementById('mh').value;
  const min=+document.getElementById('mm').value;
  const str=+document.getElementById('mst').value;
  const dur=+document.getElementById('mdu').value;
  let mask=0;
  document.querySelectorAll('.dcbl input').forEach(c=>{if(c.checked)mask|=(1<<+c.dataset.b)});
  if(!mask){toast('Select at least one day','er');return}
  const e={label,hour,minute:min,strokes:str,strokeDur:dur,strokeGap:2000,dayMask:mask,enabled:true};
  if(editId!==null)scheds[editId]={...scheds[editId],...e};else scheds.push(e);
  closeM();pushSch();
}

function renderTT(){
  const g=document.getElementById('ttgrid');
  g.innerHTML=DN.map((day,i)=>{
    const bit=1<<i;
    const bells=scheds.filter(b=>b.dayMask&bit).sort((a,b)=>a.hour*60+a.minute-(b.hour*60+b.minute));
    const ents=bells.length?bells.map(b=>`<div class="te ${b.enabled?'':'dis'}">
      <div class="teti">${pad(b.hour)}:${pad(b.minute)}</div>
      <div class="tela">${esc(b.label)}</div>
    </div>`).join(''):`<div style="color:var(--txt3);font-size:10px;padding:6px;text-align:center">No bells</div>`;
    return `<div class="dcol"><div class="dch">${day}</div><div class="dcb">${ents}</div></div>`;
  }).join('');
}

function updateStats(d){
  document.getElementById('s1').textContent=scheds.length;
  document.getElementById('s2').textContent=scheds.filter(b=>b.enabled).length;
  const bit=d.dow===0?0:(1<<(d.dow-1));
  const today=scheds.filter(b=>b.enabled&&(b.dayMask&bit));
  document.getElementById('s3').textContent=today.length;
  const now=d.hour*60+d.minute;
  let next='—',minD=Infinity;
  today.forEach(b=>{const df=b.hour*60+b.minute-now;if(df>0&&df<minD){minD=df;next=`${pad(b.hour)}:${pad(b.minute)}`}});
  document.getElementById('s4').textContent=next;
  const days=['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
  document.getElementById('todaylbl').textContent=`${days[d.dow]}, ${d.year}-${pad(d.month)}-${pad(d.day)}`;
  const tlist=document.getElementById('todaylist');
  if(!today.length){
    tlist.innerHTML=`<div class="empty"><div class="empty-ic">📭</div><p>No bells today${d.dow===0?' (Sunday)':''}</p></div>`;
    return;
  }
  const sorted=today.slice().sort((a,b)=>a.hour*60+a.minute-(b.hour*60+b.minute));
  tlist.innerHTML=`<div style="display:flex;flex-direction:column;gap:7px">
    ${sorted.map(b=>{
      const done=(b.hour*60+b.minute)<now;
      return `<div style="display:flex;align-items:center;gap:12px;padding:9px 12px;border-radius:7px;background:var(--sur2);border:1px solid var(--bdr);opacity:${done?.5:1}">
        <span style="font-family:var(--mono);font-size:15px;color:var(--acc);min-width:50px">${pad(b.hour)}:${pad(b.minute)}</span>
        <span style="flex:1;font-weight:600">${esc(b.label)}</span>
        <span style="font-size:11px;color:var(--txt3)">🔔${b.strokes}×</span>
        ${done?'<span style="font-size:10px;color:var(--txt3)">✓</span>':''}
      </div>`;
    }).join('')}
  </div>`;
}

function testBell(){
  const s=+document.getElementById('ts').value;
  const d=+document.getElementById('td').value;
  const ba=document.getElementById('bani');
  ba.classList.add('ring');
  setTimeout(()=>ba.classList.remove('ring'),Math.min(s*d*1000+(s-1)*2000+500,60000));
  fetch('/api/testbell',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({strokes:s,strokeDur:d,strokeGap:2000})})
  .then(r=>r.json()).then(d=>toast(d.ok?'Bell triggered!':'Error',d.ok?'ok':'er'))
  .catch(()=>toast('Connection error','er'));
}

function setBrowserTime(){
  const n=new Date();
  document.getElementById('mdate').value=`${n.getFullYear()}-${pad(n.getMonth()+1)}-${pad(n.getDate())}`;
  document.getElementById('mtime').value=`${pad(n.getHours())}:${pad(n.getMinutes())}:${pad(n.getSeconds())}`;
}

function setRTC(){
  const dt=document.getElementById('mdate').value,tm=document.getElementById('mtime').value;
  if(!dt||!tm){toast('Enter date and time','er');return}
  const [yr,mo,dy]=dt.split('-').map(Number),[hr,mn,sc]=tm.split(':').map(Number);
  fetch('/api/settime',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({year:yr,month:mo,day:dy,hour:hr,minute:mn,second:sc||0})})
  .then(r=>r.json()).then(d=>toast(d.ok?'RTC time set!':'Error',d.ok?'ok':'er'))
  .catch(()=>toast('Connection error','er'));
}

function loadSettings(){
  fetch('/api/settings').then(r=>r.json()).then(d=>{
    sysInfo=d;fillSettings();
    const chip=document.getElementById('wchip'),txt=document.getElementById('wtxt');
    if(d.wifiConnected){chip.className='schip on';txt.textContent=`Online · ${d.ip}`;}
    else{chip.className='schip off';txt.textContent='AP Mode';}
  }).catch(()=>{});
}

function fillSettings(){
  if(!sysInfo.user)return;
  document.getElementById('cu').value=sysInfo.user||'';
  document.getElementById('ci-ssid').textContent=sysInfo.ssid||'—';
  document.getElementById('ci-ip').textContent=sysInfo.ip||'—';
  document.getElementById('ci-wifi').textContent=sysInfo.wifiConnected?'Connected':'AP Mode';
  const tz=document.getElementById('ctz');if(tz)tz.value=String(sysInfo.timezone||5);
  const tzm=document.getElementById('ctzm');if(tzm)tzm.value=String(sysInfo.tzMinutes||0);
}

function saveCreds(){
  const u=document.getElementById('cu').value.trim(),p=document.getElementById('cp').value;
  if(!u){toast('Username required','er');return}
  const b={user:u};if(p)b.pass=p;
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})
  .then(r=>r.json()).then(d=>toast(d.ok?'Saved!':'Error',d.ok?'ok':'er'))
  .catch(()=>toast('Error','er'));
}

function saveTZ(){
  const tz=+document.getElementById('ctz').value,tzm=+document.getElementById('ctzm').value;
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({timezone:tz,tzMinutes:tzm})})
  .then(r=>r.json()).then(d=>toast(d.ok?'Timezone saved!':'Error',d.ok?'ok':'er'))
  .catch(()=>toast('Error','er'));
}

function saveWifi(){
  const s=document.getElementById('wssid').value.trim(),p=document.getElementById('wpass').value;
  if(!s){toast('SSID required','er');return}
  if(!confirm('Device will restart. Continue?'))return;
  fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s,password:p})})
  .then(r=>r.json()).then(d=>toast(d.msg||'Rebooting...','inf')).catch(()=>toast('Error','er'));
}

function exportJSON(){
  const b=new Blob([JSON.stringify(scheds,null,2)],{type:'application/json'});
  const u=URL.createObjectURL(b),a=document.createElement('a');
  a.href=u;a.download='bell_schedules.json';a.click();URL.revokeObjectURL(u);
  toast('Exported','ok');
}

function importJSON(e){
  const f=e.target.files[0];if(!f)return;
  const r=new FileReader();
  r.onload=ev=>{try{const d=JSON.parse(ev.target.result);if(!Array.isArray(d))throw 0;scheds=d;pushSch();}catch{toast('Invalid JSON','er')}};
  r.readAsText(f);e.target.value='';
}

function toast(msg,type='inf'){
  const el=document.createElement('div');
  el.className=`toast ${type}`;
  el.innerHTML={ok:'✅',er:'❌',inf:'ℹ️'}[type]+' '+esc(msg);
  document.getElementById('toasts').appendChild(el);
  setTimeout(()=>el.remove(),3000);
}

function pad(n){return String(n).padStart(2,'0')}
function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}
</script>
</body>
</html>
)====";

// ─── FORWARD DECLARATIONS ─────────────────────────────────────────────────────
void loadSettings(); void saveSettings();
void loadSchedules(); void saveSchedules();
void handleRoot(); void handleLogin(); void handleLogout();
void handleGetTime(); void handleGetSchedules(); void handleSetSchedules();
void handleTestBell(); void handleSetTime();
void handleGetSettings(); void handleSetSettings(); void handleSetWifi();
void handleNotFound();
bool isAuthenticated();
void ringBell(uint8_t strokes, uint16_t strokeDur, uint16_t strokeGap);
void checkSchedules();
void blinkLED(int times, int ms = 100);
uint8_t currentDayBit(uint8_t dow);

// ─── SETUP ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== Smart Bell System v3.0 (embedded HTML) ==="));

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN,   OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN,   HIGH);

  Wire.begin(4, 5);
  if (!rtc.begin()) {
    Serial.println(F("ERROR: RTC not found!"));
    blinkLED(10, 200);
  } else {
    if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println(F("RTC OK"));
  }

  // LittleFS only for data files (schedules + settings), NOT for HTML
  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS failed — schedules won't persist (check Flash Size setting)"));
  } else {
    Serial.println(F("LittleFS OK"));
  }

  loadSettings();
  loadSchedules();

  // WiFi
  if (strlen(cfg.ssid) > 0) {
    Serial.printf("Connecting to: %s\n", cfg.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid, cfg.wifiPass);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
      delay(500); Serial.print('.');
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());
      MDNS.begin(MDNS_HOST);
      MDNS.addService("http","tcp",80);
    }
  }
  if (!wifiConnected) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  }

  // Collect Cookie header for auth
  server.collectHeaders("Cookie");

  server.on("/",              HTTP_GET,  handleRoot);
  server.on("/login",         HTTP_POST, handleLogin);
  server.on("/logout",        HTTP_GET,  handleLogout);
  server.on("/api/time",      HTTP_GET,  handleGetTime);
  server.on("/api/schedules", HTTP_GET,  handleGetSchedules);
  server.on("/api/schedules", HTTP_POST, handleSetSchedules);
  server.on("/api/testbell",  HTTP_POST, handleTestBell);
  server.on("/api/settime",   HTTP_POST, handleSetTime);
  server.on("/api/settings",  HTTP_GET,  handleGetSettings);
  server.on("/api/settings",  HTTP_POST, handleSetSettings);
  server.on("/api/wifi",      HTTP_POST, handleSetWifi);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("HTTP server started — open http://192.168.4.1 or http://smartbell.local"));
  blinkLED(3, 150);
}

// ─── LOOP ─────────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();
  if (wifiConnected) MDNS.update();
  if (millis() - lastBellCheck >= 1000) {
    lastBellCheck = millis();
    checkSchedules();
  }
}

// ─── SCHEDULE CHECK ───────────────────────────────────────────────────────────
void checkSchedules() {
  DateTime now = rtc.now();
  int cur = now.hour() * 60 + now.minute();
  if (now.second() != 0) { lastMinuteFired = -1; return; }
  if (lastMinuteFired == cur) return;
  uint8_t dayBit = currentDayBit(now.dayOfTheWeek());
  if (!dayBit) return;
  for (int i = 0; i < bellCount; i++) {
    BellEntry& b = bells[i];
    if (!b.enabled) continue;
    if (!(b.dayMask & dayBit)) continue;
    if (b.hour == now.hour() && b.minute == now.minute()) {
      lastMinuteFired = cur;
      Serial.printf("Bell: %s\n", b.label);
      ringBell(b.strokes, b.strokeDur, b.strokeGap);
      break;
    }
  }
}

void ringBell(uint8_t strokes, uint16_t strokeDurSec, uint16_t /*unused*/) {
  if (!strokes) strokes = 1;
  for (int s = 0; s < strokes; s++) {
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN,   LOW);
    delay((uint32_t)strokeDurSec * 1000);  // seconds → ms
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(LED_PIN,   HIGH);
    if (s < strokes - 1) delay(2000);      // fixed 2s gap between strokes
  }
}

uint8_t currentDayBit(uint8_t dow) {
  if (dow == 0) return 0; // Sunday
  return (1 << (dow - 1));
}

void blinkLED(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW); delay(ms);
    digitalWrite(LED_PIN, HIGH); delay(ms);
  }
}

// ─── STORAGE ──────────────────────────────────────────────────────────────────
void loadSettings() {
  if (!LittleFS.exists(SETTINGS_FILE)) { saveSettings(); return; }
  File f = LittleFS.open(SETTINGS_FILE, "r");
  if (!f) return;
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    strlcpy(cfg.ssid,     doc["ssid"]     | "", sizeof(cfg.ssid));
    strlcpy(cfg.wifiPass, doc["wifiPass"] | "", sizeof(cfg.wifiPass));
    strlcpy(cfg.user,     doc["user"]     | "admin",    sizeof(cfg.user));
    strlcpy(cfg.pass,     doc["pass"]     | "admin123", sizeof(cfg.pass));
    cfg.timezone  = doc["timezone"]  | 5;
    cfg.tzMinutes = doc["tzMinutes"] | 0;
  }
  f.close();
}

void saveSettings() {
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) return;
  DynamicJsonDocument doc(512);
  doc["ssid"]      = cfg.ssid;
  doc["wifiPass"]  = cfg.wifiPass;
  doc["user"]      = cfg.user;
  doc["pass"]      = cfg.pass;
  doc["timezone"]  = cfg.timezone;
  doc["tzMinutes"] = cfg.tzMinutes;
  serializeJson(doc, f);
  f.close();
}

void loadSchedules() {
  bellCount = 0;
  if (!LittleFS.exists(SCHEDULE_FILE)) return;
  File f = LittleFS.open(SCHEDULE_FILE, "r");
  if (!f) return;
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  for (JsonObject obj : doc.as<JsonArray>()) {
    if (bellCount >= MAX_BELLS) break;
    BellEntry& b = bells[bellCount++];
    b.dayMask   = obj["dayMask"]   | 0x3F;
    b.hour      = obj["hour"]      | 8;
    b.minute    = obj["minute"]    | 0;
    b.strokes   = obj["strokes"]   | 1;
    b.strokeDur = obj["strokeDur"] | 2000;
    b.strokeGap = obj["strokeGap"] | 1000;
    b.enabled   = obj["enabled"]   | true;
    strlcpy(b.label, obj["label"] | "Bell", sizeof(b.label));
  }
  f.close();
  Serial.printf("Loaded %d schedules\n", bellCount);
}

void saveSchedules() {
  File f = LittleFS.open(SCHEDULE_FILE, "w");
  if (!f) return;
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < bellCount; i++) {
    BellEntry& b = bells[i];
    JsonObject o = arr.createNestedObject();
    o["dayMask"]=b.dayMask; o["hour"]=b.hour; o["minute"]=b.minute;
    o["strokes"]=b.strokes; o["strokeDur"]=b.strokeDur; o["strokeGap"]=b.strokeGap;
    o["label"]=b.label; o["enabled"]=b.enabled;
  }
  serializeJson(doc, f);
  f.close();
}

// ─── AUTH ─────────────────────────────────────────────────────────────────────
bool isAuthenticated() {
  if (!server.hasHeader("Cookie")) return false;
  return server.header("Cookie").indexOf("session=1") >= 0;
}

// ─── HTTP HANDLERS ────────────────────────────────────────────────────────────
void handleRoot() {
  // Serve HTML directly from PROGMEM — no LittleFS needed!
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleLogin() {
  if (!server.hasArg("plain")) { server.send(400,"application/json","{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(256);
  deserializeJson(doc, server.arg("plain"));
  if (strcmp(doc["username"]|"", cfg.user)==0 && strcmp(doc["password"]|"", cfg.pass)==0) {
    server.sendHeader("Set-Cookie","session=1; Path=/; HttpOnly");
    server.send(200,"application/json","{\"ok\":true}");
  } else {
    server.send(401,"application/json","{\"error\":\"Invalid credentials\"}");
  }
}

void handleLogout() {
  server.sendHeader("Set-Cookie","session=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
  server.sendHeader("Location","/");
  server.send(302);
}

void handleGetTime() {
  DateTime now = rtc.now();
  char buf[160];
  snprintf(buf, sizeof(buf),
    "{\"epoch\":%lu,\"year\":%d,\"month\":%d,\"day\":%d,"
    "\"hour\":%d,\"minute\":%d,\"second\":%d,\"dow\":%d,"
    "\"timezone\":%d,\"tzMinutes\":%d}",
    (unsigned long)now.unixtime(),
    now.year(),now.month(),now.day(),
    now.hour(),now.minute(),now.second(),
    now.dayOfTheWeek(),cfg.timezone,cfg.tzMinutes);
  server.send(200,"application/json",buf);
}

void handleGetSchedules() {
  if (!isAuthenticated()) { server.send(401,"application/json","{\"error\":\"Unauthorized\"}"); return; }
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for (int i=0;i<bellCount;i++){
    BellEntry& b=bells[i];
    JsonObject o=arr.createNestedObject();
    o["id"]=i;o["dayMask"]=b.dayMask;o["hour"]=b.hour;o["minute"]=b.minute;
    o["strokes"]=b.strokes;o["strokeDur"]=b.strokeDur;o["strokeGap"]=b.strokeGap;
    o["label"]=b.label;o["enabled"]=b.enabled;
  }
  String out; serializeJson(doc,out);
  server.send(200,"application/json",out);
}

void handleSetSchedules() {
  if (!isAuthenticated()) { server.send(401,"application/json","{\"error\":\"Unauthorized\"}"); return; }
  if (!server.hasArg("plain")) { server.send(400,"application/json","{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc,server.arg("plain"))!=DeserializationError::Ok) {
    server.send(400,"application/json","{\"error\":\"JSON error\"}"); return;
  }
  bellCount=0;
  for (JsonObject obj : doc.as<JsonArray>()) {
    if (bellCount>=MAX_BELLS) break;
    BellEntry& b=bells[bellCount++];
    b.dayMask=obj["dayMask"]|0x3F; b.hour=obj["hour"]|8; b.minute=obj["minute"]|0;
    b.strokes=obj["strokes"]|1; b.strokeDur=obj["strokeDur"]|2000;
    b.strokeGap=obj["strokeGap"]|1000; b.enabled=obj["enabled"]|true;
    strlcpy(b.label,obj["label"]|"Bell",sizeof(b.label));
  }
  saveSchedules();
  server.send(200,"application/json","{\"ok\":true}");
}

void handleTestBell() {
  if (!isAuthenticated()) { server.send(401,"application/json","{\"error\":\"Unauthorized\"}"); return; }
  DynamicJsonDocument doc(256);
  deserializeJson(doc,server.arg("plain"));
  ringBell(doc["strokes"]|1, doc["strokeDur"]|2000, doc["strokeGap"]|1000);
  server.send(200,"application/json","{\"ok\":true}");
}

void handleSetTime() {
  if (!isAuthenticated()) { server.send(401,"application/json","{\"error\":\"Unauthorized\"}"); return; }
  if (!server.hasArg("plain")) { server.send(400,"application/json","{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(256);
  deserializeJson(doc,server.arg("plain"));
  rtc.adjust(DateTime((int)(doc["year"]|2024),(int)(doc["month"]|1),(int)(doc["day"]|1),
    (int)(doc["hour"]|0),(int)(doc["minute"]|0),(int)(doc["second"]|0)));
  server.send(200,"application/json","{\"ok\":true}");
}

void handleGetSettings() {
  if (!isAuthenticated()) { server.send(401,"application/json","{\"error\":\"Unauthorized\"}"); return; }
  char buf[512];
  snprintf(buf,sizeof(buf),
    "{\"user\":\"%s\",\"timezone\":%d,\"tzMinutes\":%d,\"ssid\":\"%s\","
    "\"ip\":\"%s\",\"wifiConnected\":%s}",
    cfg.user,cfg.timezone,cfg.tzMinutes,cfg.ssid,
    wifiConnected?WiFi.localIP().toString().c_str():WiFi.softAPIP().toString().c_str(),
    wifiConnected?"true":"false");
  server.send(200,"application/json",buf);
}

void handleSetSettings() {
  if (!isAuthenticated()) { server.send(401,"application/json","{\"error\":\"Unauthorized\"}"); return; }
  if (!server.hasArg("plain")) { server.send(400,"application/json","{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(512);
  deserializeJson(doc,server.arg("plain"));
  if (doc.containsKey("user"))      strlcpy(cfg.user,doc["user"]|"",sizeof(cfg.user));
  if (doc.containsKey("pass"))      strlcpy(cfg.pass,doc["pass"]|"",sizeof(cfg.pass));
  if (doc.containsKey("timezone"))  cfg.timezone=doc["timezone"];
  if (doc.containsKey("tzMinutes")) cfg.tzMinutes=doc["tzMinutes"];
  saveSettings();
  server.send(200,"application/json","{\"ok\":true}");
}

void handleSetWifi() {
  if (!isAuthenticated()) { server.send(401,"application/json","{\"error\":\"Unauthorized\"}"); return; }
  if (!server.hasArg("plain")) { server.send(400,"application/json","{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(256);
  deserializeJson(doc,server.arg("plain"));
  strlcpy(cfg.ssid,    doc["ssid"]    |"",sizeof(cfg.ssid));
  strlcpy(cfg.wifiPass,doc["password"]|"",sizeof(cfg.wifiPass));
  saveSettings();
  server.send(200,"application/json","{\"ok\":true,\"msg\":\"Rebooting...\"}");
  delay(800); ESP.restart();
}

void handleNotFound() {
  server.sendHeader("Location","/");
  server.send(302);
}
