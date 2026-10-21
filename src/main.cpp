/** main.cpp - EvoHatch Hub ESP32 board: web server + WiFi + UART bridge to STM32. */
#include <Arduino.h>

#include "config.h"
#include "hub.h"
#include "net.h"
#include "shell.h"
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
    shell_begin();
    net_ping_task_start(); // background server ping, keeps loop() free

    Serial.println("EvoHatch Hub ready");

    pinMode(2  , OUTPUT); // onboard LED
}

void loop()
{
    static long long now = millis();

    // heartbeat: reversed comparison meant the LED never blinked and a live
    // board looked dead while WiFi was failing
    if (millis() - now >= 100) {
        digitalWrite(2, !digitalRead(2)); // toggle onboard LED
        now = millis();
    }


    server.handleClient();
    uart_poll();
    net_service();
    shell_poll();
}
