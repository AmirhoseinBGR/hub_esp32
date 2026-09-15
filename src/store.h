/** store.h - persistence of WiFi + Server settings in NVS (survives reboot). */
#pragma once

#include <Arduino.h>

void store_load();
void store_save_wifi();
void store_save_server();
