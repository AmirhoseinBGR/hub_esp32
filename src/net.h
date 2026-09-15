/** net.h - WiFi connection manager + server ping. */
#pragma once

#include <Arduino.h>

void net_begin();              // start AP + first STA connect if stored
void net_start_connect();      // (re)start connecting to hub.wifi_ssid
void net_service();            // call from loop: connection state machine
bool net_ping_server();        // HTTP GET to hub.server_domain, updates hub.server_ok
