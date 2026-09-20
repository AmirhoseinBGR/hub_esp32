/**
 * web_api.cpp - the whole API:
 *
 *   GET  /             web page
 *   GET  /api/state    everything: settings + live states + gsm status
 *   GET  /api/scan     start/poll async WiFi scan  {scanning, networks[]}
 *   GET  /api/ping     test central server now
 *   GET  /api/gsm      gsm console log lines ?after=<seq>
 *   POST /api/wifi     {ssid, pass}
 *   POST /api/server   {domain, rate}
 *   POST /api/sim      {p1, p2, p3, mask}
 *   POST /api/device   {id, sp_t, sp_h, mask, paired}
 *   POST /api/gsm      {cmd: "sms 0912... hi"} -> forwarded to STM32 modem CLI
 *
 * Every POST applies locally, persists when ESP32 owns the data,
 * and forwards SET: to the STM32 so both sides stay in sync.
 */
#include "web_api.h"
#include "config.h"
#include "gsm_log.h"
#include "hub.h"
#include "index_html.h"
#include "net.h"
#include "store.h"
#include "uart_link.h"
#include <ArduinoJson.h>
#include <WiFi.h>

WebServer server(80);

static void reply_json(int code, JsonDocument &doc)
{
    String out;
    serializeJson(doc, out);
    server.send(code, "application/json; charset=utf-8", out);
}

static void reply_ok(bool ok) { server.send(200, "application/json; charset=utf-8", ok ? "{\"ok\":true}" : "{\"ok\":false}"); }

static void handle_state()
{
    JsonDocument doc;
    hub_to_json(doc);
    reply_json(200, doc);
}

static void handle_scan()
{
    // Non-blocking: first call kicks an async scan, later calls pick the result up.
    // ?refresh=1 forces a new scan (the panel's re-scan button).
    if (hub.scanning)
    {
        /* just report current status below */
    }
    else if (server.hasArg("refresh") || hub.scan_count == 0)
    {
        hub.scan_count = 0;
        net_scan_start();
    }

    JsonDocument doc;
    doc["scanning"] = hub.scanning;
    JsonArray list = doc["networks"].to<JsonArray>();
    for (int i = 0; i < hub.scan_count; i++) list.add(hub.scan_list[i]);
    reply_json(200, doc);
}

static void handle_wifi()
{
    JsonDocument req;
    if (deserializeJson(req, server.arg("plain"))) return reply_ok(false);

    const char *ssid = req["ssid"] | "";
    const char *pass = req["pass"] | "";
    if (ssid[0] == '\0') return reply_ok(false);

    apply_wifi(ssid, pass, true);
    store_save_wifi();
    hub.scan_count = 0; // force fresh scan next time
    net_start_connect();
    uart_send("SET:WIFI|%s|%s|1", ssid, pass);
    reply_ok(true);
}

static void handle_server()
{
    JsonDocument req;
    if (deserializeJson(req, server.arg("plain"))) return reply_ok(false);

    const char *domain = req["domain"] | "";
    uint16_t rate = req["rate"] | 0;
    if (domain[0] == '\0' || rate == 0) return reply_ok(false);

    apply_server(domain, rate);
    store_save_server();
    uart_send("SET:SERVER|%s|%u", domain, (unsigned)rate);
    reply_ok(true);
}

static void handle_sim()
{
    JsonDocument req;
    if (deserializeJson(req, server.arg("plain"))) return reply_ok(false);

    const char *p1 = req["p1"] | "";
    const char *p2 = req["p2"] | "";
    const char *p3 = req["p3"] | "";
    uint16_t mask = req["mask"] | hub.sim_alarm_enable;

    apply_sim(p1, p2, p3, mask);
    uart_send("SET:SIM|%s|%s|%s|%04X", p1, p2, p3, mask);
    reply_ok(true);
}

static void handle_device()
{
    JsonDocument req;
    if (deserializeJson(req, server.arg("plain"))) return reply_ok(false);

    uint8_t id = req["id"] | 0;
    if (id < 1 || id > DEV_COUNT) return reply_ok(false);

    const Device &d = hub.dev[id - 1];
    float sp_t = req["sp_t"] | d.sp_temp_x10 / 10.0f;
    float sp_h = req["sp_h"] | d.sp_hum_x10 / 10.0f;
    uint16_t mask = req["mask"] | d.alarm_enable;
    bool paired = req["paired"] | d.paired;

    apply_device(id, (int16_t)lroundf(sp_t * 10.0f), (int16_t)lroundf(sp_h * 10.0f), mask, paired);
    uart_send("SET:DEV|%u|%d|%d|%04X|%u", (unsigned)id, (int)lroundf(sp_t * 10.0f),
              (int)lroundf(sp_h * 10.0f), mask, (unsigned)paired);
    reply_ok(true);
}

static void handle_gsm_get()
{
    uint32_t after = (uint32_t)strtoul(server.arg("after").c_str(), NULL, 10);
    JsonDocument doc;
    doc["seq"] = gsm_log_latest_seq();
    JsonArray logs = doc["logs"].to<JsonArray>();
    gsm_log_to_json(logs, after, 40);
    reply_json(200, doc);
}

static void handle_gsm_cmd()
{
    JsonDocument req;
    if (deserializeJson(req, server.arg("plain"))) return reply_ok(false);

    const char *cmd = req["cmd"] | "";
    if (cmd[0] == '\0') return reply_ok(false);

    // keep one clean line: no pipes, no control characters
    char clean[128];
    size_t o = 0;
    for (const char *p = cmd; *p && o + 1 < sizeof(clean); p++)
        if (*p != '|' && (unsigned char)*p >= 0x20) clean[o++] = *p;
    clean[o] = '\0';
    if (clean[0] == '\0') return reply_ok(false);

    uart_send("GSM:CMD|%s", clean);
    reply_ok(true);
}

void web_begin()
{
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", (const char *)INDEX_HTML); });
    server.on("/api/state", HTTP_GET, handle_state);
    server.on("/api/scan", HTTP_GET, handle_scan);
    server.on("/api/ping", HTTP_GET, [] { reply_ok(net_ping_server()); });
    server.on("/api/gsm", HTTP_GET, handle_gsm_get);
    server.on("/api/gsm", HTTP_POST, handle_gsm_cmd);
    server.on("/api/wifi", HTTP_POST, handle_wifi);
    server.on("/api/server", HTTP_POST, handle_server);
    server.on("/api/sim", HTTP_POST, handle_sim);
    server.on("/api/device", HTTP_POST, handle_device);
    server.onNotFound([] { server.send(404, "application/json", "{\"error\":\"not found\"}"); });
    server.begin();
}
