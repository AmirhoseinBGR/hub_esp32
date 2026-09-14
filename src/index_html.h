#pragma once
#include <pgmspace.h>
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>EvoHatch Hub Control Panel</title>
<style>
:root{--primary:#2563eb;--primary-hover:#1d4ed8;--bg:#0f172a;--card:#1e293b;--text:#f8fafc;--text-dim:#94a3b8;--border:#334155;--success:#22c55e;--danger:#ef4444;--warning:#f59e0b}
*{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}
body{background:var(--bg);color:var(--text);padding:1rem;line-height:1.5}
.container{max-width:1100px;margin:0 auto}
header{display:flex;justify-content:space-between;align-items:center;padding:1rem 1.5rem;background:var(--card);border:1px solid var(--border);border-radius:12px;margin-bottom:1.5rem}
.brand{font-size:1.3rem;font-weight:700;color:#38bdf8;display:flex;align-items:center;gap:8px}
.status-pill{padding:4px 12px;border-radius:20px;font-size:0.85rem;font-weight:600}
.status-ok{background:#166534;color:#86efac}
.status-err{background:#991b1b;color:#fca5a5}
.tabs{display:flex;gap:8px;margin-bottom:1.5rem;overflow-x:auto;padding-bottom:4px}
.tab-btn{background:var(--card);border:1px solid var(--border);color:var(--text);padding:10px 18px;border-radius:8px;cursor:pointer;font-weight:600;transition:.2s;white-space:nowrap}
.tab-btn.active{background:var(--primary);border-color:var(--primary)}
.tab-content{display:none}
.tab-content.active{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:1rem;margin-bottom:1.5rem}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:1.25rem}
.card h3{font-size:1.05rem;color:var(--text-dim);margin-bottom:1rem;border-bottom:1px solid var(--border);padding-bottom:0.5rem}
.stat-row{display:flex;justify-content:space-between;padding:0.4rem 0;border-bottom:1px solid #263349}
.stat-row:last-child{border-bottom:none}
table{width:100%;border-collapse:collapse;margin-top:0.5rem}
th,td{padding:10px;text-align:right;border-bottom:1px solid var(--border);font-size:0.9rem}
th{color:var(--text-dim);background:#162032}
input,select,button{width:100%;padding:10px 12px;border-radius:8px;border:1px solid var(--border);background:#0b1120;color:var(--text);margin-top:4px;margin-bottom:12px;font-size:0.95rem}
button{background:var(--primary);color:#fff;font-weight:600;cursor:pointer;border:none;transition:.2s}
button:hover{background:var(--primary-hover)}
.btn-secondary{background:#334155}
.btn-secondary:hover{background:#475569}
.badge{display:inline-block;padding:2px 8px;border-radius:6px;font-size:0.75rem;font-weight:600}
.badge-active{background:#14532d;color:#4ade80}
.badge-inactive{background:#450a0a;color:#f87171}
</style>
</head>
<body>
<div class="container">
  <header>
    <div class="brand">⚡ سامانه مرکزی پایش و کنترل هاب (EvoHatch Hub)</div>
    <div id="sim-status-badge" class="status-pill status-ok">وضعیت سیم‌کارت: فعال</div>
  </header>

  <div class="tabs">
    <button class="tab-btn active" onclick="showTab('dash')">📊 وضعیت لحظه‌ای دستگاه‌ها</button>
    <button class="tab-btn" onclick="showTab('wifi')">📶 تنظیمات وای‌فای</button>
    <button class="tab-btn" onclick="showTab('sim')">📱 سیم‌کارت و تماس</button>
    <button class="tab-btn" onclick="showTab('server')">☁ سرور مرکزی و همگام‌سازی</button>
  </div>

  <!-- تب وضعیت دستگاه‌ها -->
  <div id="dash" class="tab-content active">
    <div class="grid">
      <div class="card">
        <h3>مشخصات کلی هاب</h3>
        <div class="stat-row"><span>وضعیت لینک STM32:</span><span id="stm-link" style="color:var(--success)">متصل (UART6)</span></div>
        <div class="stat-row"><span>سیم‌کارت / ماژول GSM:</span><span id="gsm-state">آنلاین (Ready)</span></div>
        <div class="stat-row"><span>اعتبار شارژ سیم‌کارت:</span><span id="sim-credit">۲۵,۰۰۰ تومان</span></div>
        <div class="stat-row"><span>آی‌پی محلی اکسس‌پوینت:</span><span>192.168.4.1</span></div>
        <div class="stat-row"><span>شبکه وای‌فای متصل:</span><span id="wifi-cur-ssid">عدم اتصال</span></div>
      </div>
      <div class="card">
        <h3>وضعیت ارتباط با سرور</h3>
        <div class="stat-row"><span>آدرس سرور:</span><span id="srv-domain">deuteriumStudio.ir</span></div>
        <div class="stat-row"><span>نرخ ارسال داده:</span><span id="srv-rate">۱۲ بار در ساعت</span></div>
        <div class="stat-row"><span>وضعیت تست پینگ:</span><span id="srv-ping-status" style="color:var(--success)">پاسخ دریافت شد (OK)</span></div>
        <button onclick="testServerPing()" style="margin-top:10px">تست ارتباط با سرور (پینگ)</button>
      </div>
    </div>

    <div class="card">
      <h3>لیست دستگاه‌های متصل (اینکوباتورها)</h3>
      <table>
        <thead>
          <tr><th>شناسه</th><th>وضعیت</th><th>دما (°C)</th><th>تنظیم دما</th><th>رطوبت (%)</th><th>تنظیم رطوبت</th><th>وضعیت چرخش</th><th>آلارم‌ها</th></tr>
        </thead>
        <tbody id="devices-body">
          <tr><td>دستگاه ۱</td><td><span class="badge badge-active">آنلاین</span></td><td>37.5</td><td>37.5</td><td>60.2</td><td>60.0</td><td>چپ</td><td>عادی</td></tr>
          <tr><td>دستگاه ۲</td><td><span class="badge badge-active">آنلاین</span></td><td>37.8</td><td>38.0</td><td>59.1</td><td>60.0</td><td>راست</td><td>عادی</td></tr>
          <tr><td>دستگاه ۳</td><td><span class="badge badge-inactive">آفلاین</span></td><td>--</td><td>37.5</td><td>--</td><td>60.0</td><td>--</td><td>خطای ارتباط</td></tr>
        </tbody>
      </table>
    </div>
  </div>

  <!-- تب وای‌فای -->
  <div id="wifi" class="tab-content">
    <div class="card">
      <h3>اسکن و اتصال به شبکه وای‌فای</h3>
      <button class="btn-secondary" onclick="scanWifi()">🔍 اسکن شبکه‌های اطراف</button>
      <label>شبکه مورد نظر (SSID):</label>
      <select id="wifi-select"><option value="">-- اسکن کنید --</option></select>
      <label>رمز عبور شبکه:</label>
      <input type="password" id="wifi-pass" placeholder="رمز عبور وای‌فای...">
      <button onclick="connectWifi()">اتصال به وای‌فای</button>
    </div>
  </div>

  <!-- تب سیم‌کارت -->
  <div id="sim" class="tab-content">
    <div class="card">
      <h3>تنظیمات شماره‌های هشدار سیم‌کارت</h3>
      <label>شماره تماس ۱:</label>
      <input type="text" id="sim-p1" placeholder="09xxxxxxxxx" maxlength="11">
      <label>شماره تماس ۲:</label>
      <input type="text" id="sim-p2" placeholder="09xxxxxxxxx" maxlength="11">
      <label>شماره تماس ۳:</label>
      <input type="text" id="sim-p3" placeholder="09xxxxxxxxx" maxlength="11">
      <button onclick="saveSim()">ذخیره شماره‌ها</button>
    </div>
  </div>

  <!-- تب سرور -->
  <div id="server" class="tab-content">
    <div class="card">
      <h3>تنظیمات سرور و همگام‌سازی</h3>
      <label>آدرس سرور مرکزی:</label>
      <input type="text" id="server-input-domain" value="deuteriumStudio.ir">
      <label>دفعات ارسال در ساعت:</label>
      <select id="server-input-rate">
        <option value="1">۱ بار در ساعت</option>
        <option value="4">۴ بار در ساعت</option>
        <option value="12" selected>۱۲ بار در ساعت</option>
        <option value="30">۳۰ بار در ساعت</option>
        <option value="60">۶۰ بار در ساعت</option>
      </select>
      <button onclick="saveServer()">اعمال تنظیمات سرور</button>
    </div>
  </div>
</div>

<script>
function showTab(id){
  document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));
  document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));
  event.target.classList.add('active');
  document.getElementById(id).classList.add('active');
}
function scanWifi(){
  fetch('/api/wifi/scan').then(r=>r.json()).then(data=>{
    let sel = document.getElementById('wifi-select');
    sel.innerHTML = '';
    data.networks.forEach(net=>{
      let opt = document.createElement('option');
      opt.value = net; opt.innerText = net;
      sel.appendChild(opt);
    });
  });
}
function connectWifi(){
  let ssid = document.getElementById('wifi-select').value;
  let pass = document.getElementById('wifi-pass').value;
  fetch('/api/wifi/connect', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ssid, pass})})
  .then(r=>r.json()).then(res=>{ alert(res.msg); loadStatus(); });
}
function testServerPing(){
  document.getElementById('srv-ping-status').innerText = '...در حال ارسال پینگ';
  fetch('/api/server/ping').then(r=>r.json()).then(res=>{
    let el = document.getElementById('srv-ping-status');
    el.innerText = res.ok ? 'ارتباط برقرار است (OK)' : 'خطا در برقراری ارتباط (Error)';
    el.style.color = res.ok ? 'var(--success)' : 'var(--danger)';
  });
}
function saveSim(){
  let p1 = document.getElementById('sim-p1').value;
  let p2 = document.getElementById('sim-p2').value;
  let p3 = document.getElementById('sim-p3').value;
  fetch('/api/sim/save', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({p1, p2, p3})})
  .then(r=>r.json()).then(res=>alert(res.msg));
}
function saveServer(){
  let domain = document.getElementById('server-input-domain').value;
  let rate = document.getElementById('server-input-rate').value;
  fetch('/api/server/save', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({domain, rate})})
  .then(r=>r.json()).then(res=>alert(res.msg));
}
function loadStatus(){
  fetch('/api/status').then(r=>r.json()).then(s=>{
    if(s.sim_ok){
      document.getElementById('sim-status-badge').className = 'status-pill status-ok';
      document.getElementById('sim-status-badge').innerText = 'وضعیت سیم‌کارت: فعال';
      document.getElementById('gsm-state').innerText = 'آماده (Ready)';
    } else {
      document.getElementById('sim-status-badge').className = 'status-pill status-err';
      document.getElementById('sim-status-badge').innerText = 'وضعیت سیم‌کارت: غیرفعال یا خطا';
      document.getElementById('gsm-state').innerText = 'عدم دسترسی';
    }
    if(s.wifi_ssid){ document.getElementById('wifi-cur-ssid').innerText = s.wifi_ssid; }
    if(s.sim_p1) document.getElementById('sim-p1').value = s.sim_p1;
    if(s.sim_p2) document.getElementById('sim-p2').value = s.sim_p2;
    if(s.sim_p3) document.getElementById('sim-p3').value = s.sim_p3;
  }).catch(()=>{});
}
setInterval(loadStatus, 4000);
loadStatus();
</script>
</body>
</html>)rawliteral";
