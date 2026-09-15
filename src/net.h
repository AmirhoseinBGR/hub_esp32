/** net.h - WiFi connection manager + async scan + server ping. */
#pragma once

#include <Arduino.h>

void net_begin();              // start AP + first STA connect if stored
void net_start_connect();      // (re)start connecting to hub.wifi_ssid
void net_service();            // call from loop: non-blocking state machine
void net_scan_start();         // kick an async WiFi scan (safe to call repeatedly)
bool net_ping_server();        // HTTP GET to hub.server_domain, updates hub.server_ok
void net_ping_task_start();    // start background ping task
