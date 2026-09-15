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
 *
 * ESP32 -> STM32:
 *   WIFI:STATUS:CONNECTED:<ssid> | WIFI:STATUS:DISCONNECTED
 *   WIFI:SCAN_RES:<ssid>,<ssid>,...
 *   SERVER:PING_RES:OK | SERVER:PING_RES:FAIL
 *   SERVER:SYNC_RES:OK
 *   CREDIT:<toman>
 */
#pragma once

#define UART_RX_PIN 16
#define UART_TX_PIN 17
#define UART_BAUD 115200

#define AP_SSID "ESP32_HUB_AP"
#define AP_PASS "12345678"

#define LINE_MAX 256

// How long without UART traffic before STM32 is considered offline (ms)
#define STM32_TIMEOUT_MS 15000
// WiFi connect wait / retry period (ms)
#define WIFI_TIMEOUT_MS 20000
#define WIFI_RETRY_MS 20000
// Periodic ping of the central server (ms)
#define PING_PERIOD_MS 60000
