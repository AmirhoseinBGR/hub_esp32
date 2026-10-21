#include "remote_settings.h"
#include "cloud_link.h"
#include "web_api.h"
#include "uart_link.h"
#include <ArduinoJson.h>

static uint32_t next_token, token, started;
static bool pending;
static char result[160];
static char cloud_command_id[64];

static bool valid_command(const char *cmd)
{
    if (!cmd) return false;
    const size_t len = strlen(cmd);
    if (!len || len > 150) return false;
    for (const unsigned char *p = (const unsigned char *)cmd; *p; p++)
        if (*p < 32 || *p > 126 || *p == '|') return false;
    return true;
}

static void expire()
{
    if (!pending || millis() - started <= 15000u) return;

    pending = false;
    strlcpy(result, "تأیید نشد؛ هاب پاسخ نداد", sizeof(result));
    if (cloud_command_id[0])
        cloud_link_send_result(cloud_command_id, false, result);
    cloud_command_id[0] = '\0';
}

bool remote_settings_start(const char *cmd, const char *cloud_id)
{
    expire();
    if (pending || !valid_command(cmd)) return false;
    if (cloud_id && strlen(cloud_id) >= sizeof(cloud_command_id)) return false;

    if (!next_token) next_token = millis();
    token = ++next_token;
    if (!token) token = ++next_token;
    started = millis();
    result[0] = '\0';
    pending = true;
    strlcpy(cloud_command_id, cloud_id ? cloud_id : "", sizeof(cloud_command_id));
    uart_send("CFG:REQ|%lu|%s", (unsigned long)token, cmd);
    return true;
}

void remote_settings_poll() { expire(); }

void remote_settings_result(const char *line)
{
    if (strncmp(line, "CFG:RES|", 8)) return;
    const char *p = line + 8;
    char *end = nullptr;
    unsigned long id = strtoul(p, &end, 10);
    if (end == p || *end != '|' || !pending || id != token) return;

    strlcpy(result, end + 1, sizeof(result));
    pending = false;
    if (cloud_command_id[0]) cloud_link_send_result(cloud_command_id, true, result);
    cloud_command_id[0] = '\0';
}

void remote_settings_routes()
{
    server.on("/api/settings", HTTP_POST, [] {
        expire();
        if (pending)
        {
            server.send(409, "application/json", "{\"error\":\"busy\"}");
            return;
        }

        JsonDocument req;
        if (deserializeJson(req, server.arg("plain")) || !req["cmd"].is<const char *>())
        {
            server.send(400, "application/json", "{\"error\":\"command required\"}");
            return;
        }
        const char *cmd = req["cmd"];
        if (!valid_command(cmd))
        {
            server.send(400, "application/json", "{\"error\":\"invalid command\"}");
            return;
        }
        if (!remote_settings_start(cmd, nullptr))
        {
            server.send(409, "application/json", "{\"error\":\"busy\"}");
            return;
        }
        // Accepted is NOT applied yet: the client polls for the correlated result.
        server.send(202, "application/json", String("{\"accepted\":true,\"token\":") + token + "}");
    });

    server.on("/api/settings", HTTP_GET, [] {
        expire();
        if (!server.hasArg("token") || strtoul(server.arg("token").c_str(), nullptr, 10) != token || !token)
        {
            server.send(404, "application/json", "{\"error\":\"unknown token\"}");
            return;
        }
        JsonDocument doc;
        doc["token"] = token;
        doc["pending"] = pending;
        if (!pending) doc["text"] = result;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json; charset=utf-8", out);
    });
}
