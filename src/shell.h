/**
 * shell.h - tiny USB serial shell.
 * Type any line in the Arduino Serial Monitor:
 *   help                 command list
 *   state                dump full state JSON
 *   wifi <ssid> <pass>   set + save WiFi
 *   server <domain>      set + save server domain
 *   anything else        forwarded to the STM32 GSM CLI (same commands as the
 *                        serial menu on the device: sms, dial, ussd, at, ...)
 */
#pragma once

#include <Arduino.h>

void shell_begin();
void shell_poll(); // call from loop()
