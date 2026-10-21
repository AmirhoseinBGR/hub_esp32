'use strict';

const path = require('node:path');
const crypto = require('node:crypto');
const http = require('node:http');
const express = require('express');
const { WebSocket, WebSocketServer } = require('ws');
require('dotenv').config({ path: path.join(__dirname, '.env') });

const COOKIE_NAME = 'evohatch_session';
const SESSION_TTL_SECONDS = 8 * 60 * 60;
const MAX_FRAME_BYTES = 32 * 1024;
const MAX_GSM_LOGS = 240;
const VALID_RATES = new Set([1, 2, 3, 4, 6, 12, 20, 30, 60]);

class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function safeEqual(left, right) {
  const a = Buffer.from(String(left ?? ''), 'utf8');
  const b = Buffer.from(String(right ?? ''), 'utf8');
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

function readConfig(env = process.env) {
  const config = {
    host: env.HOST || '0.0.0.0',
    port: Number(env.PORT || env.PLESK_NODE_PORT || 3000),
    publicOrigin: env.PUBLIC_ORIGIN ? new URL(env.PUBLIC_ORIGIN).origin : '',
    adminUsername: env.ADMIN_USERNAME || '',
    adminPassword: env.ADMIN_PASSWORD || '',
    sessionSecret: env.SESSION_SECRET || '',
    deviceId: env.DEVICE_ID || 'hub-01',
    deviceToken: env.DEVICE_TOKEN || '',
    deviceTimeoutMs: Math.max(5000, Number(env.DEVICE_TIMEOUT_MS || 15000)),
    commandTimeoutMs: Math.max(3000, Number(env.COMMAND_TIMEOUT_MS || 10000)),
    nodeEnv: env.NODE_ENV || 'development',
    publicDir: path.join(__dirname, 'public'),
  };

  if (!Number.isInteger(config.port) || config.port < 0 || config.port > 65535) {
    throw new Error('PORT must be a valid TCP port.');
  }
  if (!config.adminUsername || config.adminUsername.length > 64) {
    throw new Error('Set ADMIN_USERNAME in Plesk or plesk/.env.');
  }
  if (config.adminPassword.length < 12) {
    throw new Error('Set a unique ADMIN_PASSWORD with at least 12 characters.');
  }
  if (Buffer.byteLength(config.sessionSecret, 'utf8') < 32) {
    throw new Error('SESSION_SECRET must contain at least 32 bytes of random data.');
  }
  if (config.deviceToken.length < 32) {
    throw new Error('DEVICE_TOKEN must contain at least 32 random characters.');
  }
  if (!/^[A-Za-z0-9._-]{1,48}$/.test(config.deviceId)) {
    throw new Error('DEVICE_ID may contain only letters, numbers, dot, underscore, and dash.');
  }
  return config;
}

function defaultState() {
  return {
    link: { stm32: false, age_ms: 0 },
    wifi: {
      ssid: '', pass_set: false, auto: true, connected: false, ip: '',
      ap: 'ESP32_HUB_AP', phase: 0, err: 0, scanning: false, networks: [],
    },
    server: { domain: 'deuteriumStudio.ir', rate: 12, ok: false },
    sim: { ok: false, credit: 0, phones: ['', '', ''], mask: 0x00af },
    gsm: {
      seen: false, fsm: '', act: '', step: '', ready: false, sim: false,
      net: false, op: '', rssi: 0, bars: 0, sms_used: 0, sms_total: 0,
      paused: false, qsize: 0, qcap: 0, call: 'idle', peer: '',
    },
    devices: Array.from({ length: 16 }, (_, index) => ({
      id: index + 1, online: false, temp: 0, hum: 0, sp_t: 37.5,
      sp_h: 60, rot: 0, alarms: 0, en: 0x00af, paired: false,
    })),
  };
}

function copyJson(value) {
  return JSON.parse(JSON.stringify(value));
}

function parseCookies(header = '') {
  const cookies = Object.create(null);
  for (const item of header.split(';')) {
    const separator = item.indexOf('=');
    if (separator < 0) continue;
    const name = item.slice(0, separator).trim();
    const value = item.slice(separator + 1).trim();
    if (!name) continue;
    try { cookies[name] = decodeURIComponent(value); } catch { cookies[name] = value; }
  }
  return cookies;
}

function makeSession(username, secret) {
  const payload = Buffer.from(JSON.stringify({
    sub: username,
    exp: Math.floor(Date.now() / 1000) + SESSION_TTL_SECONDS,
  })).toString('base64url');
  const signature = crypto.createHmac('sha256', secret).update(payload).digest('base64url');
  return `${payload}.${signature}`;
}

function verifySession(cookie, secret) {
  if (typeof cookie !== 'string') return null;
  const dot = cookie.lastIndexOf('.');
  if (dot <= 0) return null;
  const payload = cookie.slice(0, dot);
  const signature = cookie.slice(dot + 1);
  const expected = crypto.createHmac('sha256', secret).update(payload).digest('base64url');
  if (!safeEqual(signature, expected)) return null;
  try {
    const data = JSON.parse(Buffer.from(payload, 'base64url').toString('utf8'));
    if (!data || typeof data.sub !== 'string' || !Number.isFinite(data.exp) || data.exp <= Date.now() / 1000) return null;
    return data;
  } catch {
    return null;
  }
}

function asyncRoute(handler) {
  return (req, res, next) => Promise.resolve(handler(req, res, next)).catch(next);
}

function normalizeSafeText(value, label, maxLength, { required = true, trim = true } = {}) {
  if (typeof value !== 'string') throw new HttpError(400, `${label} must be text.`);
  const text = trim ? value.trim() : value;
  if ((required && !text) || text.length > maxLength || /[|\u0000-\u001f\u007f]/.test(text)) {
    throw new HttpError(400, `${label} is missing, too long, or contains unsupported characters.`);
  }
  return text;
}

function toMask(value, label = 'mask') {
  const number = Number(value);
  if (!Number.isInteger(number) || number < 0 || number > 0xffff) {
    throw new HttpError(400, `${label} must be an integer between 0 and 65535.`);
  }
  return number;
}

function createHubServer(options = {}) {
  const config = options.config || readConfig(options.env || process.env);
  const app = express();
  const server = http.createServer(app);
  const wss = new WebSocketServer({ noServer: true, maxPayload: MAX_FRAME_BYTES });
  const browserClients = new Set();
  const pendingCommands = new Map();
  const loginAttempts = new Map();
  let deviceSocket = null;
  let lastSeen = 0;
  let lastState = defaultState();
  let scanPending = false;
  let gsmSequence = 0;
  let gsmLogs = [];
  let closed = false;

  app.disable('x-powered-by');
  // Plesk/IIS terminates HTTPS and forwards traffic to the Node process.
  app.set('trust proxy', 1);
  app.use((req, res, next) => {
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Referrer-Policy', 'strict-origin-when-cross-origin');
    res.setHeader('X-Frame-Options', 'DENY');
    res.setHeader('Permissions-Policy', 'camera=(), microphone=(), geolocation=()');
    res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self' ws: wss:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'; form-action 'self'");
    next();
  });
  app.use(express.json({ limit: '32kb', strict: true }));

  function isDeviceOnline() {
    return !!deviceSocket && deviceSocket.readyState === WebSocket.OPEN && Date.now() - lastSeen <= config.deviceTimeoutMs;
  }

  function currentState() {
    const result = copyJson(lastState);
    result.cloud = {
      enabled: true,
      connected: isDeviceOnline(),
      device_id: config.deviceId,
      last_seen: lastSeen ? new Date(lastSeen).toISOString() : null,
      last_seen_ms: lastSeen ? Math.max(0, Date.now() - lastSeen) : null,
    };
    if (result.wifi && scanPending) result.wifi.scanning = true;
    return result;
  }

  function sendJson(socket, value) {
    if (!socket || socket.readyState !== WebSocket.OPEN) return false;
    try {
      socket.send(JSON.stringify(value));
      return true;
    } catch {
      return false;
    }
  }

  function broadcast(value) {
    const encoded = JSON.stringify(value);
    for (const client of browserClients) {
      if (client.readyState !== WebSocket.OPEN) {
        browserClients.delete(client);
        continue;
      }
      try { client.send(encoded); } catch { browserClients.delete(client); }
    }
  }

  function broadcastState() {
    broadcast({ type: 'state', state: currentState() });
  }

  function failPending(status, message) {
    for (const [id, pending] of pendingCommands) {
      clearTimeout(pending.timer);
      pendingCommands.delete(id);
      pending.reject(new HttpError(status, message));
    }
  }

  function issueCommand(action, payload = {}, timeoutMs = config.commandTimeoutMs) {
    if (!isDeviceOnline()) return Promise.reject(new HttpError(503, 'The hub is not connected to the cloud server.'));
    if (pendingCommands.size >= 32) return Promise.reject(new HttpError(429, 'Too many commands are waiting for the hub.'));
    const id = crypto.randomUUID();
    const timeout = Math.max(1000, Math.min(Number(timeoutMs) || config.commandTimeoutMs, 30000));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pendingCommands.delete(id);
        reject(new HttpError(504, 'The hub did not acknowledge the command in time.'));
      }, timeout);
      pendingCommands.set(id, { resolve, reject, timer, action });
      if (!sendJson(deviceSocket, { type: 'command', id, action, payload })) {
        clearTimeout(timer);
        pendingCommands.delete(id);
        reject(new HttpError(503, 'The WebSocket connection to the hub was lost.'));
      }
    });
  }

  function sameOrigin(req) {
    const origin = req.get('origin');
    if (!origin) return false;
    let expected = config.publicOrigin;
    if (!expected) {
      const forwardedProto = String(req.get('x-forwarded-proto') || req.protocol).split(',')[0].trim();
      expected = `${forwardedProto}://${req.get('host')}`;
    }
    return origin === expected;
  }

  function requireSameOrigin(req, res, next) {
    if (!sameOrigin(req)) return res.status(403).json({ ok: false, error: 'درخواست از مبدأ مجاز نیست.' });
    next();
  }

  function sessionForRequest(req) {
    const cookie = parseCookies(req.headers.cookie || '')[COOKIE_NAME];
    return verifySession(cookie, config.sessionSecret);
  }

  function requireLogin(req, res, next) {
    const session = sessionForRequest(req);
    if (!session) return res.status(401).json({ ok: false, error: 'ابتدا وارد پنل شوید.' });
    req.panelUser = session.sub;
    next();
  }

  function setSessionCookie(req, res, value) {
    const secure = config.nodeEnv === 'production' || req.secure;
    const fields = [
      `${COOKIE_NAME}=${encodeURIComponent(value)}`,
      'Path=/',
      'HttpOnly',
      'SameSite=Strict',
      `Max-Age=${SESSION_TTL_SECONDS}`,
    ];
    if (secure) fields.push('Secure');
    res.setHeader('Set-Cookie', fields.join('; '));
  }

  function setExpiredCookie(req, res) {
    const secure = config.nodeEnv === 'production' || req.secure;
    const fields = [`${COOKIE_NAME}=`, 'Path=/', 'HttpOnly', 'SameSite=Strict', 'Max-Age=0'];
    if (secure) fields.push('Secure');
    res.setHeader('Set-Cookie', fields.join('; '));
  }

  function allowLoginAttempt(req) {
    const key = String(req.ip || req.socket.remoteAddress || 'unknown');
    const now = Date.now();
    const current = loginAttempts.get(key);
    if (!current || now - current.startedAt > 15 * 60 * 1000) {
      loginAttempts.set(key, { startedAt: now, count: 1 });
      return true;
    }
    current.count++;
    return current.count <= 10;
  }

  function appendGsmLogs(logs) {
    if (!Array.isArray(logs)) return;
    for (const item of logs.slice(0, 40)) {
      if (!item || typeof item.t !== 'string') continue;
      const text = item.t.replace(/[\u0000-\u001f\u007f]/g, '').slice(0, 176);
      if (!text) continue;
      gsmLogs.push({ seq: ++gsmSequence, t: text });
    }
    if (gsmLogs.length > MAX_GSM_LOGS) gsmLogs = gsmLogs.slice(-MAX_GSM_LOGS);
  }

  function handleDeviceMessage(socket, data, isBinary) {
    if (isBinary) return socket.close(1003, 'JSON text messages only');
    let message;
    try { message = JSON.parse(data.toString('utf8')); } catch { return socket.close(1007, 'Invalid JSON'); }
    if (!message || typeof message !== 'object' || Array.isArray(message)) return;
    lastSeen = Date.now();

    if (message.type === 'state' && message.state && typeof message.state === 'object' && !Array.isArray(message.state)) {
      lastState = message.state;
      if (scanPending && lastState.wifi && !lastState.wifi.scanning) scanPending = false;
      broadcastState();
      return;
    }
    if (message.type === 'gsm') {
      appendGsmLogs(message.logs);
      return;
    }
    if (message.type === 'result' && typeof message.id === 'string') {
      const pending = pendingCommands.get(message.id);
      if (!pending) return;
      clearTimeout(pending.timer);
      pendingCommands.delete(message.id);
      const result = {
        ok: message.ok === true,
        message: typeof message.message === 'string' ? message.message.slice(0, 240) : '',
        data: message.data && typeof message.data === 'object' ? message.data : {},
      };
      if (pending.action === 'wifi.scan' && result.ok) scanPending = !!result.data.scanning;
      pending.resolve(result);
      broadcastState();
    }
  }

  function rejectUpgrade(socket, status, message) {
    const phrase = status === 401 ? 'Unauthorized' : status === 403 ? 'Forbidden' : 'Not Found';
    try {
      socket.write(`HTTP/1.1 ${status} ${phrase}\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: ${Buffer.byteLength(message)}\r\n\r\n${message}`);
    } catch { /* The peer may already have disconnected. */ }
    socket.destroy();
  }

  server.on('upgrade', (req, socket, head) => {
    let pathname;
    try { pathname = new URL(req.url, 'http://localhost').pathname; } catch { return rejectUpgrade(socket, 404, 'Not found'); }

    if (pathname === '/ws/device') {
      const authorization = String(req.headers.authorization || '');
      const match = /^Bearer\s+(.+)$/i.exec(authorization);
      const deviceId = String(req.headers['x-device-id'] || '');
      if (!match || !safeEqual(match[1], config.deviceToken) || !safeEqual(deviceId, config.deviceId)) {
        return rejectUpgrade(socket, 401, 'Unauthorized device');
      }
      return wss.handleUpgrade(req, socket, head, (ws) => {
        if (deviceSocket && deviceSocket !== ws && deviceSocket.readyState === WebSocket.OPEN) {
          deviceSocket.close(4001, 'Replaced by a newer connection');
        }
        deviceSocket = ws;
        lastSeen = Date.now();
        ws.isAlive = true;
        ws.on('pong', () => { ws.isAlive = true; lastSeen = Date.now(); });
        ws.on('message', (data, binary) => handleDeviceMessage(ws, data, binary));
        ws.on('error', () => {});
        ws.on('close', () => {
          if (deviceSocket === ws) {
            deviceSocket = null;
            scanPending = false;
            failPending(503, 'The hub disconnected before acknowledging the command.');
            broadcastState();
          }
        });
        broadcastState();
      });
    }

    if (pathname === '/ws/browser') {
      const session = sessionForRequest({ headers: req.headers });
      const origin = req.headers.origin;
      if (!session) return rejectUpgrade(socket, 401, 'Panel login required');
      if (config.publicOrigin && origin !== config.publicOrigin) return rejectUpgrade(socket, 403, 'Origin not allowed');
      return wss.handleUpgrade(req, socket, head, (ws) => {
        browserClients.add(ws);
        ws.send(JSON.stringify({ type: 'state', state: currentState() }));
        ws.on('message', () => ws.close(1008, 'Browser WebSocket is read-only'));
        ws.on('error', () => browserClients.delete(ws));
        ws.on('close', () => browserClients.delete(ws));
      });
    }

    rejectUpgrade(socket, 404, 'WebSocket route not found');
  });

  app.get('/healthz', (_req, res) => res.json({ ok: true, service: 'evohatch-cloud', deviceOnline: isDeviceOnline() }));
  app.get('/api/auth/me', (req, res) => {
    const session = sessionForRequest(req);
    res.setHeader('Cache-Control', 'no-store');
    res.json({ remote: true, authenticated: !!session, username: session ? session.sub : undefined });
  });
  app.post('/api/auth/login', requireSameOrigin, (req, res) => {
    if (!allowLoginAttempt(req)) return res.status(429).json({ ok: false, error: 'تعداد تلاش‌های ورود زیاد است؛ کمی بعد دوباره امتحان کنید.' });
    const username = typeof req.body?.username === 'string' ? req.body.username : '';
    const password = typeof req.body?.password === 'string' ? req.body.password : '';
    const valid = safeEqual(username, config.adminUsername) && safeEqual(password, config.adminPassword);
    if (!valid) return res.status(401).json({ ok: false, error: 'نام کاربری یا رمز عبور نادرست است.' });
    loginAttempts.delete(String(req.ip || req.socket.remoteAddress || 'unknown'));
    setSessionCookie(req, res, makeSession(config.adminUsername, config.sessionSecret));
    res.json({ ok: true });
  });
  app.post('/api/auth/logout', requireSameOrigin, (req, res) => {
    setExpiredCookie(req, res);
    res.json({ ok: true });
  });

  app.get('/api/state', requireLogin, (_req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.json(currentState());
  });

  app.get('/api/scan', requireLogin, asyncRoute(async (req, res) => {
    if (req.query.refresh === '1' || (!lastState.wifi?.networks?.length && !scanPending)) {
      const result = await issueCommand('wifi.scan', {});
      if (!result.ok) throw new HttpError(502, result.message || 'The hub could not start a Wi-Fi scan.');
      scanPending = !!result.data.scanning;
    }
    const wifi = currentState().wifi;
    res.json({ scanning: !!wifi.scanning, networks: Array.isArray(wifi.networks) ? wifi.networks : [] });
  }));

  app.get('/api/ping', requireLogin, asyncRoute(async (_req, res) => {
    const result = await issueCommand('server.ping', {}, Math.max(config.commandTimeoutMs, 12000));
    if (!result.ok) throw new HttpError(502, result.message || 'The hub could not perform the connectivity test.');
    res.json({ ok: result.data.ok === true });
  }));

  app.get('/api/gsm', requireLogin, (req, res) => {
    const after = Math.max(0, Number.parseInt(String(req.query.after || '0'), 10) || 0);
    const logs = gsmLogs.filter((line) => line.seq > after).slice(-40);
    res.json({ seq: gsmSequence, logs });
  });

  app.post('/api/wifi', requireLogin, requireSameOrigin, asyncRoute(async (req, res) => {
    const ssid = normalizeSafeText(req.body?.ssid, 'ssid', 32, { trim: false });
    const pass = normalizeSafeText(req.body?.pass ?? '', 'pass', 64, { required: false, trim: false });
    const result = await issueCommand('wifi.set', { ssid, pass });
    if (!result.ok) throw new HttpError(502, result.message || 'The hub rejected the Wi-Fi settings.');
    res.json({ ok: true });
  }));

  app.post('/api/server', requireLogin, requireSameOrigin, asyncRoute(async (req, res) => {
    const domain = normalizeSafeText(req.body?.domain, 'domain', 63);
    const rate = Number(req.body?.rate);
    if (!Number.isInteger(rate) || !VALID_RATES.has(rate)) throw new HttpError(400, 'rate is not supported.');
    const result = await issueCommand('server.set', { domain, rate });
    if (!result.ok) throw new HttpError(502, result.message || 'The hub rejected the server settings.');
    res.json({ ok: true });
  }));

  app.post('/api/sim', requireLogin, requireSameOrigin, asyncRoute(async (req, res) => {
    const phones = ['p1', 'p2', 'p3'].map((key) => normalizeSafeText(req.body?.[key] ?? '', key, 11, { required: false }));
    if (phones.some((phone) => phone && !/^\+?[0-9]+$/.test(phone))) throw new HttpError(400, 'Phone numbers may contain digits and an optional leading plus sign only.');
    const mask = toMask(req.body?.mask);
    const result = await issueCommand('sim.set', { p1: phones[0], p2: phones[1], p3: phones[2], mask });
    if (!result.ok) throw new HttpError(502, result.message || 'The hub rejected the SIM settings.');
    res.json({ ok: true });
  }));

  app.post('/api/device', requireLogin, requireSameOrigin, asyncRoute(async (req, res) => {
    const id = Number(req.body?.id);
    const temp = Number(req.body?.sp_t);
    const humidity = Number(req.body?.sp_h);
    const mask = toMask(req.body?.mask);
    if (!Number.isInteger(id) || id < 1 || id > 16) throw new HttpError(400, 'id must be between 1 and 16.');
    if (!Number.isFinite(temp) || temp < 20 || temp > 45) throw new HttpError(400, 'sp_t must be between 20 and 45 degrees.');
    if (!Number.isFinite(humidity) || humidity < 0 || humidity > 95) throw new HttpError(400, 'sp_h must be between 0 and 95 percent.');
    if (typeof req.body?.paired !== 'boolean') throw new HttpError(400, 'paired must be a boolean.');
    const result = await issueCommand('device.set', {
      id, sp_t: Math.round(temp * 10) / 10, sp_h: Math.round(humidity * 10) / 10,
      mask, paired: req.body.paired,
    });
    if (!result.ok) throw new HttpError(502, result.message || 'The hub rejected the device settings.');
    res.json({ ok: true });
  }));

  app.post('/api/gsm', requireLogin, requireSameOrigin, asyncRoute(async (req, res) => {
    const cmd = normalizeSafeText(req.body?.cmd, 'cmd', 127);
    const result = await issueCommand('gsm.command', { cmd });
    if (!result.ok) throw new HttpError(502, result.message || 'The hub rejected the modem command.');
    res.json({ ok: true });
  }));

  app.post('/api/settings', requireLogin, requireSameOrigin, asyncRoute(async (req, res) => {
    const cmd = normalizeSafeText(req.body?.cmd, 'cmd', 150);
    if (!/^[\x20-\x7e]+$/.test(cmd)) throw new HttpError(400, 'cmd must contain printable ASCII characters only.');
    const result = await issueCommand('settings.command', { cmd }, 18000);
    if (!result.ok) throw new HttpError(502, result.message || 'The STM32 did not accept the settings command.');
    res.json({ ok: true, text: result.message });
  }));

  app.use('/api', (_req, res) => res.status(404).json({ ok: false, error: 'مسیر API پیدا نشد.' }));
  app.use(express.static(config.publicDir, {
    dotfiles: 'deny',
    index: 'index.html',
    fallthrough: true,
    setHeaders(res) { res.setHeader('Cache-Control', 'no-cache'); },
  }));
  app.use((req, res) => {
    if (req.accepts('html')) return res.status(404).sendFile(path.join(config.publicDir, 'index.html'));
    res.status(404).json({ ok: false, error: 'not found' });
  });
  app.use((error, _req, res, _next) => {
    const status = Number.isInteger(error.status) ? error.status : (error instanceof SyntaxError ? 400 : 500);
    const message = status >= 500 ? 'خطای داخلی سرور.' : (error.message || 'درخواست نامعتبر است.');
    if (status >= 500) console.error('[api]', error.message);
    res.status(status).json({ ok: false, error: message });
  });

  const heartbeat = setInterval(() => {
    if (deviceSocket && deviceSocket.readyState === WebSocket.OPEN) {
      if (deviceSocket.isAlive === false) {
        deviceSocket.terminate();
      } else {
        deviceSocket.isAlive = false;
        try { deviceSocket.ping(); } catch { deviceSocket.terminate(); }
      }
    }
    if (loginAttempts.size > 2000) {
      const cutoff = Date.now() - 15 * 60 * 1000;
      for (const [key, attempt] of loginAttempts) if (attempt.startedAt < cutoff) loginAttempts.delete(key);
    }
  }, 15000);
  heartbeat.unref?.();

  async function close() {
    if (closed) return;
    closed = true;
    clearInterval(heartbeat);
    for (const client of wss.clients) client.terminate();
    failPending(503, 'The cloud server is shutting down.');
    if (!server.listening) return;
    await new Promise((resolve) => server.close(resolve));
  }

  function listen(port = config.port, host = config.host) {
    return new Promise((resolve, reject) => {
      const onError = (error) => { server.off('listening', onListening); reject(error); };
      const onListening = () => { server.off('error', onError); resolve(server.address()); };
      server.once('error', onError);
      server.once('listening', onListening);
      server.listen(port, host);
    });
  }

  return { app, server, wss, listen, close, config };
}

if (require.main === module) {
  let hub;
  try {
    hub = createHubServer();
  } catch (error) {
    console.error(`[startup] ${error.message}`);
    process.exit(1);
  }
  hub.listen().then((address) => {
    console.log(`[evohatch-cloud] listening on ${address.address}:${address.port}`);
    console.log(`[evohatch-cloud] device id: ${hub.config.deviceId}; WebSocket: /ws/device`);
  }).catch((error) => {
    console.error(`[startup] ${error.message}`);
    process.exit(1);
  });
  const shutdown = async () => { await hub.close(); process.exit(0); };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

module.exports = { createHubServer, defaultState, readConfig, safeEqual, verifySession };
