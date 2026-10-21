#pragma once
#include <Arduino.h>
// One in-flight command, shared with STM32's bounded transaction manager.
void remote_settings_result(const char* line);
void remote_settings_routes();
