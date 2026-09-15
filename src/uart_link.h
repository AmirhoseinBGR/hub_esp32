/** uart_link.h - line protocol over UART2 to the STM32. */
#pragma once

#include <Arduino.h>

void uart_begin();
void uart_poll(); // call from loop: read lines, parse, update state
void uart_send(const char *fmt, ...);

#ifdef HOST_TEST
HardwareSerial &uart_for_test(); // host-side unit tests only
#endif
