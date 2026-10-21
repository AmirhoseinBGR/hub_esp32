'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { once } = require('node:events');
const WebSocket = require('ws');
const { createHubServer } = require('../server');

const config = {
  host: '127.0.0.1', port: 0, publicOrigin: '',
  adminUsername: 'panel-admin', adminPassword: 'a-unique-test-password-123',
  sessionSecret: 'test-session-secret-that-is-longer-than-thirty-two-bytes',
  deviceId: 'hub-test', deviceToken: 'unit-test-device-token-with-more-than-32-characters',
  deviceTimeoutMs: 10000, commandTimeoutMs: 2500, nodeEnv: 'test',
  publicDir: path.join(__dirname, '..', 'public'),
};

async function createTestServer() {
  const hub = createHubServer({ config });
  const address = await hub.listen(0, '127.0.0.1');
  const base = `http://127.0.0.1:${address.port}`;
  return { hub, base, close: () => hub.close() };
}

async function login(base) {
  const response = await fetch(`${base}/api/auth/login`, {
    method: 'POST',
    headers: { 'content-type': 'application/json', origin: base },
    body: JSON.stringify({ username: config.adminUsername, password: config.adminPassword }),
  });
  assert.equal(response.status, 200);
  return response.headers.get('set-cookie').split(';')[0];
}

function jsonHeaders(base, cookie) {
  return { 'content-type': 'application/json', origin: base, cookie };
}

test('remote API requires login and serves the light dashboard', async (t) => {
  const { hub, base, close } = await createTestServer();
  t.after(close);

  const page = await fetch(base);
  assert.equal(page.status, 200);
  assert.match(await page.text(), /مرکز کنترل EvoHatch/);

  const authState = await fetch(`${base}/api/auth/me`);
  assert.deepEqual(await authState.json(), { remote: true, authenticated: false });
  const privateState = await fetch(`${base}/api/state`);
  assert.equal(privateState.status, 401);

  const cookie = await login(base);
  const stateResponse = await fetch(`${base}/api/state`, { headers: { cookie } });
  assert.equal(stateResponse.status, 200);
  const state = await stateResponse.json();
  assert.equal(state.cloud.enabled, true);
  assert.equal(state.cloud.connected, false);
  assert.equal(state.devices.length, 16);
});

test('authenticated browser receives live state and controls are forwarded to the device socket', async (t) => {
  const { hub, base, close } = await createTestServer();
  t.after(close);
  const cookie = await login(base);
  const wsBase = base.replace(/^http/, 'ws');
  const browser = new WebSocket(`${wsBase}/ws/browser`, { headers: { cookie, origin: base } });
  t.after(() => browser.terminate());
  const initialPromise = once(browser, 'message');
  await once(browser, 'open');
  const initial = await initialPromise;
  assert.equal(JSON.parse(initial[0].toString()).type, 'state');

  const device = new WebSocket(`${wsBase}/ws/device`, {
    headers: {
      authorization: `Bearer ${config.deviceToken}`,
      'x-device-id': config.deviceId,
    },
  });
  t.after(() => device.terminate());
  await once(device, 'open');

  const liveState = {
    link: { stm32: true, age_ms: 0 },
    wifi: { ssid: 'Studio-WiFi', connected: true, ip: '192.168.1.42', phase: 2, err: 0, scanning: false, networks: [] },
    server: { domain: 'monitor.deuteriumstudio.ir', rate: 12, ok: true },
    sim: { ok: true, credit: 1000, phones: ['', '', ''], mask: 175 },
    gsm: { seen: true, ready: true, sim: true, net: true, op: 'Test', rssi: -63, bars: 4, sms_used: 0, sms_total: 20, paused: false, qsize: 0, qcap: 10, call: 'idle', peer: '' },
    devices: [],
  };
  device.send(JSON.stringify({ type: 'state', state: liveState }));
  const stateResponse = await fetch(`${base}/api/state`, { headers: { cookie } });
  const state = await stateResponse.json();
  assert.equal(state.cloud.connected, true);
  assert.equal(state.wifi.ssid, 'Studio-WiFi');

  const commandPromise = once(device, 'message').then(([raw]) => JSON.parse(raw.toString()));
  const commandResponsePromise = fetch(`${base}/api/gsm`, {
    method: 'POST', headers: jsonHeaders(base, cookie), body: JSON.stringify({ cmd: 'i' }),
  });
  const command = await commandPromise;
  assert.equal(command.type, 'command');
  assert.equal(command.action, 'gsm.command');
  assert.deepEqual(command.payload, { cmd: 'i' });
  device.send(JSON.stringify({ type: 'result', id: command.id, ok: true, message: 'sent' }));
  const commandResponse = await commandResponsePromise;
  assert.equal(commandResponse.status, 200);
  assert.deepEqual(await commandResponse.json(), { ok: true });
});

test('Wi-Fi scan and credential commands use the device acknowledgement protocol', async (t) => {
  const { hub, base, close } = await createTestServer();
  t.after(close);
  const cookie = await login(base);
  const wsBase = base.replace(/^http/, 'ws');
  const device = new WebSocket(`${wsBase}/ws/device`, {
    headers: { authorization: `Bearer ${config.deviceToken}`, 'x-device-id': config.deviceId },
  });
  t.after(() => device.terminate());
  await once(device, 'open');

  const scanCommandPromise = once(device, 'message').then(([raw]) => JSON.parse(raw.toString()));
  const scanResponsePromise = fetch(`${base}/api/scan?refresh=1`, { headers: { cookie } });
  const scanCommand = await scanCommandPromise;
  assert.equal(scanCommand.action, 'wifi.scan');
  device.send(JSON.stringify({ type: 'result', id: scanCommand.id, ok: true, data: { scanning: true } }));
  const scanResponse = await scanResponsePromise;
  assert.deepEqual(await scanResponse.json(), { scanning: true, networks: [] });

  const wifiCommandPromise = once(device, 'message').then(([raw]) => JSON.parse(raw.toString()));
  const wifiResponsePromise = fetch(`${base}/api/wifi`, {
    method: 'POST',
    headers: jsonHeaders(base, cookie),
    body: JSON.stringify({ ssid: ' Studio WiFi ', pass: ' pass phrase ' }),
  });
  const wifiCommand = await wifiCommandPromise;
  assert.equal(wifiCommand.action, 'wifi.set');
  assert.deepEqual(wifiCommand.payload, { ssid: ' Studio WiFi ', pass: ' pass phrase ' });
  device.send(JSON.stringify({ type: 'result', id: wifiCommand.id, ok: true }));
  const wifiResponse = await wifiResponsePromise;
  assert.deepEqual(await wifiResponse.json(), { ok: true });
});

test('cookie-protected writes reject a cross-origin request', async (t) => {
  const { base, close } = await createTestServer();
  t.after(close);
  const cookie = await login(base);
  const response = await fetch(`${base}/api/gsm`, {
    method: 'POST',
    headers: { 'content-type': 'application/json', origin: 'https://attacker.example', cookie },
    body: JSON.stringify({ cmd: 'i' }),
  });
  assert.equal(response.status, 403);
});
