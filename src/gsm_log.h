/** gsm_log.h - ring buffer of GSM console lines (fed by GSM:LOG from STM32). */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void gsm_log_add(const char *text);          // store one line, ANSI stripped
void gsm_log_to_json(JsonArray arr, uint32_t after_seq, int max_lines);
void gsm_log_mirror_serial(bool on);         // echo lines to USB serial
bool gsm_log_mirroring();
