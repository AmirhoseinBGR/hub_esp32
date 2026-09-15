/** uart_link.cpp - protocol parser. Every message is one line: TYPE|f1|f2|... */
#include "uart_link.h"
#include "config.h"
#include "hub.h"
#include "net.h"
#include "store.h"
#include <HardwareSerial.h>
#include <WiFi.h>

static HardwareSerial stm(2);
static char rx_line[LINE_MAX];
static uint16_t rx_len = 0;

#ifdef HOST_TEST
HardwareSerial &uart_for_test() { return stm; } // host-side unit tests only
#endif

// Split "a|b|c" in place, returns field count
static int split_fields(char *s, char *out[], int max)
{
    int n = 0;
    out[n++] = s;
    for (char *p = s; *p && n < max; p++)
    {
        if (*p == '|')
        {
            *p = '\0';
            out[n++] = p + 1;
        }
    }
    return n;
}

static uint16_t hex16(const char *s) { return (uint16_t)strtol(s ? s : "0", NULL, 16); }

static void handle_set(char *body)
{
    char *f[6];
    int n = split_fields(body, f, 6);

    if (n >= 4 && strcmp(f[0], "WIFI") == 0)
    {
        if (apply_wifi(f[1], f[2], atoi(f[3]) != 0))
        {
            store_save_wifi();
            net_start_connect();
        }
    }
    else if (n >= 3 && strcmp(f[0], "SERVER") == 0)
    {
        if (apply_server(f[1], (uint16_t)atoi(f[2]))) store_save_server();
    }
    else if (n >= 5 && strcmp(f[0], "SIM") == 0)
    {
        apply_sim(f[1], f[2], f[3], hex16(f[4]));
    }
    else if (n >= 6 && strcmp(f[0], "DEV") == 0)
    {
        apply_device((uint8_t)atoi(f[1]), (int16_t)atoi(f[2]), (int16_t)atoi(f[3]), hex16(f[4]),
                     atoi(f[5]) != 0);
    }
}

static void handle_state(char *body)
{
    char *f[8];
    int n = split_fields(body, f, 8);

    hub.last_rx_ms = millis();
    hub.stm32_ok = true;

    if (n >= 7 && strcmp(f[0], "DEV") == 0)
    {
        int id = atoi(f[1]);
        if (id >= 1 && id <= DEV_COUNT)
        {
            Device &d = hub.dev[id - 1];
            d.online = atoi(f[2]) != 0;
            d.temp = atoi(f[3]) / 10.0f;
            d.hum = atoi(f[4]) / 10.0f;
            d.rotation = (uint8_t)atoi(f[5]);
            d.alarms = hex16(f[6]);
        }
    }
    else if (n >= 5 && strcmp(f[0], "HUB") == 0)
    {
        hub.sim_ok = atoi(f[1]) != 0;
        hub.sim_credit = atol(f[2]);
    }
}

static void handle_req(char *body)
{
    if (strcmp(body, "SCAN") == 0)
    {
        int n = WiFi.scanNetworks();
        char list[200] = "";
        for (int i = 0; i < n && i < 5; i++)
        {
            if (i > 0) strlcat(list, ",", sizeof(list));
            strlcat(list, WiFi.SSID(i).c_str(), sizeof(list));
        }
        uart_send("WIFI:SCAN_RES:%s", list);
    }
    else if (strcmp(body, "PING") == 0)
    {
        uart_send("SERVER:PING_RES:%s", net_ping_server() ? "OK" : "FAIL");
    }
    else if (strcmp(body, "CREDIT") == 0)
    {
        uart_send("CREDIT:%ld", (long)hub.sim_credit);
    }
}

static void handle_legacy(const char *line)
{
    // Old colon format, kept so mixed old/new firmware still works
    if (strcmp(line, "WIFI:SCAN") == 0) { handle_req((char *)"SCAN"); return; }
    if (strncmp(line, "WIFI:CONNECT:", 13) == 0)
    {
        const char *rest = line + 13;
        char ssid[33] = "", pass[65] = "";
        const char *colon = strchr(rest, ':');
        if (colon)
        {
            strlcpy(ssid, rest, colon - rest + 1);
            strlcpy(pass, colon + 1, sizeof(pass));
        }
        else strlcpy(ssid, rest, sizeof(ssid));
        if (apply_wifi(ssid, pass, true))
        {
            store_save_wifi();
            net_start_connect();
        }
        return;
    }
    if (strncmp(line, "SERVER:PING:", 12) == 0)
    {
        apply_server(line + 12, hub.server_rate);
        uart_send("SERVER:PING_RES:%s", net_ping_server() ? "OK" : "FAIL");
        return;
    }
    if (strcmp(line, "SERVER:SYNC") == 0)
    {
        uart_send("SERVER:SYNC_RES:%s", net_ping_server() ? "OK" : "FAIL");
        return;
    }
}

static void process_line(const char *line)
{
    if (line[0] == '\0') return;

    if (strncmp(line, "SET:", 4) == 0)
    {
        char body[LINE_MAX];
        strlcpy(body, line + 4, sizeof(body));
        handle_set(body);
    }
    else if (strncmp(line, "STATE:", 6) == 0)
    {
        char body[LINE_MAX];
        strlcpy(body, line + 6, sizeof(body));
        handle_state(body);
    }
    else if (strncmp(line, "REQ:", 4) == 0)
    {
        char body[LINE_MAX];
        strlcpy(body, line + 4, sizeof(body));
        handle_req(body);
    }
    else if (strcmp(line, "SYNC:END") == 0)
    {
        uart_send("SERVER:SYNC_RES:OK");
    }
    else
    {
        handle_legacy(line);
    }
}

void uart_begin()
{
    stm.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
}

void uart_poll()
{
    while (stm.available())
    {
        char c = (char)stm.read();
        if (c == '\n' || c == '\r')
        {
            if (rx_len > 0)
            {
                rx_line[rx_len] = '\0';
                process_line(rx_line);
                rx_len = 0;
            }
        }
        else if (rx_len < LINE_MAX - 1)
        {
            rx_line[rx_len++] = c;
        }
        else
        {
            rx_len = 0; // overflow, drop line
        }
    }

    if (hub.stm32_ok && millis() - hub.last_rx_ms > STM32_TIMEOUT_MS) hub.stm32_ok = false;
}

void uart_send(const char *fmt, ...)
{
    char buf[LINE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    stm.print(buf);
    stm.print("\r\n");
    Serial.printf("[TX] %s\n", buf);
}
