/**
 * net.cpp - everything here is non-blocking so loop() never stalls
 * (the old WiFi.scanNetworks() sync call + blocking ping froze the board).
 *
 * Scan:    WiFi.scanNetworks(true) async, results polled in net_service().
 * Connect: state machine with wrong-password / timeout detection.
 * Ping:    own FreeRTOS task, loop() is never blocked by HTTP.
 */
#include "net.h"
#include "config.h"
#include "hub.h"
#include "store.h"
#include "uart_link.h"
#include <HTTPClient.h>
#include <WiFi.h>

// older arduino-esp32 cores may not define it
#ifndef WL_WRONG_PASSWORD
#define WL_WRONG_PASSWORD 8
#endif

static bool connecting = false;
static uint32_t connect_start = 0;
static uint32_t last_retry = 0;
static volatile bool ping_busy = false;

static void report(bool connected)
{
    hub.wifi_connected = connected;
    if (connected)
    {
        hub.wifi_phase = 2;
        hub.wifi_err = 0;
        IPAddress ip = WiFi.localIP();
        strlcpy(hub.wifi_ip, ip.toString().c_str(), sizeof(hub.wifi_ip));
        uart_send("WIFI:STATUS:CONNECTED:%s", hub.wifi_ssid);
    }
    else
    {
        hub.wifi_ip[0] = '\0';
        uart_send("WIFI:STATUS:DISCONNECTED");
    }
}

void net_begin()
{
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    if (hub.wifi_auto && hub.wifi_ssid[0] != '\0') net_start_connect();
}

void net_start_connect()
{
    if (hub.wifi_ssid[0] == '\0') return;
    hub.wifi_phase = 1;
    hub.wifi_err = 0;
    WiFi.begin(hub.wifi_ssid, hub.wifi_pass);
    connecting = true;
    connect_start = millis();
    last_retry = millis();
}

void net_scan_start()
{
    if (hub.scanning) return;
    WiFi.scanDelete();
    if (WiFi.scanNetworks(true /*async*/, false) == WIFI_SCAN_RUNNING) hub.scanning = true;
}

static void scan_service()
{
    if (!hub.scanning) return;

    int8_t r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING || r == WIFI_SCAN_FAILED) return; // still working

    hub.scanning = false;

    if (r > 0)
    {
        hub.scan_count = 0;
        for (int i = 0; i < r && hub.scan_count < SCAN_MAX; i++)
        {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue; // skip hidden
            strlcpy(hub.scan_list[hub.scan_count++], ssid.c_str(), 33);
        }
    }
    else hub.scan_count = 0;

    WiFi.scanDelete();

    // push to STM32 device UI (device WiFi page)
    char list[220] = "";
    for (int i = 0; i < hub.scan_count && i < 5; i++)
    {
        if (i > 0) strlcat(list, ",", sizeof(list));
        strlcat(list, hub.scan_list[i], sizeof(list));
    }
    uart_send("WIFI:SCAN_RES:%s", list);

    if (WiFi.status() == WL_CONNECTED)
    {
        connecting = false;
        report(true);
    }
    
}

static void connect_service()
{
    if (!connecting) return;

    if (WiFi.status() == WL_CONNECTED)
    {
        connecting = false;
        report(true);
        return;
    }

    if (WiFi.status() == WL_WRONG_PASSWORD)
    {
        connecting = false;
        hub.wifi_phase = 3;
        hub.wifi_err = 1;
        uart_send("WIFI:STATUS:FAILED:AUTH");
        return;
    }

    if (millis() - connect_start > WIFI_TIMEOUT_MS)
    {
        connecting = false;
        hub.wifi_phase = 3;
        hub.wifi_err = (WiFi.status() == WL_NO_SSID_AVAIL) ? 2 : 3;
        uart_send("WIFI:STATUS:FAILED:TIMEOUT");
        return;
    }
}

void net_service()
{
    scan_service();
    connect_service();

    if (!connecting && WiFi.status() != WL_CONNECTED && hub.wifi_auto &&
        hub.wifi_ssid[0] != '\0' && hub.wifi_phase != 3 &&
        millis() - last_retry > WIFI_RETRY_MS)
    {
        // silent retry; phase stays connected=0/idle so the page shows "retrying"
        last_retry = millis();
        connect_start = millis();
        connecting = true;
        WiFi.begin(hub.wifi_ssid, hub.wifi_pass);
    }
    else if (!connecting && WiFi.status() == WL_CONNECTED && !hub.wifi_connected)
    {
        report(true);
    }
    else if (WiFi.status() != WL_CONNECTED && hub.wifi_connected)
    {
        hub.wifi_connected = false;
        hub.wifi_ip[0] = '\0';
        if (hub.wifi_phase != 3) hub.wifi_phase = 0; // keep a fresh failure reason
        uart_send("WIFI:STATUS:DISCONNECTED");
    }
}

bool net_ping_server()
{
    if (WiFi.status() != WL_CONNECTED || ping_busy)
    {
        if (!ping_busy) hub.server_ok = false;
        return hub.server_ok;
    }

    ping_busy = true;
    HTTPClient http;
    String url = String("http://") + hub.server_domain + "/";
    bool ok = false;
    if (http.begin(url))
    {
        http.setTimeout(4000);
        int code = http.GET();
        ok = (code > 0);
        http.end();
    }
    ping_busy = false;

    hub.server_ok = ok;
    return ok;
}

// Periodic reachability check runs in its own task: HTTP timeout
// can never freeze the web server or the UART link anymore.
static void ping_task(void *)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(PING_PERIOD_MS));
        net_ping_server();
    }
}

void net_ping_task_start()
{
    xTaskCreatePinnedToCore(ping_task, "ping", 4096, nullptr, 1, nullptr, 1);
}
