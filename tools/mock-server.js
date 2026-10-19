// VECTRON-32 UI mock server - lets you work on data/ in a browser WITHOUT an ESP32.
//
//   node tools/mock-server.js          then open  http://localhost:8099/
//   login:  demo / demo
//
// It imitates the routes in src/api.cpp with FAKE data.
// It uses only Node built-ins.
// Override with env vars: MOCK_PORT, MOCK_USER, MOCK_PASS.
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', 'data');
const PORT = +process.env.MOCK_PORT || 8099;
const USER = process.env.MOCK_USER || 'demo';
const PASS = process.env.MOCK_PASS || 'demo';
const MIME = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.woff2': 'font/woff2' };

const sessions = new Set();
let fails = 0, lockUntil = 0;
let led = false, ble = false, tx = 2, scan = 'idle', scanAt = 0, found = [];
const t0 = Date.now();

const json = (res, code, obj, headers = {}) => {
  res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...headers });
  res.end(JSON.stringify(obj));
};
const authed = (req) => {
  const m = /VECTRON_SID=([0-9a-f]{32})/.exec(req.headers.cookie || '');
  return !!(m && sessions.has(m[1]));
};

http.createServer((req, res) => {
  let body = '';
  req.on('data', (d) => (body += d));
  req.on('end', () => {
    const p = new URLSearchParams(body);
    const u = req.url.split('?')[0];

    if (u === '/api/session') return json(res, 200, { ok: true, authenticated: authed(req), device: 'VECTRON-32' });

    if (u === '/api/login') {
      if (Date.now() < lockUntil) return json(res, 429, { ok: false, error: 'LOCKED', retryAfter: Math.ceil((lockUntil - Date.now()) / 1000) });
      if (p.get('user') === USER && p.get('pass') === PASS) {
        fails = 0;
        const t = crypto.randomBytes(16).toString('hex');
        sessions.add(t);
        return json(res, 200, { ok: true }, { 'Set-Cookie': `VECTRON_SID=${t}; Path=/; HttpOnly; SameSite=Strict` });
      }
      if (++fails >= 5) { fails = 0; lockUntil = Date.now() + 8000; return json(res, 429, { ok: false, error: 'LOCKED', retryAfter: 8 }); }
      return json(res, 401, { ok: false, error: 'ACCESS DENIED', attemptsLeft: 5 - fails });
    }

    if (u.startsWith('/api/')) {
      if (u === '/api/logout') return json(res, 200, { ok: true });
      if (!authed(req)) return json(res, 401, { ok: false, error: 'UNAUTHORIZED' });
      if (u === '/api/info') return json(res, 200, { ok: true, device: 'VECTRON-32', firmware: '2.0.0', chip: 'ESP32-D0WD-V3', revision: 3, cores: 2, cpuMHz: 240,
        flashBytes: 4194304, sketchBytes: 1200000, sdk: 'mock', mac: 'AA:BB:CC:DD:EE:FF', fsUsed: 98304, fsTotal: 1048576, outputs: 2, inputs: 1 });
      if (u === '/api/status') {
        if (scan === 'scanning' && Date.now() > scanAt) { scan = 'done'; found = [0x3c, 0x76]; }
        return json(res, 200, { ok: true, uptime: Math.floor((Date.now() - t0) / 1000) + 4231,
          heap: { free: 213456, min: 187000, maxBlock: 110000 }, tempC: null,
          wifi: { mode: 'AP', ssid: 'VECTRON-32', ip: '192.168.4.1', mac: 'AA:BB:CC:DD:EE:FF', channel: 6, clients: 1, maxClients: 4,
            txPower: ['LOW', 'MED', 'HIGH'][tx], txLevel: tx, stations: [{ mac: '12:34:56:78:9A:BC', rssi: -47 }] },
          bluetooth: { classic: { hardware: true, firmware: false }, ble: { hardware: true, firmware: true, advertising: ble, name: 'VECTRON-32', connections: 0 } },
          gpio: { outputs: [
              { id: 0, name: 'ONBOARD_LED', pin: 2, activeHigh: true, blocked: false, on: led, level: led ? 1 : 0 },
              { id: 1, name: 'RELAY_1', pin: 16, activeHigh: false, blocked: false, on: false, level: 1 } ],
            inputs: [{ name: 'BOOT_BUTTON', pin: 0, active: false }] },
          i2c: { state: scan, sda: 21, scl: 22, found } });
      }
      if (u === '/api/gpio') { if (p.get('id') === '0') led = p.get('state') === '1'; return json(res, 200, { ok: true }); }
      if (u === '/api/bluetooth') { setTimeout(() => (ble = p.get('state') === '1'), 500); return json(res, 202, { ok: true, pending: true }); }
      if (u === '/api/wifi/txpower') { tx = +p.get('level'); return json(res, 200, { ok: true }); }
      if (u === '/api/i2c/scan') { scan = 'scanning'; scanAt = Date.now() + 1500; return json(res, 202, { ok: true, pending: true }); }
      if (u === '/api/reboot') return json(res, 200, { ok: true });
      return json(res, 404, { ok: false, error: 'NOT_FOUND' });
    }

    const f = path.join(ROOT, u === '/' ? 'index.html' : u);
    if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end('404'); }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream' });
    fs.createReadStream(f).pipe(res);
  });
}).listen(PORT, () => console.log(`VECTRON-32 mock UI on http://localhost:${PORT}/  (login: ${USER} / ${PASS})`));
