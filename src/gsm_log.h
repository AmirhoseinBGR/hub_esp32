/** gsm_log.h - ring buffer of GSM console lines (fed by GSM:LOG from STM32). */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void gsm_log_add(const char *text);
void gsm_log_to_json(JsonArray arr, uint32_t after_seq, int max_lines);
void gsm_log_mirror_serial(bool on);
bool gsm_log_mirroring();
uint32_t gsm_log_latest_seq();
