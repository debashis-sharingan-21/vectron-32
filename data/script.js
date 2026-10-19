/* ==========================================================================
   VECTRON-32 v2.0 Real-Time Instrumentation & SCADA Console
   Flow: LOGIN -> ACCESS GRANTED -> BOOT SEQUENCE -> DASHBOARD
   Transport: WebSocket at ws://<host>/ws
   ========================================================================== */
(() => {
'use strict';

// ============================================================
// 1. CONSTANTS & STATE
// ============================================================
const WS_URL = `ws://${location.host}/ws`;

let ws            = null;
let wsConnected   = false;
let wsReconnTimer = 0;
let wsReconnDelay = 1000;

const state = {
  telem:   null,
  outSig:  '',
  inSig:   '',
  rebooting: false,
  diagFilters: new Set(['INFO', 'WARN', 'ERROR', 'BUS']),
};

// ============================================================
// 2. UTILITY FUNCTIONS
// ============================================================
const $  = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
const sleep = ms => new Promise(r => setTimeout(r, ms));
const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

function h(tag, props, ...kids) {
  const e = document.createElement(tag);
  if (props) for (const k of Object.keys(props)) {
    if (k === 'class') e.className = props[k];
    else if (k === 'text') e.textContent = props[k];
    else if (k.startsWith('data-')) e.setAttribute(k, props[k]);
    else e[k] = props[k];
  }
  for (const kid of kids) if (kid != null) e.append(kid);
  return e;
}

const pad2 = n => String(n).padStart(2, '0');
const kb   = b => (b / 1024).toFixed(b > 10240 ? 0 : 1) + ' KB';

function fmtUptime(s) {
  const d = Math.floor(s / 86400);
  const hh = Math.floor((s % 86400) / 3600);
  const m  = Math.floor((s % 3600) / 60);
  const ss = s % 60;
  return (d ? d + 'd ' : '') + pad2(hh) + ':' + pad2(m) + ':' + pad2(ss);
}

// Simple REST helper (for login / session / logout only)
async function api(path, opts = {}) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), opts.timeout || 6000);
  const init = { method: opts.method || 'GET', credentials: 'same-origin', signal: ctrl.signal, headers: {} };
  if (opts.form || init.method === 'POST') {
    init.method = 'POST';
    init.headers['Content-Type'] = 'application/x-www-form-urlencoded';
    init.body = new URLSearchParams(opts.form || {}).toString();
  }
  let res;
  try { res = await fetch(path, init); }
  catch (e) { throw Object.assign(new Error('NO LINK'), { net: true }); }
  finally { clearTimeout(timer); }
  let data = null;
  try { data = await res.json(); } catch (_) {}
  if (res.status === 401 && path !== '/api/login' && path !== '/api/session') sessionLost('SESSION EXPIRED');
  return { status: res.status, ok: res.ok, data };
}

// ============================================================
// 3. SCREEN MANAGEMENT
// ============================================================
let current = null;
const hooks = { login: [], dash: [] };

async function show(name) {
  let next = $('#screen-' + name);
  if (!next && (name === 'dash' || name === 'dashboard')) {
    next = $('#screen-dashboard') || $('#screen-dash');
  }
  if (!next) return;
  if (current === next) return;
  if (current) {
    current.classList.add('leaving');
    await sleep(reduced ? 0 : 270);
    current.classList.remove('leaving', 'active');
  }
  next.classList.add('active');
  document.body.dataset.screen = name;
  current = next;
  login.active = (name === 'login');
  if (name === 'login') login.startFx();
  else login.stopFx();
  if (name === 'dash' || name === 'dashboard') dashboard.onEnter();
}

// ============================================================
// 4. LOGIN SCREEN — 3D PERSPECTIVE TUNNEL & PIXEL SKULL
// ============================================================
const SKULL_HALF = [
  '..........#####', '......#########', '....###########', '...############', '..#############',
  '..#############', '.##############', '.##############', '.##############', '.####....######',
  '.###......#####', '.###......#####', '.###......#####', '.####....######', '..#############',
  '..############.', '...##########..', '...############', '....###########', '....###########',
  '....##.##.##.##', '....##.##.##.##', '.....#########.', '......#########',
];
const SKULL = SKULL_HALF.map(r => r + r.split('').reverse().join(''));

function drawLoginCanvas(t) {
  const cv = $('#login-canvas');
  if (!cv) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const w = 520, h = 400;
  if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
    cv.width = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
  }
  const ctx = cv.getContext('2d');
  ctx.save();
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.imageSmoothingEnabled = false;
  ctx.clearRect(0, 0, w, h);

  const cx = w / 2, cy = h / 2;
  const ts = (t || 0) / 1000;

  // 1. Radar Grid Lines & Circular Reticle Background
  ctx.save();
  ctx.strokeStyle = 'rgba(0, 255, 102, 0.16)';
  ctx.lineWidth = 1;
  ctx.setLineDash([4, 6]);
  for (const r of [70, 140, 210]) {
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.moveTo(cx - 240, cy); ctx.lineTo(cx + 240, cy);
  ctx.moveTo(cx, cy - 180); ctx.lineTo(cx, cy + 180);
  ctx.stroke();

  // Rotating Radar Sweep Beam (10Hz active radar sweep)
  if (!reduced) {
    const sweepAngle = (ts * 1.5) % (Math.PI * 2);
    const grad = ctx.createRadialGradient(cx, cy, 10, cx, cy, 210);
    grad.addColorStop(0, 'rgba(0, 255, 102, 0.28)');
    grad.addColorStop(1, 'rgba(0, 255, 102, 0.0)');
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.arc(cx, cy, 210, sweepAngle - 0.4, sweepAngle);
    ctx.closePath();
    ctx.fill();
  }
  ctx.restore();

  // 2. Infinite 3D Wireframe Perspective Tunnel (10 exponential depth rectangles)
  const iw = w * 0.28, ih = h * 0.34;
  const ow = w * 0.98, oh = h * 0.98;

  // Dual-tone neon glow effect (shadowColor = '#00ff66', shadowBlur = 6)
  ctx.lineWidth = 1.4;
  ctx.strokeStyle = '#00ff66';
  ctx.shadowColor = '#00ff66';
  ctx.shadowBlur = 6;

  // Vanishing rays converging at center screen
  const rays = [
    [-1, -1], [-0.66, -1], [-0.33, -1], [0, -1], [0.33, -1], [0.66, -1], [1, -1],
    [-1, 1], [-0.66, 1], [-0.33, 1], [0, 1], [0.33, 1], [0.66, 1], [1, 1],
    [-1, -0.5], [-1, 0], [-1, 0.5],
    [1, -0.5], [1, 0], [1, 0.5]
  ];

  ctx.globalAlpha = 0.55;
  ctx.beginPath();
  for (const [rx, ry] of rays) {
    ctx.moveTo(cx + rx * (iw / 2), cy + ry * (ih / 2));
    ctx.lineTo(cx + rx * (ow / 2), cy + ry * (oh / 2));
  }
  ctx.stroke();

  // 10 Exponential depth rectangles synchronized with skull breathing pulsation Math.sin(ts * 2.5)
  const N = 10;
  const phase = reduced ? 0 : ((ts * 0.24) + 0.02 * Math.sin(ts * 2.5)) % 1;
  for (let k = 0; k < N; k++) {
    const u = (k + phase) / N;
    const e = Math.pow(u, 2.2);
    const rw = (iw / 2) + ((ow - iw) / 2) * e;
    const rh = (ih / 2) + ((oh - ih) / 2) * e;
    ctx.globalAlpha = 0.18 + 0.82 * e;
    // Dual-tone outline highlight
    ctx.strokeStyle = e > 0.65 ? '#70ff9c' : '#00ff66';
    ctx.strokeRect(cx - rw, cy - rh, rw * 2, rh * 2);
  }

  // Inner vanishing portal frame
  ctx.globalAlpha = 0.95;
  ctx.lineWidth = 2.0;
  ctx.strokeStyle = '#70ff9c';
  ctx.strokeRect(cx - iw / 2, cy - ih / 2, iw, ih);

  // Center depth crosshair
  ctx.globalAlpha = 0.45;
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(cx - 10, cy); ctx.lineTo(cx + 10, cy);
  ctx.moveTo(cx, cy - 10); ctx.lineTo(cx, cy + 10);
  ctx.stroke();

  // 3. Pixel Skull Drawing (24x24 bitmap reference match scaled to 520x400)
  const cols = SKULL[0].length; // 30
  const rows = SKULL.length;    // 24
  const breathe = reduced ? 1 : 1 + 0.04 * Math.sin(ts * 2.5);
  const cell = Math.min((w * 0.49) / cols, (h * 0.65) / rows) * breathe;
  const ox = cx - (cell * cols) / 2;
  const oy = cy - (cell * rows) / 2 - 4;
  const dot = cell * 0.84;
  const off = (cell - dot) / 2;

  ctx.shadowColor = '#00ff66';
  ctx.shadowBlur = 6;

  for (let r = 0; r < rows; r++) {
    const scan = reduced ? 1 : 0.82 + 0.18 * Math.sin(ts * 4.2 - r * 0.45);
    for (let c = 0; c < cols; c++) {
      if (SKULL[r][c] !== '#') continue;
      const edge = (r === 0 || SKULL[r-1][c] !== '#' || c === 0 || SKULL[r][c-1] !== '#' ||
                    c === cols - 1 || SKULL[r][c+1] !== '#');
      ctx.fillStyle = edge ? '#b9ffcb' : '#00ff66';
      ctx.globalAlpha = scan * (edge ? 1 : 0.88);
      ctx.fillRect(Math.round(ox + c * cell + off), Math.round(oy + r * cell + off), Math.round(dot), Math.round(dot));
    }
  }

  // Animated pulsing eye-socket glow effect (#00ff66 to #33ff88) with subtle flicker
  if (!reduced) {
    const eyePulse = 0.5 + 0.5 * Math.sin(ts * 3.5);
    const eyeFlicker = 0.70 + 0.30 * Math.sin(ts * 14 + Math.cos(ts * 8));
    const leftEyeX = ox + 7 * cell + cell / 2;
    const rightEyeX = ox + 22 * cell + cell / 2;
    const eyeY = oy + 11.5 * cell;
    const eyeR = cell * 2.2;

    ctx.save();
    ctx.shadowBlur = 16;
    const rVal = Math.round(eyePulse * 51);
    const gVal = 255;
    const bVal = Math.round(102 + eyePulse * 34);
    ctx.shadowColor = `rgb(${rVal}, ${gVal}, ${bVal})`;

    ctx.fillStyle = `rgba(${rVal}, ${gVal}, ${bVal}, ${0.34 * eyeFlicker})`;
    ctx.beginPath();
    ctx.arc(leftEyeX, eyeY, eyeR, 0, Math.PI * 2);
    ctx.arc(rightEyeX, eyeY, eyeR, 0, Math.PI * 2);
    ctx.fill();

    // Intense pupil glint
    ctx.fillStyle = `rgba(195, 255, 225, ${0.85 * eyeFlicker})`;
    ctx.beginPath();
    ctx.arc(leftEyeX, eyeY, eyeR * 0.38, 0, Math.PI * 2);
    ctx.arc(rightEyeX, eyeY, eyeR * 0.38, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  // Dual-Axis Phosphor Scanline Overlay (Horizontal CRT scanlines + Vertical phosphor columns)
  ctx.save();
  ctx.globalAlpha = 0.05;
  ctx.fillStyle = '#00ff66';
  for (let x = 0; x < w; x += 4) {
    ctx.fillRect(x, 0, 1, h);
  }
  ctx.globalAlpha = 0.08;
  for (let y = 0; y < h; y += 3) {
    ctx.fillRect(0, y, w, 1);
  }
  ctx.restore();

  // Dynamic target coordinates readout updating
  const az = (142.8 + 0.4 * Math.sin(ts * 0.7)).toFixed(1);
  const el = (-12.4 + 0.2 * Math.cos(ts * 0.5)).toFixed(1);
  const azElEl = $('#hud-azel');
  if (azElEl) azElEl.textContent = `AZ: ${az}° | EL: ${el}°`;

  ctx.restore();
}

const login = {
  active: false, busy: false, locked: false, raf: 0, lines: [], lockTimer: 0,
  startFx() {
    this.active = true;
    cancelAnimationFrame(this.raf);
    const loop = t => {
      if (!this.active) return;
      drawLoginCanvas(t);
      this.raf = requestAnimationFrame(loop);
    };
    this.raf = requestAnimationFrame(loop);
  },
  stopFx() {
    this.active = false;
    cancelAnimationFrame(this.raf);
  }
};

// Login log box
function logSet(key, text, state) {
  const i = login.lines.findIndex(l => l.key === key);
  const line = { key, text, state: state || '' };
  if (i >= 0) login.lines[i] = line; else login.lines.push(line);
  if (login.lines.length > 5) login.lines.shift();
  const ul = $('#login-log');
  ul.replaceChildren(...login.lines.map(l => {
    const li = h('li', { class: l.state });
    li.append(l.key.padEnd(5, ' ').replace(/ /g, '\u00a0') + '...... ', h('em', { text: l.text }));
    return li;
  }));
}

const setLed = (id, mode) => {
  const e = $('#' + id);
  e.className = 'led' + (mode ? ' ' + mode : '');
};

function setBtn(label, disabled, cls) {
  const b = $('#btn-login');
  b.textContent = label; b.disabled = !!disabled;
  b.className = 'lf-btn' + (cls ? ' ' + cls : '');
}

function loginDenied(left) {
  logSet('AUTH', 'ACCESS DENIED', 'bad');
  if (typeof left === 'number') logSet('TRIES', left + ' LEFT', left <= 1 ? 'bad' : '');
  setLed('led-auth', 'warn');
  setTimeout(() => setLed('led-auth', ''), 1400);
  const f = $('.login-frame');
  f.classList.remove('shake'); void f.offsetWidth; f.classList.add('shake');
  $('#in-pass').value = '';
  $('#in-pass').focus();
}

function loginLock(seconds) {
  login.locked = true;
  setLed('led-lock', 'warn'); setLed('led-auth', '');
  logSet('AUTH', 'LOCKED OUT', 'bad');
  let left = seconds;
  clearInterval(login.lockTimer);
  const tick = () => {
    if (left <= 0) {
      clearInterval(login.lockTimer); login.locked = false;
      setLed('led-lock', ''); logSet('AUTH', 'STANDBY'); logSet('TRIES', 'RESET');
      setBtn('[ AUTHENTICATE ]', false); return;
    }
    setBtn('[ LOCKED ' + left + 's ]', true, 'locked'); left--;
  };
  tick(); login.lockTimer = setInterval(tick, 1000);
}

async function onLoginSubmit(ev) {
  ev.preventDefault();
  if (login.busy || login.locked) return;
  const user = $('#in-user').value, pass = $('#in-pass').value;
  if (!user || !pass) { logSet('AUTH', 'EMPTY FIELD', 'bad'); return; }
  login.busy = true;
  setBtn('[ VERIFYING... ]', true); setLed('led-auth', 'warn'); logSet('AUTH', 'VERIFYING', 'blink');
  $('#in-pass').value = '';
  try {
    const r = await api('/api/login', { form: { user, pass } });
    setLed('led-link', 'on');
    if (r.ok && r.data && r.data.ok) {
      setLed('led-auth', 'on'); logSet('AUTH', 'ACCESS OK');
      $('#in-user').value = '';
      await sleep(350);
      await runSequence();
      return;
    }
    if (r.status === 429) loginLock((r.data && r.data.retryAfter) || 60);
    else if (r.status === 401) loginDenied(r.data && r.data.attemptsLeft);
    else { logSet('AUTH', 'ERROR ' + r.status, 'bad'); setLed('led-auth', ''); }
  } catch (e) {
    setLed('led-link', 'warn'); setLed('led-auth', '');
    logSet('LINK', 'FAULT', 'bad'); logSet('AUTH', 'NO RESPONSE', 'bad');
  } finally {
    login.busy = false;
    if (!login.locked) setBtn('[ AUTHENTICATE ]', false);
  }
}

/// ============================================================
// 5. BOOT SEQUENCE ANIMATION (ESP32 DevKit V1 Vector Schematic)
// ============================================================
const PIN_L = ['3V3','EN','VP','VN','D34','D35','D32','D33','D25','D26','D27','D14','D12','D13','CMD','SD0','SD1','CLK','GND'];
const PIN_R = ['VIN','GND','D15','D2','D4','RX2','TX2','D5','D18','D19','D21','RX0','TX0','D22','D23','D24','SD2','SD3','3V3'];

function buildBoard() {
  const NS = 'http://www.w3.org/2000/svg';
  const py = i => 85 + i * 26.5; // 19 pins from 85 to 562
  let s = '';

  // 1. Outline & Micro-USB Connector (Step 1)
  s += '<g id="g-outline">';
  // PCB Outer boundary & corner mounting holes
  s += '<rect class="ln" x="8" y="8" width="384" height="604" rx="18"/>';
  s += '<rect class="ln thin" x="14" y="14" width="372" height="592" rx="14"/>';
  s += '<circle class="ln" cx="28" cy="26" r="11"/><circle class="ln thin" cx="28" cy="26" r="5.5"/>';
  s += '<circle class="ln" cx="372" cy="26" r="11"/><circle class="ln thin" cx="372" cy="26" r="5.5"/>';
  s += '<circle class="ln" cx="28" cy="594" r="11"/><circle class="ln thin" cx="28" cy="594" r="5.5"/>';
  s += '<circle class="ln" cx="372" cy="594" r="11"/><circle class="ln thin" cx="372" cy="594" r="5.5"/>';
  // Micro-USB connector with metallic casing geometry & solder lugs
  s += '<path class="ln" d="M 155 8 H 245 V 54 H 155 Z"/>';
  s += '<rect class="ln" x="146" y="18" width="9" height="22" rx="2"/>';
  s += '<rect class="ln" x="245" y="18" width="9" height="22" rx="2"/>';
  s += '<path class="ln thin" d="M 166 14 H 234 V 42 Q 234 48 228 48 H 172 Q 166 48 166 42 Z"/>';
  s += '<rect class="ln thin" x="180" y="22" width="40" height="10" rx="2"/>';
  for (let i = 0; i < 5; i++) {
    s += `<line class="ln thin" x1="${186 + i * 7}" y1="48" x2="${186 + i * 7}" y2="58"/>`;
  }
  s += '</g>';

  // 2. Copper routing traces (Step 2)
  s += '<g id="g-traces">';
  // USB Differential Pair (D+/D-) with 45-degree chamfers
  s += '<path class="ln thin" d="M 193 58 V 66 L 186 73 V 86"/>';
  s += '<path class="ln thin" d="M 207 58 V 66 L 214 73 V 86"/>';
  // 5V bus to AMS1117 regulator
  s += '<path class="ln thin" d="M 186 58 V 70 L 252 70 L 268 86"/>';
  // UART lines CP2102 to ESP-WROOM-32
  s += '<path class="ln thin" d="M 184 142 L 184 158 L 150 158 L 150 174"/>';
  s += '<path class="ln thin" d="M 216 142 L 216 158 L 250 158 L 250 174"/>';
  // 3.3V Output bus
  s += '<path class="ln thin" d="M 285 142 V 156 L 310 156 L 310 174"/>';
  s += '<path class="ln thin" d="M 285 148 L 354 148 L 354 86"/>';
  // Left header breakout traces
  for (let i = 0; i < 14; i++) {
    const ty = 184 + i * 16;
    s += `<path class="ln thin" d="M 75 ${ty} L 50 ${ty} L 36 ${85 + (i + 4) * 26.5}"/>`;
  }
  // Right header breakout traces
  for (let i = 0; i < 14; i++) {
    const ty = 184 + i * 16;
    s += `<path class="ln thin" d="M 325 ${ty} L 350 ${ty} L 364 ${85 + (i + 2) * 26.5}"/>`;
  }
  s += '</g>';

  // 3. Passives, crystal oscillator & switches (Step 2)
  s += '<g id="g-passives">';
  // 40.000 MHz Crystal Oscillator
  s += '<rect class="ln" x="86" y="84" width="52" height="34" rx="5"/>';
  s += '<rect class="ln thin" x="91" y="89" width="42" height="24" rx="3"/>';
  s += '<text class="show-late" x="112" y="102" text-anchor="middle" font-size="7.5">40.000</text>';
  s += '<text class="show-late" x="112" y="111" text-anchor="middle" font-size="6">MHz</text>';
  // SMD 0805 filter capacitors & resistor arrays
  for (const [x, y, w, hh] of [
    [148, 86, 12, 22], [148, 114, 12, 22],
    [238, 86, 12, 22], [238, 114, 12, 22],
    [320, 86, 14, 24], [320, 116, 14, 24],
    [110, 130, 24, 12], [266, 68, 22, 12]
  ]) {
    s += `<rect class="ln thin" x="${x}" y="${y}" width="${w}" height="${hh}" rx="2"/>`;
    s += `<line class="ln thin" x1="${x + w / 2}" y1="${y}" x2="${x + w / 2}" y2="${y + hh}"/>`;
  }
  // EN & BOOT tactile switches
  s += '<rect class="ln" x="58" y="22" width="36" height="46" rx="4"/>';
  s += '<rect class="ln thin" x="65" y="29" width="22" height="32" rx="2"/>';
  s += '<text class="show-late" x="76" y="47" text-anchor="middle" font-size="8.5" font-weight="bold">EN</text>';
  s += '<rect class="ln" x="306" y="22" width="36" height="46" rx="4"/>';
  s += '<rect class="ln thin" x="313" y="29" width="22" height="32" rx="2"/>';
  s += '<text class="show-late" x="324" y="47" text-anchor="middle" font-size="8.5" font-weight="bold">BOOT</text>';
  s += '</g>';

  // 4. ICs: CP2102 USB-UART & AMS1117 3.3V LDO (Step 3)
  s += '<g id="g-ics">';
  // CP2102 IC
  s += '<rect class="ln fillable" id="ic-uart" x="170" y="86" width="60" height="58" rx="3"/>';
  s += '<circle class="ln thin" cx="178" cy="94" r="2.5"/>';
  s += '<text class="show-late" x="200" y="116" text-anchor="middle" font-size="9" font-weight="bold">CP2102</text>';
  s += '<text class="show-late" x="200" y="128" text-anchor="middle" font-size="6.5">USB-UART</text>';
  for (let i = 0; i < 7; i++) {
    const o = 175 + i * 7.5;
    s += `<line class="ln thin" x1="${o}" y1="80" x2="${o}" y2="86"/>`;
    s += `<line class="ln thin" x1="${o}" y1="144" x2="${o}" y2="150"/>`;
    const vo = 91 + i * 7.5;
    s += `<line class="ln thin" x1="164" y1="${vo}" x2="170" y2="${vo}"/>`;
    s += `<line class="ln thin" x1="230" y1="${vo}" x2="236" y2="${vo}"/>`;
  }
  // AMS1117 LDO Regulator
  s += '<rect class="ln fillable" id="ic-reg" x="264" y="88" width="46" height="52" rx="2"/>';
  s += '<path class="ln" d="M 272 88 V 76 H 302 V 88"/>'; // heatsink tab
  s += '<rect class="ln thin" x="270" y="140" width="6" height="12"/>';
  s += '<rect class="ln thin" x="284" y="140" width="6" height="12"/>';
  s += '<rect class="ln thin" x="298" y="140" width="6" height="12"/>';
  s += '<text class="show-late" x="287" y="112" text-anchor="middle" font-size="7.5" font-weight="bold">AMS1117</text>';
  s += '<text class="show-late" x="287" y="123" text-anchor="middle" font-size="6.5">3.3V LDO</text>';
  s += '</g>';

  // 5. ESP-WROOM-32 RF Shield Can (Step 3)
  s += '<g id="g-rf-can">';
  s += '<rect class="ln fillable" id="wroom-can" x="75" y="170" width="250" height="260" rx="8"/>';
  s += '<rect class="ln thin" x="83" y="178" width="234" height="244" rx="6"/>';
  // Castellation pads along left, right, bottom
  for (let i = 0; i < 14; i++) {
    const ty = 184 + i * 16;
    s += `<rect class="ln thin" x="67" y="${ty}" width="10" height="9"/>`;
    s += `<rect class="ln thin" x="323" y="${ty}" width="10" height="9"/>`;
  }
  for (let i = 0; i < 10; i++) {
    s += `<rect class="ln thin" x="${102 + i * 20}" y="428" width="9" height="10"/>`;
  }
  // Silkscreen engravings
  s += '<text class="show-late" x="200" y="235" text-anchor="middle" font-size="16" font-weight="bold" letter-spacing="1">ESP-WROOM-32</text>';
  s += '<text class="show-late" x="200" y="254" text-anchor="middle" font-size="9.5">ESPRESSIF SYSTEMS</text>';
  s += '<line class="ln thin" x1="110" y1="266" x2="290" y2="266"/>';
  s += '<text class="show-late" x="200" y="286" text-anchor="middle" font-size="8">FCC ID: 2AC7Z-ESPWROOM32</text>';
  s += '<text class="show-late" x="200" y="302" text-anchor="middle" font-size="8">KCC / CE / RoHS Compliant</text>';
  s += '<circle class="ln thin" cx="145" cy="340" r="13"/><text class="show-late" x="145" y="344" text-anchor="middle" font-size="8.5">CE</text>';
  s += '<rect class="ln thin" x="175" y="328" width="38" height="24" rx="4"/><text class="show-late" x="194" y="343" text-anchor="middle" font-size="7.5">Wi-Fi</text>';
  s += '<rect class="ln thin" x="222" y="328" width="32" height="24" rx="4"/><text class="show-late" x="238" y="343" text-anchor="middle" font-size="7.5">BT</text>';
  s += '<text class="show-late" x="200" y="386" text-anchor="middle" font-size="8.5" letter-spacing="0.5">Tensilica Xtensa Dual-Core 240MHz</text>';
  s += '</g>';

  // 6. Meander Inverted-F PCB Antenna (Step 3)
  s += '<g id="g-antenna">';
  s += '<rect class="ln thin fillable" id="antenna-bg" x="80" y="444" width="240" height="130" rx="4"/>';
  s += '<path class="ln ant-trace" d="M 106 564 V 466 H 294 V 564 H 280 V 478 H 262 V 552 H 248 V 478 H 230 V 552 H 216 V 478 H 198 V 552 H 184 V 478 H 166 V 552 H 152 V 478 H 134 V 552 H 120 V 478 H 106 Z"/>';
  s += '<line class="ln" x1="120" y1="466" x2="120" y2="438"/>';
  s += '<line class="ln" x1="140" y1="466" x2="140" y2="438"/>';
  s += '<text class="show-late" x="200" y="525" text-anchor="middle" font-size="9.5" letter-spacing="1.5">2.4GHz MIFA ANTENNA</text>';
  s += '</g>';

  // 7. Dual Header Strips - 19-Pin Rails (Step 4)
  s += '<g id="g-pins">';
  for (let i = 0; i < 19; i++) {
    const y = py(i);
    // Left pin
    s += `<circle class="pin pin-via" data-i="${i}" cx="28" cy="${y}" r="8"/>`;
    s += `<circle class="pin-hole" cx="28" cy="${y}" r="3.5"/>`;
    s += `<text class="pin-lbl" x="44" y="${y + 3.5}">${PIN_L[i]}</text>`;
    // Right pin
    s += `<circle class="pin pin-via" data-i="${i}" cx="372" cy="${y}" r="8"/>`;
    s += `<circle class="pin-hole" cx="372" cy="${y}" r="3.5"/>`;
    s += `<text class="pin-lbl" x="356" y="${y + 3.5}" text-anchor="end">${PIN_R[i]}</text>`;
  }
  s += '</g>';

  // 8. Switches & Indicators / LEDs (Step 5)
  s += '<g id="g-leds">';
  s += '<circle id="pwr-led" class="ln" cx="114" cy="56" r="4.5"/>';
  s += '<text class="show-late" x="114" y="69" text-anchor="middle" font-size="7">PWR</text>';
  s += '<circle id="gpio2-led" class="ln blinkled" cx="286" cy="56" r="4.5"/>';
  s += '<text class="show-late" x="286" y="69" text-anchor="middle" font-size="7">IO2</text>';
  s += '</g>';

  $('#bt-board').innerHTML = `<svg id="boot-board-svg" viewBox="0 0 400 620" xmlns="${NS}" role="img" aria-label="ESP32 DevKit V1 Board Schematic">${s}</svg>`;

  $$('#bt-board .ln').forEach(el => {
    let len = 400; try { len = Math.ceil(el.getTotalLength()) + 2; } catch (_) {}
    el.style.strokeDasharray = len;
    el.style.strokeDashoffset = len;
    el.dataset.len = len;
  });
  $$('#bt-board .show-late').forEach(t => { t.style.opacity = 0; });
}

function drawGroup(id, dur) {
  $$('#' + id + ' .ln').forEach((el, i) => {
    const d = reduced ? 0 : dur;
    el.style.transition = `stroke-dashoffset ${d}ms ease-out ${(i % 12) * (reduced ? 0 : dur / 40)}ms`;
    void el.getBoundingClientRect();
    el.style.strokeDashoffset = 0;
  });
}

function logLine(text, ok) {
  const pre = $('#bt-log');
  $$('.cur', pre).forEach(e => e.classList.remove('cur'));
  const line = h('div', { class: 'cur' });
  line.append('> ' + text);
  if (ok) line.append(' ', h('span', { class: 'ok', text: ok }));
  pre.append(line);
  while (pre.childNodes.length > 16) pre.removeChild(pre.firstChild);
}

function setProgress(p, stage) {
  $('#bt-fill').style.width = p + '%';
  $('#bt-pct').textContent = p + '%';
  if (stage) $('#bt-stage').textContent = stage;
}

function litPins() {
  $$('#g-pins .pin-via').forEach((p, idx) => {
    setTimeout(() => p.classList.add('lit'), reduced ? 0 : idx * 16);
  });
}

function blinkGpio2Led() {
  const led = $('#gpio2-led');
  if (led) {
    led.classList.add('flashing');
  }
}

async function runBoot() {
  buildBoard();
  $('#bt-log').replaceChildren();
  $('#bt-board').classList.remove('done');
  setProgress(0, 'POWER ON');

  const T = reduced ? 0.2 : 1.0;

  const timeline = [
    [0, () => {
      logLine('VECTRON-32 BIOS v2.0 // HARDWARE COLD BOOT');
      drawGroup('g-outline', 550);
      setProgress(15, 'POWER ON');
    }],
    [550, () => {
      logLine('PCB TOPOLOGY........... ESP32 DevKit V1 Outline & USB', 'OK');
      drawGroup('g-traces', 650);
      drawGroup('g-passives', 650);
      setProgress(35, 'CIRCUIT TRACE');
    }],
    [1200, () => {
      logLine('COPPER ROUTING......... 45° Chamfer Bus & Passives', 'ROUTED');
      logLine('SILICON BRIDGE......... CP2102 UART & AMS1117 3.3V LDO', 'STABLE');
      drawGroup('g-ics', 600);
      drawGroup('g-rf-can', 700);
      drawGroup('g-antenna', 700);
      $('#wroom-can').classList.add('lit');
      $$('#bt-board .show-late').forEach(t => t.style.opacity = .88);
      setProgress(65, 'MCU CORE');
    }],
    [1950, () => {
      logLine('ESP-WROOM-32 CAN....... RF Shield & Xtensa 240MHz', 'LOCKED');
      logLine('RF SUBSYSTEM........... 2.4GHz Meander MIFA Antenna', 'TUNED');
      logLine('38-PIN BREAKOUT........ Dual Header Rails Mapped', 'ARMED');
      litPins();
      setProgress(85, 'PIN MATRIX');
    }],
    [2500, () => {
      logLine('GPIO 2 STATUS LED...... HEARTBEAT LINK VERIFIED', 'ONLINE');
      logLine('SYSTEM READY........... INITIALIZATION COMPLETE', 'OK');
      blinkGpio2Led();
      setProgress(100, 'SYSTEM READY');
      $('#bt-board').classList.add('done');
    }],
    [2850, () => {
      // Complete
    }]
  ];

  const t0 = performance.now();
  for (const [ms, fn] of timeline) {
    const wait = ms * T - (performance.now() - t0);
    if (wait > 0) await sleep(wait);
    fn();
  }
  await sleep(400 * T);
}

// Access-granted noise helper
function fillNoise() {
  const hex = () => Math.floor(Math.random() * 65536).toString(16).padStart(4, '0').toUpperCase();
  const rows = [];
  for (let i = 0; i < 26; i++) rows.push(Array.from({ length: 22 }, hex).join(' '));
  $('#ag-noise').textContent = rows.join('\n');
}

async function runSequence() {
  login.stopFx();
  fillNoise();
  await show('granted');
  await sleep(reduced ? 600 : 1200);
  await show('boot');
  await runBoot();
  await show('dash');
}

// ============================================================
// 6. WEBSOCKET TRANSPORT
// ============================================================
function wsConnect() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;
  addLog('INFO', 'WS connecting to ' + WS_URL);
  updateWsState('CONNECTING');
  ws = new WebSocket(WS_URL);

  ws.addEventListener('open', () => {
    wsConnected = true;
    wsReconnDelay = 1000;
    addLog('INFO', 'WebSocket connected');
    updateWsState('CONNECTED');
    banner(null);
  });

  ws.addEventListener('message', ev => {
    let data;
    try { data = JSON.parse(ev.data); } catch (_) { return; }
    if (data && data.t === 'telem') handleTelemetry(data);
  });

  ws.addEventListener('close', () => {
    wsConnected = false;
    addLog('WARN', 'WebSocket closed — reconnecting in ' + Math.round(wsReconnDelay / 1000) + 's');
    updateWsState('DISCONNECTED');
    if (!state.rebooting) {
      banner('WS DISCONNECTED — CHECK YOU ARE STILL ON THE VECTRON-32 WI-FI');
    }
    clearTimeout(wsReconnTimer);
    wsReconnTimer = setTimeout(() => {
      wsReconnDelay = Math.min(wsReconnDelay * 1.5, 15000);
      wsConnect();
    }, wsReconnDelay);
  });

  ws.addEventListener('error', () => {
    addLog('ERROR', 'WebSocket error');
  });
}

function wsSend(obj) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    addLog('WARN', 'WS not open — cannot send: ' + JSON.stringify(obj));
    return false;
  }
  ws.send(JSON.stringify(obj));
  return true;
}

function updateWsState(s) {
  const el = $('#ro-ws-state');
  if (el) el.textContent = s;
}

// ============================================================
// 7. TELEMETRY HANDLER
// ============================================================
function handleTelemetry(d) {
  state.telem = d;

  // System telemetry panel
  setK('uptime',    fmtUptime(d.up || 0));
  setK('heap',      kb(d.heap || 0));
  setK('heap-min',  kb(d.heapMin || 0));
  setK('heap-frag', (d.heapFrag != null ? d.heapFrag.toFixed(1) : '--') + '%');
  setK('temp',      d.temp != null ? d.temp.toFixed(1) + ' °C' : 'N/A');
  setK('ws-clients', String(d.clients || 0));
  setK('watermark',  kb(d.watermark || 0));

  // FreeRTOS & Memory Health Card
  setK('net-watermark', (d.netWatermark || 4096) + ' B');
  setK('tel-watermark', (d.telWatermark || d.watermark || 4096) + ' B');
  setK('rtos-frag',     (d.heapFrag != null ? Number(d.heapFrag).toFixed(1) : '--') + '%');
  setK('rtos-cpu',      (d.cpuMHz || 240) + ' MHz');

  // LittleFS usage progress bar
  if (d.fsTotal && d.fsTotal > 0) {
    const used = d.fsUsed || 0;
    const total = d.fsTotal;
    const pct = Math.min(100, Math.max(0, Math.round((used / total) * 100)));
    const bar = $('#fs-bar-fill');
    if (bar) bar.style.width = pct + '%';
    const meta = $('#fs-meta-text');
    if (meta) meta.textContent = `${kb(used)} / ${kb(total)} (${pct}%)`;
  }

  // Live readout
  const ru = $('#ro-uptime'); if (ru) ru.textContent = fmtUptime(d.up || 0);
  const rl = $('#ro-link');    if (rl) { rl.textContent = 'ONLINE'; }
  const rh = $('#ro-heap');    if (rh) rh.textContent = kb(d.heap || 0);
  const rt = $('#ro-temp');    if (rt) rt.textContent = (d.temp != null ? d.temp.toFixed(1) : '--') + ' °C';

  // d-link indicator
  const dl = $('#d-link');
  if (dl) { dl.classList.remove('bad'); $('#d-link-text').textContent = 'LINK OK'; }

  // Oscilloscope feed
  osc.push(d.heap || 0, d.temp || 0, d.clients || 0);

  // GPIO
  if (d.gpio) {
    renderGpioOutputs(d.gpio.out || []);
    renderGpioInputs(d.gpio.in  || []);
  }

  // I2C
  if (d.i2c) renderI2c(d.i2c);

  // BLE
  if (d.ble) {
    setK('ble-name', d.ble.name || '--');
    setK('ble-adv',  d.ble.adv  ? 'YES' : 'NO');
    setK('ble-conn', String(d.ble.conn || 0));
  }

  // WiFi
  if (d.wifi) {
    setK('w-ssid', d.wifi.ssid || '--');
    setK('w-ip',   d.wifi.ip   || '--');
    setK('w-ch',   String(d.wifi.ch || '--'));
    setK('w-clients', String(d.wifi.clients || 0));
    setK('w-tx',   d.wifi.txPower || '--');
    $$('#tx-chips .chip').forEach(c => c.classList.toggle('on', +c.dataset.level === d.wifi.txLevel));
  }

  // Update Tab 2: Silicon Memory Map
  updateMemoryMap(d);

  // Update Tab 3: Board & Pinout Inspector
  updatePinoutInspector(d);

  banner(null);
}

const setK = (k, v) => { const e = $(`[data-k="${k}"]`); if (e) e.textContent = v; };

// ============================================================
// 8. OSCILLOSCOPE
// ============================================================
const SAMPLES = 600;   // 60 s @ 10 Hz (telemetry can come faster but we cap at 600)

const osc = {
  heap:    new Float32Array(SAMPLES),
  temp:    new Float32Array(SAMPLES),
  clients: new Float32Array(SAMPLES),
  ptr: 0, count: 0,
  raf: 0, running: false,

  push(heap, temp, clients) {
    this.heap[this.ptr]    = heap;
    this.temp[this.ptr]    = temp;
    this.clients[this.ptr] = clients;
    this.ptr = (this.ptr + 1) % SAMPLES;
    if (this.count < SAMPLES) this.count++;
  },

  start() {
    if (this.running) return;
    this.running = true;
    const loop = () => {
      if (!this.running) return;
      this.draw();
      this.raf = requestAnimationFrame(loop);
    };
    this.raf = requestAnimationFrame(loop);
  },

  stop() {
    this.running = false;
    cancelAnimationFrame(this.raf);
  },

  draw() {
    const cv = $('#telemetry-chart') || $('#osc-canvas');
    if (!cv) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const W = cv.clientWidth, H = cv.clientHeight;
    if (cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)) {
      cv.width  = Math.round(W * dpr);
      cv.height = Math.round(H * dpr);
    }
    const ctx = cv.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);

    const n = this.count;
    if (n < 2) {
      ctx.fillStyle = 'rgba(0,255,136,.15)';
      ctx.font = '11px monospace';
      ctx.fillText('AWAITING DATA...', 8, H / 2);
      return;
    }

    // Draw oscilloscope grid (dark green dashed lines, 10 vertical x 8 horizontal divisions)
    ctx.save();
    ctx.strokeStyle = 'rgba(0, 255, 136, 0.15)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);

    for (let gx = 0; gx <= 10; gx++) {
      const x = (gx / 10) * W;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
    }
    for (let gy = 0; gy <= 8; gy++) {
      const y = (gy / 8) * H;
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
    }
    ctx.restore();

    // Build ordered arrays from ring buffer
    const ordered = (buf) => {
      const out = new Float32Array(n);
      const start = (this.ptr - n + SAMPLES) % SAMPLES;
      for (let i = 0; i < n; i++) out[i] = buf[(start + i) % SAMPLES];
      return out;
    };

    const heapArr    = ordered(this.heap);
    const tempArr    = ordered(this.temp);
    const clientArr  = ordered(this.clients);

    // Auto-scale each trace independently, scaled to virtual units for common Y axis
    // Heap -> raw bytes, Temp -> *1000 for visual scaling, Clients -> *10000
    const heapV    = heapArr;
    const tempV    = new Float32Array(n); for (let i = 0; i < n; i++) tempV[i]   = tempArr[i]   * 1000;
    const clientV  = new Float32Array(n); for (let i = 0; i < n; i++) clientV[i] = clientArr[i] * 10000;

    const allVals = [...heapV, ...tempV, ...clientV];
    let yMin = Math.min(...allVals), yMax = Math.max(...allVals);
    if (yMax === yMin) { yMax = yMin + 1; }
    const yRange = yMax - yMin;
    const toY = v => H - ((v - yMin) / yRange) * (H * 0.78) - H * 0.10;

    const drawTrace = (arr, color) => {
      ctx.save();
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.8;
      ctx.shadowColor = color;
      ctx.shadowBlur = 4;
      ctx.beginPath();
      for (let i = 0; i < n; i++) {
        const x = (i / (SAMPLES - 1)) * W;
        const y = toY(arr[i]);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
      ctx.restore();
    };

    drawTrace(heapV,   '#00ff88');
    drawTrace(tempV,   '#ffaa00');
    drawTrace(clientV, '#00e5ff');

    // In-canvas top-right HUD legend with live numeric values
    const latestHeap = heapArr[n - 1];
    const latestTemp = tempArr[n - 1];
    const latestCli  = clientArr[n - 1];

    ctx.save();
    const hudW = 124, hudH = 50;
    const hudX = W - hudW - 8, hudY = 8;
    ctx.fillStyle = 'rgba(5, 14, 8, 0.85)';
    ctx.strokeStyle = 'rgba(0, 255, 136, 0.35)';
    ctx.lineWidth = 1;
    ctx.fillRect(hudX, hudY, hudW, hudH);
    ctx.strokeRect(hudX, hudY, hudW, hudH);

    ctx.font = '9px monospace';
    ctx.fillStyle = '#00ff88';
    ctx.fillText(`HEAP: ${(latestHeap / 1024).toFixed(1)} KB`, hudX + 8, hudY + 14);

    ctx.fillStyle = '#ffaa00';
    ctx.fillText(`TEMP: ${latestTemp.toFixed(1)} °C`, hudX + 8, hudY + 28);

    ctx.fillStyle = '#00e5ff';
    ctx.fillText(`CLI:  ${Math.round(latestCli)}`, hudX + 8, hudY + 42);
    ctx.restore();

    // Y-axis labels
    ctx.fillStyle = 'rgba(0,255,136,.45)'; ctx.font = '10px monospace';
    ctx.fillText(Math.round(yMax / 1024) + 'K', 4, 14);
    ctx.fillText(Math.round(yMin / 1024) + 'K', 4, H - 4);

    // Time labels
    ctx.fillStyle = 'rgba(0,255,136,.3)';
    ctx.fillText('-60s', 24, H - 4);
    ctx.fillText('now', W - 32, H - 4);
  },
};

// ============================================================
// 9. GPIO PANEL
// ============================================================
function renderGpioOutputs(list) {
  const sig = list.map(o => [o.id, o.pin, o.name, o.blocked].join(':')).join('|');
  const container = $('#gpio-outputs');
  if (!container) return;

  if (sig !== state.outSig) {
    state.outSig = sig;
    if (!list.length) {
      container.innerHTML = '<div class="gpio-empty">No outputs configured</div>';
      return;
    }
    container.replaceChildren(...list.map(buildOutputRow));
  }

  // Update toggle states without rebuild
  for (const o of list) {
    const row = container.querySelector(`[data-out-id="${o.id}"]`); if (!row) continue;
    const cb = row.querySelector('input[type="checkbox"]');
    if (cb && !cb.dataset.busy) cb.checked = !!o.on;
    row.classList.toggle('blocked', !!o.blocked);
    const meta = row.querySelector('.gpio-meta');
    if (meta) meta.textContent = `GPIO${String(o.pin).padStart(2,'0')} · ${o.blocked ? 'BLOCKED' : (o.on ? 'HIGH' : 'LOW')}`;
  }
}

function buildOutputRow(o) {
  const row = h('div', { class: 'gpio-row' + (o.blocked ? ' blocked' : ''), 'data-out-id': String(o.id) });

  const info = h('div', { class: 'gpio-info' });
  info.append(
    h('div', { class: 'gpio-name', text: o.name }),
    h('div', { class: 'gpio-meta', text: `GPIO${String(o.pin).padStart(2,'0')} · ${o.blocked ? 'BLOCKED' : (o.on ? 'HIGH' : 'LOW')}` })
  );

  const label = h('label', { class: 'toggle-switch' });
  const cb = h('input', { type: 'checkbox' });
  cb.checked = !!o.on;
  if (o.blocked) cb.disabled = true;
  const slider = h('span', { class: 'slider' });
  const pinName = h('span', { class: 'pin-name', text: o.on ? 'ON' : 'OFF' });

  cb.addEventListener('change', () => {
    const newState = cb.checked ? 1 : 0;
    pinName.textContent = cb.checked ? 'ON' : 'OFF';
    cb.dataset.busy = '1';
    addLog('INFO', `GPIO cmd: id=${o.id} pin=${o.pin} state=${newState}`);
    wsSend({ action: 'gpio', cmd: 'gpio', pin: o.pin, id: o.id, state: newState });
    setTimeout(() => { delete cb.dataset.busy; }, 500);
  });

  label.append(cb, slider, pinName);
  row.append(info, label);
  return row;
}

function renderGpioInputs(list) {
  const sig = list.map(i => i.name + i.pin).join('|');
  const container = $('#gpio-inputs');
  if (!container) return;

  if (sig !== state.inSig) {
    state.inSig = sig;
    if (!list.length) {
      container.innerHTML = '<div class="gpio-empty">No inputs configured</div>';
      return;
    }
    container.replaceChildren(...list.map(buildInputRow));
  }

  for (const i of list) {
    const row = container.querySelector(`[data-in-name="${i.name}"]`); if (!row) continue;
    const led = row.querySelector('.gpio-in-led');
    const st  = row.querySelector('.gpio-in-state');
    if (led) led.classList.toggle('active', !!i.active);
    if (st)  { st.textContent = i.active ? '[ ACTIVE ]' : '[ idle ]'; st.classList.toggle('active', !!i.active); }
  }
}

function buildInputRow(i) {
  const row = h('div', { class: 'gpio-in-row', 'data-in-name': i.name });
  const led  = h('div', { class: 'gpio-in-led' + (i.active ? ' active' : '') });
  const info = h('div', { class: 'gpio-in-info' });
  info.append(
    h('div', { class: 'gpio-in-name', text: i.name }),
    h('div', { class: 'gpio-in-meta', text: `GPIO${String(i.pin).padStart(2,'0')} · READ-ONLY` })
  );
  const st = h('div', { class: 'gpio-in-state' + (i.active ? ' active' : ''), text: i.active ? '[ ACTIVE ]' : '[ idle ]' });
  row.append(led, info, st);
  return row;
}

// ============================================================
// 10. I2C PANEL
// ============================================================
function renderI2c(i2c) {
  const meta = $('#i2c-meta');
  if (meta) meta.textContent = `SDA GPIO${i2c.sda} / SCL GPIO${i2c.scl}`;

  const stateEl = $('#i2c-state');
  if (stateEl) {
    stateEl.textContent = i2c.state === 'scanning' ? '[ SCANNING... ]' : (i2c.state === 'done' ? '[ DONE ]' : '');
    stateEl.classList.toggle('scanning', i2c.state === 'scanning');
  }

  const btn = $('#btn-i2c');
  if (btn) { btn.disabled = i2c.state === 'scanning'; btn.classList.toggle('busy', i2c.state === 'scanning'); }

  if (i2c.state === 'done') {
    addLog('BUS', `I2C scan done: ${i2c.found.length} device(s) found`);
    const map = $('#i2c-map');
    if (!map) return;
    if (!i2c.found.length) {
      map.innerHTML = '<div class="gpio-empty">No I2C devices found. Check wiring &amp; pull-ups.</div>';
      return;
    }
    map.replaceChildren(...i2c.found.map(dev => {
      const card = h('div', { class: 'i2c-chip' });
      const addrHex = '0x' + (dev.addr || 0).toString(16).toUpperCase().padStart(2, '0');
      card.append(
        h('div', { class: 'i2c-chip-addr', text: addrHex }),
        h('div', { class: 'i2c-chip-name', text: dev.name || 'UNKNOWN' }),
        h('div', { class: 'i2c-chip-desc', text: dev.desc || '—' })
      );
      return card;
    }));
  }
}

// ============================================================
// 10B. SILICON MEMORY MAP UPDATER
// ============================================================
function updateMemoryMap(d) {
  // SRAM / DRAM heap metrics
  const heapTotal = d.heapTotal || 327680; // 320 KB default
  const heapFree  = d.heap || 0;
  const heapUsed  = Math.max(0, heapTotal - heapFree);
  const heapPct   = Math.min(100, Math.max(0, Math.round((heapUsed / heapTotal) * 100)));

  setK('mem-heap-total', kb(heapTotal));
  setK('mem-heap-free',  kb(heapFree));
  setK('mem-heap-used',  kb(heapUsed));
  setK('mem-heap-min',   kb(d.heapMin || 0));
  setK('mem-heap-max',   kb(d.heapMax || Math.round(heapFree * 0.65)));
  setK('mem-heap-frag',  (d.heapFrag != null ? Number(d.heapFrag).toFixed(1) : '--') + ' %');

  const heapBar = $('#mem-heap-bar');
  if (heapBar) heapBar.style.width = heapPct + '%';
  const heapPctEl = $('#mem-heap-pct');
  if (heapPctEl) heapPctEl.textContent = heapPct + '%';
  const heapDetail = $('#mem-heap-detail');
  if (heapDetail) heapDetail.textContent = `${kb(heapFree)} FREE / ${kb(heapTotal)} TOTAL`;

  // Application Partition (huge_app 3.1 MB = 3145728 B)
  const appTotal = 3145728;
  const appUsed  = d.sketchBytes || 1228800; // ~1.2 MB default
  const appPct   = Math.min(100, Math.max(0, Math.round((appUsed / appTotal) * 100)));
  const appBar   = $('#mem-app-bar');
  if (appBar) appBar.style.width = appPct + '%';
  const appPctEl = $('#mem-app-pct');
  if (appPctEl) appPctEl.textContent = appPct + '%';
  const appText  = $('#mem-app-text');
  if (appText) appText.textContent = `${kb(appUsed)} USED / ${kb(appTotal)} ALLOCATED`;

  // LittleFS Partition (1.0 MB = 1048576 B)
  const fsTotal = d.fsTotal || 1048576;
  const fsUsed  = d.fsUsed || 0;
  const fsPct   = Math.min(100, Math.max(0, Math.round((fsUsed / fsTotal) * 100)));
  const fsBar   = $('#mem-fs-bar');
  if (fsBar) fsBar.style.width = fsPct + '%';
  const fsPctEl = $('#mem-fs-pct');
  if (fsPctEl) fsPctEl.textContent = fsPct + '%';
  const fsText  = $('#mem-fs-text');
  if (fsText) fsText.textContent = `${kb(fsUsed)} USED / ${kb(fsTotal)} ALLOCATED`;

  // FreeRTOS Task Headroom
  const netWm = d.netWatermark || 4096;
  const telWm = d.telWatermark || d.watermark || 4096;
  const taskNetVal = $('#mem-task-net-val');
  if (taskNetVal) taskNetVal.textContent = `${netWm} B REMAINING`;
  const taskTelVal = $('#mem-task-tel-val');
  if (taskTelVal) taskTelVal.textContent = `${telWm} B REMAINING`;

  const taskNetBar = $('#mem-task-net-bar');
  if (taskNetBar) taskNetBar.style.width = Math.min(100, Math.max(5, Math.round((netWm / 8192) * 100))) + '%';
  const taskTelBar = $('#mem-task-tel-bar');
  if (taskTelBar) taskTelBar.style.width = Math.min(100, Math.max(5, Math.round((telWm / 8192) * 100))) + '%';

  setK('mem-cpu', `${d.cpuMHz || 240} MHz (Xtensa LX6)`);
}

// ============================================================
// 10C. BOARD & PINOUT INSPECTOR UPDATER
// ============================================================
function updatePinoutInspector(d) {
  if (d.gpio && d.gpio.out) {
    for (const o of d.gpio.out) {
      const pinNum = o.pin != null ? o.pin : (o.id === 0 ? 2 : (o.id === 1 ? 4 : 16));
      const isOn = !!o.on;

      // Update SVG pin element
      const svgPin = $(`#p-${pinNum}`);
      if (svgPin) svgPin.classList.toggle('on', isOn);

      // Status LED (GPIO 2)
      if (pinNum === 2) {
        const ledD2 = $('#svg-led-d2');
        if (ledD2) {
          ledD2.setAttribute('fill', isOn ? '#ffffff' : '#00ff66');
          ledD2.style.filter = isOn ? 'url(#neon-glow)' : 'none';
        }
      }

      // Update table cell
      const tblState = $(`#tbl-state-gpio${pinNum}`);
      if (tblState) {
        tblState.innerHTML = isOn
          ? `<span class="chip-val chip-high">HIGH (ON)</span>`
          : `<span class="chip-val chip-low">LOW (OFF)</span>`;
      }
    }
  }

  if (d.gpio && d.gpio.in) {
    for (const inp of d.gpio.in) {
      const pinNum = inp.pin != null ? inp.pin : 0;
      const isActive = !!inp.active;

      const svgPin = $(`#p-${pinNum}`);
      if (svgPin) svgPin.classList.toggle('active', isActive);

      if (pinNum === 0) {
        const bootBtn = $('#svg-btn-boot');
        if (bootBtn) bootBtn.classList.toggle('active', isActive);
        const bootCircle = $('#svg-circle-boot');
        if (bootCircle) bootCircle.setAttribute('fill', isActive ? '#ffffff' : '#003314');

        const tblBoot = $('#tbl-state-gpio0');
        if (tblBoot) {
          tblBoot.innerHTML = isActive
            ? `<span class="chip-val chip-active">ACTIVE (LOW)</span>`
            : `<span class="chip-val chip-idle">IDLE (HIGH)</span>`;
        }
      }
    }
  }
}

// ============================================================
// 10D. VIRTUAL TABS & PIN INTERACTION WIRING
// ============================================================
function wireTabs() {
  $$('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const target = btn.dataset.tab;
      $$('.tab-btn').forEach(b => {
        const isSel = (b === btn);
        b.classList.toggle('active', isSel);
        b.setAttribute('aria-selected', isSel ? 'true' : 'false');
      });
      $$('.tab-view').forEach(v => {
        v.classList.toggle('active', v.id === `view-${target}`);
      });
      if (target === 'scada') {
        osc.start();
      }
    });
  });
}

function wirePinHover() {
  $$('.svg-pin').forEach(pin => {
    pin.addEventListener('mouseenter', () => {
      const id = pin.id;
      const num = id.replace('p-', '');
      const row = $(`#tbl-row-gpio${num}`);
      if (row) row.classList.add('highlight');
    });
    pin.addEventListener('mouseleave', () => {
      const id = pin.id;
      const num = id.replace('p-', '');
      const row = $(`#tbl-row-gpio${num}`);
      if (row) row.classList.remove('highlight');
    });
  });

  $$('.pin-table tbody tr').forEach(row => {
    row.addEventListener('click', () => {
      const id = row.id;
      if (!id) return;
      const num = id.replace('tbl-row-gpio', '');
      const svgPin = $(`#p-${num}`);
      if (svgPin) {
        svgPin.style.filter = 'url(#neon-glow)';
        setTimeout(() => { svgPin.style.filter = ''; }, 1200);
      }
    });
  });
}

// ============================================================
// 11. DIAGNOSTICS CONSOLE
// ============================================================
let diagCount = 0;
const MAX_DIAG = 200;

function addLog(level, msg) {
  const console_el = $('#diag-console');
  if (!console_el) return;

  const now = new Date();
  const ts = pad2(now.getHours()) + ':' + pad2(now.getMinutes()) + ':' + pad2(now.getSeconds());
  const line = h('div', { class: 'diag-line', 'data-level': level });
  line.textContent = `[${ts}] [${level}] ${msg}`;
  if (!state.diagFilters.has(level)) line.classList.add('hidden');

  console_el.append(line);
  diagCount++;

  // Trim old entries
  while (console_el.children.length > MAX_DIAG) {
    console_el.removeChild(console_el.firstChild);
  }

  // Auto-scroll to bottom
  console_el.scrollTop = console_el.scrollHeight;
}

function wireDiagFilters() {
  $$('.diag-filter').forEach(btn => {
    btn.addEventListener('click', () => {
      const filter = btn.dataset.filter;
      btn.classList.toggle('active');
      if (btn.classList.contains('active')) {
        state.diagFilters.add(filter);
      } else {
        state.diagFilters.delete(filter);
      }
      // Show/hide existing lines
      $$(`#diag-console .diag-line[data-level="${filter}"]`).forEach(line => {
        line.classList.toggle('hidden', !state.diagFilters.has(filter));
      });
    });
  });
}

// ============================================================
// 12. CONTROL HANDLERS
// ============================================================
const dashboard = {
  onEnter() {
    osc.start();
    wsConnect();
    addLog('INFO', 'Dashboard entered — WS transport active');
  },
  onLeave() {
    osc.stop();
    if (ws) { ws.close(); ws = null; }
    clearTimeout(wsReconnTimer);
    state.outSig = ''; state.inSig = '';
    state.telem = null;
  },
};

function wireTxPower() {
  $$('#tx-chips .chip').forEach(c => {
    c.addEventListener('click', () => {
      if (c.disabled) return;
      addLog('INFO', `TX power set to level ${c.dataset.level}`);
      wsSend({ cmd: 'txpower', level: +c.dataset.level });
    });
  });
}

function wireI2cScan() {
  $('#btn-i2c').addEventListener('click', () => {
    addLog('BUS', 'I2C scan requested');
    wsSend({ cmd: 'i2c_scan' });
  });
}

function wireReboot() {
  const y = $('#btn-reboot-y'), n = $('#btn-reboot-n'), yn = $('#reboot-yn');
  const cd = $('#reboot-countdown');
  let armed = false;

  const disarm = () => {
    armed = false; y.textContent = '[y]'; n.hidden = true;
    yn.textContent = '[ y / n ]';
  };

  n.addEventListener('click', disarm);

  y.addEventListener('click', () => {
    if (!armed) {
      armed = true; y.textContent = '[y] CONFIRM'; n.hidden = false;
      yn.textContent = 'REBOOT VECTRON-32?';
      return;
    }
    disarm();
    addLog('WARN', 'Reboot command sent');
    wsSend({ cmd: 'reboot' });
    state.rebooting = true;
    banner('REBOOTING — VECTRON-32 WILL RECONNECT SHORTLY...');
    cd.hidden = false;
    let secs = 10;
    cd.textContent = 'RECONNECTING IN ' + secs + 's...';
    const t = setInterval(() => {
      secs--;
      if (secs <= 0) { clearInterval(t); cd.hidden = true; state.rebooting = false; }
      else cd.textContent = 'RECONNECTING IN ' + secs + 's...';
    }, 1000);
  });
}

function wireLogout() {
  $('#btn-logout').addEventListener('click', async () => {
    try { await api('/api/logout', { method: 'POST' }); } catch (_) {}
    sessionLost('SESSION CLOSED');
  });
}

function banner(msg, type) {
  const b = $('#d-banner');
  if (!b) return;
  if (!msg) { b.hidden = true; return; }
  b.textContent = '! ' + msg;
  b.className = 'd-banner' + (type ? ' ' + type : '');
  b.hidden = false;
}

// ============================================================
// 13. SESSION MANAGEMENT
// ============================================================
let leaving = false;

async function sessionLost(msg) {
  if (leaving) return; leaving = true;
  dashboard.onLeave();
  banner(null);
  await show('login');
  login.lines = [];
  logSet('LINK', 'OK');
  logSet('AUTH', msg, msg === 'SESSION CLOSED' ? '' : 'bad');
  setLed('led-auth', '');
  setBtn('[ AUTHENTICATE ]', false);
  leaving = false;
  const inUser = $('#in-user');
  if (inUser) {
    if (!inUser.value) inUser.value = 'demo';
    inUser.focus();
  }
  const inPass = $('#in-pass');
  if (inPass && !inPass.value) inPass.value = 'demo';
}

// ============================================================
// 14. INIT
// ============================================================
async function init() {
  const hostEl = $('#lf-host');
  if (hostEl) hostEl.textContent = location.host || '192.168.4.1';
  const loginForm = $('#login-form');
  if (loginForm) loginForm.addEventListener('submit', onLoginSubmit);
  wireDiagFilters();
  wireTxPower();
  wireI2cScan();
  wireReboot();
  wireLogout();
  wireTabs();
  wirePinHover();

  window.addEventListener('resize', () => {
    if (login.active) drawLoginCanvas(performance.now());
  });

  // Check existing session
  let authed = false;
  try {
    const r = await api('/api/session', { timeout: 3500 });
    authed = !!(r.ok && r.data && r.data.authenticated);
  } catch (_) { /* offline — show login */ }

  if (authed) {
    await show('dash');
  } else {
    logSet('SYS', 'INITIALIZED');
    logSet('LINK', 'ONLINE');
    logSet('AUTH', 'STANDBY');
    await show('login');
    const u = $('#in-user');
    if (u) {
      if (!u.value) u.value = 'demo';
      u.focus();
    }
    const p = $('#in-pass');
    if (p && !p.value) p.value = 'demo';
  }
}

init();

})();
