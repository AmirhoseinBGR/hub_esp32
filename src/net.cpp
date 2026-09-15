/** net.cpp - non-blocking WiFi connect + periodic reachability check. */
#include "net.h"
#include "config.h"
#include "hub.h"
#include "store.h"
#include "uart_link.h"
#include <HTTPClient.h>
#include <WiFi.h>

static bool connecting = false;
static uint32_t connect_start = 0;
static uint32_t last_retry = 0;
static uint32_t last_ping = 0;

static void report(bool connected)
{
    hub.wifi_connected = connected;
    if (connected)
    {
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
    WiFi.begin(hub.wifi_ssid, hub.wifi_pass);
    connecting = true;
    connect_start = millis();
    last_retry = millis();
}

void net_service()
{
    if (connecting && WiFi.status() == WL_CONNECTED)
    {
        connecting = false;
        report(true);
    }
    else if (connecting && millis() - connect_start > WIFI_TIMEOUT_MS)
    {
        connecting = false;
        report(false);
    }
    else if (!connecting && WiFi.status() != WL_CONNECTED && hub.wifi_auto &&
             hub.wifi_ssid[0] != '\0' && millis() - last_retry > WIFI_RETRY_MS)
    {
        last_retry = millis();
        connect_start = millis();
        connecting = true;
        WiFi.begin(hub.wifi_ssid, hub.wifi_pass);
    }
    else if (!connecting && WiFi.status() == WL_CONNECTED && !hub.wifi_connected)
    {
        report(true);
    }

    if (millis() - last_ping > PING_PERIOD_MS)
    {
        last_ping = millis();
        net_ping_server();
    }
}

bool net_ping_server()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        hub.server_ok = false;
        return false;
    }

    HTTPClient http;
    String url = String("http://") + hub.server_domain + "/";
    http.begin(url);
    http.setTimeout(4000);
    int code = http.GET();
    http.end();

    hub.server_ok = (code > 0);
    return hub.server_ok;
}
