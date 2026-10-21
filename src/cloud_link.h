#pragma once

#include <Arduino.h>

// Outbound secure WebSocket client for the Plesk cloud dashboard.
void cloud_link_begin();
void cloud_link_poll();
bool cloud_link_is_connected();
void cloud_link_send_result(const char *id, bool ok, const char *message = nullptr);
void cloud_link_send_scan_result(const char *id, bool ok, bool scanning, const char *message = nullptr);
void cloud_link_send_ping_result(const char *id, bool ok);
// Called from the FreeRTOS ping task; the actual WebSocket send is deferred to loop().
void cloud_link_report_ping(const char *id, bool ok);
