#pragma once
#include <Arduino.h>

// One in-flight settings command shared by local HTTP and the cloud bridge.
void remote_settings_result(const char *line);
void remote_settings_routes();
bool remote_settings_start(const char *cmd, const char *cloud_id);
void remote_settings_poll();
