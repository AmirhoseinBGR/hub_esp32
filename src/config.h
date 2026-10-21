/**
 * config.h - pins, constants and UART protocol definition.
 *
 * Line format:  TYPE|field|field|...   terminated by \n, max 256 bytes.
 *
 * STM32 -> ESP32:
 *   SET:WIFI|<ssid>|<pass>|<0|1>            apply + persist + connect
 *   SET:SIM|<p1>|<p2>|<p3>|<mask_hex>       apply to cache
 *   SET:SERVER|<domain>|<rate>              apply + persist
 *   SET:DEV|<id>|<sp_t_x10>|<sp_h_x10>|<mask_hex>|<0|1>
 *   SYNC:END                                burst finished -> reply SYNC_RES
 *   STATE:DEV|<id>|<0|1>|<t_x10>|<h_x10>|<rot>|<mask_hex>
 *   STATE:HUB|<sim_ok>|<credit>|<wifi>|<server_ok>
 *   REQ:SCAN / REQ:PING / REQ:CREDIT
 *   GSM:ST|<fsm>|<act>|<step>|<ready>|<sim>|<net>|<op>|<rssi>|<bars>|
 *         <smsu>|<smst>|<paused>|<qsize>|<qcap>|<call>|<peer>   (modem live)
 *   GSM:LOG|<text>                          modem console line
 *
 * ESP32 -> STM32:
 *   WIFI:STATUS:CONNECTED:<ssid> | WIFI:STATUS:DISCONNECTED
 *   WIFI:STATUS:FAILED:AUTH | WIFI:STATUS:FAILED:TIMEOUT
 *   WIFI:SCAN_RES:<ssid>,<ssid>,...
 *   SERVER:PING_RES:OK | SERVER:PING_RES:FAIL
 *   SERVER:SYNC_RES:OK
 *   CREDIT:<toman>
 *   GSM:CMD|<command>                       web/serial/cloud -> modem CLI
 *
 * Cloud WebSocket (WSS): authenticated JSON command frames are translated to
 * the same SET:/REQ:/GSM:CMD UART protocol; live HubState + GSM logs stream up.
 */
#pragma once

#define UART_RX_PIN 16
#define UART_TX_PIN 17
#define UART_BAUD 115200

#define AP_SSID "ESP32_HUB_AP"
#define AP_PASS "12345678"

#define PROTO_LINE_MAX 256

// How long without UART traffic before STM32 is considered offline (ms)
#define STM32_TIMEOUT_MS 15000
// How long a single connect attempt may take before it counts as failed (ms)
#define WIFI_TIMEOUT_MS 20000
// Earliest failure event we trust: WiFi.begin() itself posts a disconnect for
// the previous configuration and it must not be read as a failed attempt (ms)
#define WIFI_ATTEMPT_MIN_MS 300
/* Reconnect backoff after a failed attempt: 5s, 10s, 20s, 40s, 80s, ... capped.
 * The old code either retried forever every 20s (radio storm, scans died) or
 * stopped reconnecting for good after the first failure (hub stayed offline). */
#define WIFI_RETRY_BASE_MS 5000
#define WIFI_RETRY_MAX_MS 120000
// Wrong password: jump straight into the deep backoff - a human must fix it
#define WIFI_RETRY_AUTH_STEP 3
// Periodic ping of the central server (ms)
#define PING_PERIOD_MS 60000
// How often ESP32 re-announces WIFI:STATUS to STM32 (ms)
#define WIFI_STATUS_PERIOD_MS 8000
// WiFi scan: first attempt often races STA connect — retry before giving up
#define WIFI_SCAN_RETRIES 3
// ... more passes while STA is associating: that is when scans come back empty
#define WIFI_SCAN_RETRIES_BUSY 5
#define WIFI_SCAN_RETRY_DELAY_MS 600
#define WIFI_SCAN_MAX_MS_PER_CHAN 400
// Watchdog: a stuck async scan must not block net_scan_start() forever (ms)
#define WIFI_SCAN_TIMEOUT_MS 15000
