// VECTRON-32 UI mock server - lets you work on data/ in a browser WITHOUT an ESP32.
//
//   node tools/mock-server.js          then open  http://localhost:8099/
//   login:  demo / demo
//
// It imitates the routes in src/api.cpp with real-time WebSocket telemetry.
// It uses only Node.js built-ins (zero npm packages required).
// Override with env vars: MOCK_PORT, MOCK_USER, MOCK_PASS.
'use strict';
const http = require('http'), fs = require('fs'), path = require('path'), crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', 'data');
const PORT = +process.env.MOCK_PORT || 8099;
const USER = process.env.MOCK_USER || 'demo';
const PASS = process.env.MOCK_PASS || 'demo';
const MIME = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.woff2': 'font/woff2', '.png': 'image/png', '.svg': 'image/svg+xml' };

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

const server = http.createServer((req, res) => {
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
});

// Real-time WebSocket support for mock server (RFC 6455)
server.on('upgrade', (req, socket) => {
  if (req.url === '/ws') {
    const key = req.headers['sec-websocket-key'];
    if (!key) { socket.destroy(); return; }
    const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + '\r\n\r\n');

    const send = (obj) => {
      const data = Buffer.from(JSON.stringify(obj));
      let header;
      if (data.length < 126) {
        header = Buffer.from([0x81, data.length]);
      } else if (data.length < 65536) {
        header = Buffer.alloc(4);
        header[0] = 0x81; header[1] = 126;
        header.writeUInt16BE(data.length, 2);
      } else {
        header = Buffer.alloc(10);
        header[0] = 0x81; header[1] = 127;
        header.writeBigUInt64BE(BigInt(data.length), 2);
      }
      try { socket.write(Buffer.concat([header, data])); } catch (_) {}
    };

    const interval = setInterval(() => {
      const up = Math.floor((Date.now() - t0) / 1000);
      const free = 210000 + Math.floor(Math.sin(up / 2) * 8000);
      send({
        t: "telem",
        up,
        heap: free,
        heapMin: 187000,
        heapMax: 110000,
        heapTotal: 327680,
        heapFrag: 8.5,
        temp: 42.4 + Math.sin(up / 4) * 0.8,
        rssi: -45,
        clients: 1,
        cpuMHz: 240,
        watermark: 4200,
        telWatermark: 4200,
        netWatermark: 3950,
        fsUsed: 98304,
        fsTotal: 1048576,
        sketchBytes: 1228800,
        flashBytes: 4194304,
        gpio: {
          out: [
            { id: 0, pin: 2, name: 'ONBOARD_LED', on: led, blocked: false },
            { id: 1, pin: 16, name: 'RELAY_1', on: false, blocked: false }
          ],
          in: [
            { id: 0, pin: 0, name: 'BOOT_BUTTON', active: false }
          ]
        },
        i2c: { state: scan, sda: 21, scl: 22, found: [{ addr: 0x3C, name: 'SSD1306', desc: 'OLED 128x64' }, { addr: 0x76, name: 'BME280', desc: 'Env sensor' }] },
        ble: { adv: ble, conn: 0, name: 'VECTRON-32' },
        wifi: { ssid: 'VECTRON-32', ip: '192.168.4.1', ch: 6, clients: 1, txLevel: tx, txPower: ['LOW', 'MED', 'HIGH'][tx] }
      });
    }, 100);

    socket.on('close', () => clearInterval(interval));
    socket.on('error', () => clearInterval(interval));
  }
});

server.listen(PORT, () => console.log(`VECTRON-32 mock UI on http://localhost:${PORT}/  (login: ${USER} / ${PASS})`));
