/** shell.cpp - USB serial shell: local commands + GSM CLI passthrough. */
#include "shell.h"
#include "gsm_log.h"
#include "hub.h"
#include "net.h"
#include "store.h"
#include "uart_link.h"

static char line[192];
static uint16_t len = 0;

static void help()
{
    Serial.println("help                 this list");
    Serial.println("state                dump full state JSON");
    Serial.println("wifi <ssid> <pass>   set + save WiFi");
    Serial.println("server <domain>      set + save server domain");
    Serial.println("mirror on|off        toggle GSM log echo to serial");
    Serial.println("--- GSM CLI (forwarded to STM32 modem) ---");
    Serial.println("sms <num> <text>     send SMS");
    Serial.println("dial <num>           make a call");
    Serial.println("ussd <code>          send USSD code");
    Serial.println("at <cmd>             raw AT command");
    Serial.println("i s r d b p x h R K L  modem menu keys");
}

static void run(const char *cmd)
{
    if (!*cmd) return;

    if (strcmp(cmd, "help") == 0) { help(); return; }

    if (strcmp(cmd, "state") == 0)
    {
        JsonDocument doc;
        hub_to_json(doc);
        serializeJsonPretty(doc, Serial);
        Serial.println();
        return;
    }

    if (strncmp(cmd, "mirror ", 7) == 0)
    {
        bool on = strcmp(cmd + 7, "on") == 0;
        gsm_log_mirror_serial(on);
        Serial.printf("GSM log mirror %s\n", on ? "on" : "off");
        return;
    }

    if (strncmp(cmd, "wifi ", 5) == 0)
    {
        const char *rest = cmd + 5;
        while (*rest == ' ') rest++;
        char ssid[33] = "";
        const char *sp = strchr(rest, ' ');
        if (!sp) { Serial.println("usage: wifi <ssid> <pass>"); return; }
        strlcpy(ssid, rest, sp - rest + 1);
        while (*sp == ' ') sp++;
        apply_wifi(ssid, sp, true);
        store_save_wifi();
        net_start_connect();
        uart_send("SET:WIFI|%s|%s|1", ssid, sp);
        Serial.println("wifi saved, connecting...");
        return;
    }

    if (strncmp(cmd, "server ", 7) == 0)
    {
        const char *dom = cmd + 7;
        while (*dom == ' ') dom++;
        if (!*dom) { Serial.println("usage: server <domain>"); return; }
        apply_server(dom, hub.server_rate);
        store_save_server();
        uart_send("SET:SERVER|%s|%u", dom, (unsigned)hub.server_rate);
        Serial.println("server saved");
        return;
    }

    // forward to STM32 GSM CLI
    uart_send("GSM:CMD|%s", cmd);
    Serial.printf("[GSM-CMD] %s\n", cmd);
}

void shell_begin() { Serial.println("shell ready - type help"); }

void shell_poll()
{
    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r')
        {
            if (len > 0)
            {
                line[len] = '\0';
                run(line);
                len = 0;
            }
        }
        else if (len < sizeof(line) - 1)
        {
            line[len++] = c;
        }
        else
        {
            len = 0;
            Serial.println("line too long - discarded");
        }
    }
}
