/**
 * cloud_link.cpp - authenticated WSS bridge from the ESP32 hub to Plesk.
 *
 * Device protocol (one JSON object per WebSocket text frame):
 *   hub -> server: {"type":"state", ...hub_to_json fields...}
 *   hub -> server: {"type":"gsm","logs":[{"seq":1,"t":"..."}]}
 *   server -> hub: {"type":"command","id":"...","action":"wifi.set","payload":{...}}
 *   hub -> server: {"type":"result","id":"...","ok":true,"data":{...}}
 */
#include "cloud_link.h"
#include "cloud_config.h"
#include "gsm_log.h"
#include "hub.h"
#include "net.h"
#include "remote_settings.h"
#include "store.h"
#include "uart_link.h"
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

static WebSocketsClient socketClient;
static bool configured = false;
static bool socketStarted = false;
static uint32_t lastStatePublishMs = 0;
static uint32_t lastLogSeq = 0;
static char commandHeaders[256];

// Manual HTTP checks run in net.cpp's worker task. Copy their result back to
// loop() so the WebSockets library is only touched by the Arduino main task.
static portMUX_TYPE resultMux = portMUX_INITIALIZER_UNLOCKED;
static bool pingResultPending = false;
static bool pingResultOk = false;
static char pingResultId[64] = "";

static bool copy_field(char *out, size_t cap, const char *value, size_t max_len,
                       bool allow_empty = false)
{
    if (!out || cap == 0 || !value) return false;
    const size_t len = strlen(value);
    if ((!allow_empty && len == 0) || len > max_len || len >= cap) return false;
    for (size_t i = 0; i < len; i++)
    {
        const unsigned char c = (unsigned char)value[i];
        if (c < 0x20 || c == 0x7F || c == '|') return false;
    }
    memcpy(out, value, len);
    out[len] = '\0';
    return true;
}

static void send_result_data(const char *id, bool ok, const char *message,
                             int data_kind = 0, bool data_value = false)
{
    if (!configured || !hub.cloud_connected || !id || !*id) return;
    JsonDocument doc;
    doc["type"] = "result";
    doc["id"] = id;
    doc["ok"] = ok;
    if (message && *message) doc["message"] = message;
    if (data_kind == 1)
        doc["data"]["scanning"] = data_value;
    else if (data_kind == 2)
        doc["data"]["ok"] = data_value;

    String text;
    serializeJson(doc, text);
    socketClient.sendTXT(text);
}

void cloud_link_send_result(const char *id, bool ok, const char *message)
{
    send_result_data(id, ok, message);
}

void cloud_link_send_scan_result(const char *id, bool ok, bool scanning, const char *message)
{
    send_result_data(id, ok, message, 1, scanning);
}

void cloud_link_send_ping_result(const char *id, bool ok)
{
    // `ok` inside data describes the target reachability; the command itself
    // completed successfully even if that target did not answer.
    send_result_data(id, true, nullptr, 2, ok);
}

void cloud_link_report_ping(const char *id, bool ok)
{
    if (!id || !*id) return;
    portENTER_CRITICAL(&resultMux);
    strlcpy(pingResultId, id, sizeof(pingResultId));
    pingResultOk = ok;
    pingResultPending = true;
    portEXIT_CRITICAL(&resultMux);
}

bool cloud_link_is_connected() { return hub.cloud_connected; }

static void publish_state()
{
    if (!configured || !hub.cloud_connected) return;
    JsonDocument doc;
    doc["type"] = "state";
    hub_to_json(doc);
    String text;
    serializeJson(doc, text);
    if (socketClient.sendTXT(text)) hub.cloud_last_tx_ms = millis();
}

static void publish_gsm_logs()
{
    if (!configured || !hub.cloud_connected) return;
    const uint32_t latest = gsm_log_latest_seq();
    if (latest < lastLogSeq) lastLogSeq = 0; // the hub rebooted; its local sequence restarted
    if (latest == lastLogSeq) return;

    JsonDocument doc;
    doc["type"] = "gsm";
    doc["seq"] = latest;
    JsonArray lines = doc["logs"].to<JsonArray>();
    gsm_log_to_json(lines, lastLogSeq, 20);
    if (lines.size() == 0) return;

    String text;
    serializeJson(doc, text);
    if (socketClient.sendTXT(text)) lastLogSeq = latest;
}

static bool valid_command_id(const char *id)
{
    if (!id) return false;
    const size_t len = strlen(id);
    if (len == 0 || len >= 64) return false;
    for (size_t i = 0; i < len; i++)
    {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

static void handle_command(uint8_t *payload, size_t length)
{
    JsonDocument req;
    if (deserializeJson(req, payload, length)) return;
    const char *type = req["type"] | "";
    if (strcmp(type, "command") != 0) return;

    const char *id = req["id"] | "";
    const char *action = req["action"] | "";
    if (!valid_command_id(id)) return;

    JsonObjectConst data = req["payload"].as<JsonObjectConst>();
    if (strcmp(action, "wifi.set") == 0)
    {
        char ssid[33], pass[65];
        const char *ssidIn = data["ssid"] | "";
        const char *passIn = data["pass"] | "";
        if (!copy_field(ssid, sizeof(ssid), ssidIn, 32) ||
            !copy_field(pass, sizeof(pass), passIn, 64, true))
        {
            cloud_link_send_result(id, false, "شبکه یا رمز عبور نامعتبر است.");
            return;
        }
        apply_wifi(ssid, pass, true);
        store_save_wifi();
        hub.scan_count = 0;
        net_start_connect();
        uart_send("SET:WIFI|%s|%s|1", ssid, pass);
        cloud_link_send_result(id, true, "تنظیم وای‌فای دریافت شد.");
        return;
    }

    if (strcmp(action, "server.set") == 0)
    {
        char domain[64];
        const char *domainIn = data["domain"] | "";
        const uint16_t rate = data["rate"] | 0;
        if (!copy_field(domain, sizeof(domain), domainIn, 63) || rate == 0 || rate > 60)
        {
            cloud_link_send_result(id, false, "دامنه یا نرخ ارسال نامعتبر است.");
            return;
        }
        if (apply_server(domain, rate)) store_save_server();
        uart_send("SET:SERVER|%s|%u", domain, (unsigned)rate);
        cloud_link_send_result(id, true, "تنظیم سرور دریافت شد.");
        return;
    }

    if (strcmp(action, "sim.set") == 0)
    {
        char p1[12], p2[12], p3[12];
        const char *p1In = data["p1"] | "";
        const char *p2In = data["p2"] | "";
        const char *p3In = data["p3"] | "";
        const uint16_t mask = data["mask"] | hub.sim_alarm_enable;
        if (!copy_field(p1, sizeof(p1), p1In, 11, true) ||
            !copy_field(p2, sizeof(p2), p2In, 11, true) ||
            !copy_field(p3, sizeof(p3), p3In, 11, true))
        {
            cloud_link_send_result(id, false, "شماره هشدار نامعتبر است.");
            return;
        }
        apply_sim(p1, p2, p3, mask);
        uart_send("SET:SIM|%s|%s|%s|%04X", p1, p2, p3, mask);
        cloud_link_send_result(id, true, "تنظیمات سیم‌کارت دریافت شد.");
        return;
    }

    if (strcmp(action, "device.set") == 0)
    {
        const uint8_t deviceId = data["id"] | 0;
        const float temp = data["sp_t"] | -100.0f;
        const float humidity = data["sp_h"] | -100.0f;
        const uint16_t mask = data["mask"] | 0;
        const bool paired = data["paired"] | false;
        if (deviceId < 1 || deviceId > DEV_COUNT || !isfinite(temp) || !isfinite(humidity) ||
            temp < 20.0f || temp > 45.0f || humidity < 0.0f || humidity > 95.0f)
        {
            cloud_link_send_result(id, false, "تنظیمات دستگاه نامعتبر است.");
            return;
        }
        const int16_t t10 = (int16_t)lroundf(temp * 10.0f);
        const int16_t h10 = (int16_t)lroundf(humidity * 10.0f);
        apply_device(deviceId, t10, h10, mask, paired);
        uart_send("SET:DEV|%u|%d|%d|%04X|%u", (unsigned)deviceId, (int)t10,
                  (int)h10, mask, (unsigned)paired);
        cloud_link_send_result(id, true, "تنظیمات دستگاه دریافت شد.");
        return;
    }

    if (strcmp(action, "gsm.command") == 0)
    {
        char command[128];
        const char *commandIn = data["cmd"] | "";
        if (!copy_field(command, sizeof(command), commandIn, 127))
        {
            cloud_link_send_result(id, false, "فرمان مودم نامعتبر است.");
            return;
        }
        uart_send("GSM:CMD|%s", command);
        cloud_link_send_result(id, true, "فرمان به برد اصلی ارسال شد.");
        return;
    }

    if (strcmp(action, "wifi.scan") == 0)
    {
        net_scan_start();
        cloud_link_send_scan_result(id, true, hub.scanning, "اسکن شبکه آغاز شد.");
        return;
    }

    if (strcmp(action, "server.ping") == 0)
    {
        net_request_ping(id);
        return; // the ping worker returns the correlated result asynchronously
    }

    if (strcmp(action, "settings.command") == 0)
    {
        char command[151];
        const char *commandIn = data["cmd"] | "";
        if (!copy_field(command, sizeof(command), commandIn, 150) || !remote_settings_start(command, id))
        {
            cloud_link_send_result(id, false, "فرمان تنظیمات رد شد یا فرمان دیگری در حال اجراست.");
        }
        return; // remote_settings_result() sends the final correlated result
    }

    cloud_link_send_result(id, false, "فرمان پشتیبانی نمی‌شود.");
}

static void web_socket_event(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
        case WStype_CONNECTED:
            hub.cloud_connected = true;
            lastStatePublishMs = 0;
            Serial.println("[CLOUD] WSS connected");
            publish_state();
            break;
        case WStype_DISCONNECTED:
            hub.cloud_connected = false;
            Serial.println("[CLOUD] WSS disconnected");
            break;
        case WStype_TEXT:
            handle_command(payload, length);
            break;
        case WStype_PONG:
            hub.cloud_last_tx_ms = millis();
            break;
        case WStype_ERROR:
            hub.cloud_connected = false;
            Serial.println("[CLOUD] WSS error");
            break;
        default:
            break;
    }
}

void cloud_link_begin()
{
    configured = CLOUD_DEVICE_TOKEN[0] != '\0' &&
                 strcmp(CLOUD_DEVICE_TOKEN, "paste-the-generated-64-character-token-here") != 0;
    hub.cloud_configured = configured;
    if (!configured)
    {
        Serial.println("[CLOUD] Disabled: copy include/cloud_secrets.h.example and set a device token");
        return;
    }

    snprintf(commandHeaders, sizeof(commandHeaders),
             "Authorization: Bearer %s\r\nX-Device-ID: %s",
             CLOUD_DEVICE_TOKEN, CLOUD_DEVICE_ID);
    socketClient.setExtraHeaders(commandHeaders);
    socketClient.onEvent(web_socket_event);
    socketClient.setReconnectInterval(5000);
    socketClient.enableHeartbeat(20000, 5000, 2);

    // CA validation checks certificate validity dates. SNTP runs in the
    // background; defer the first TLS handshake until the clock is credible.
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.printf("[CLOUD] WSS configured for %s (device %s); waiting for Wi-Fi and clock\n",
                  CLOUD_WS_HOST, CLOUD_DEVICE_ID);
}

void cloud_link_poll()
{
    if (!configured) return;
    if (!socketStarted)
    {
        if (WiFi.status() != WL_CONNECTED) return;
        const time_t now = time(nullptr);
        if (now < 1700000000) return; // wait for SNTP before verifying TLS dates
        socketClient.beginSslWithCA(CLOUD_WS_HOST, CLOUD_WS_PORT, CLOUD_WS_PATH, CLOUD_ROOT_CA);
        socketStarted = true;
    }
    socketClient.loop();

    char resultId[sizeof(pingResultId)] = "";
    bool resultOk = false;
    bool haveResult = false;
    portENTER_CRITICAL(&resultMux);
    if (pingResultPending)
    {
        strlcpy(resultId, pingResultId, sizeof(resultId));
        resultOk = pingResultOk;
        pingResultPending = false;
        pingResultId[0] = '\0';
        haveResult = true;
    }
    portEXIT_CRITICAL(&resultMux);
    if (haveResult) cloud_link_send_ping_result(resultId, resultOk);

    if (!hub.cloud_connected) return;
    if (millis() - lastStatePublishMs >= 1200)
    {
        lastStatePublishMs = millis();
        publish_state();
        publish_gsm_logs();
    }
}
