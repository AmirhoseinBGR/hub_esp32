/** main.cpp - EvoHatch Hub ESP32 board: web server + WiFi + UART bridge to STM32. */
#include <Arduino.h>

#include "config.h"
#include "hub.h"
#include "net.h"
#include "store.h"
#include "uart_link.h"
#include "web_api.h"

void setup()
{
    Serial.begin(115200);

    store_load(); // restore WiFi + Server settings from NVS
    uart_begin();
    net_begin();  // AP always on; STA connect if settings stored
    web_begin();

    Serial.println("EvoHatch Hub ready");
}

void loop()
{
    server.handleClient();
    uart_poll();
    net_service();
}
