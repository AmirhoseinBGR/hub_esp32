/** uart_link.cpp - protocol parser. Every message is one line: TYPE|f1|f2|... */
#include "uart_link.h"
#include "config.h"
#include "gsm_log.h"
#include "hub.h"
#include "net.h"
#include "store.h"
#include <HardwareSerial.h>
#include <WiFi.h>

static HardwareSerial stm(2);
static char rx_line[PROTO_LINE_MAX];
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
        const bool changed = apply_wifi(f[1], f[2], atoi(f[3]) != 0);
        if (changed) store_save_wifi();

        // Same credentials but link down (missed STATUS / keepalive echo)
        // must still retry — otherwise SET:WIFI is a silent no-op.
        if (changed || (hub.wifi_ssid[0] != '\0' && WiFi.status() != WL_CONNECTED))
            net_start_connect();

        net_send_wifi_status();
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
    else if (n >= 3 && strcmp(f[0], "HUB") == 0)
    {
        hub.sim_ok = atoi(f[1]) != 0;
        hub.sim_credit = atol(f[2]);
    }
}

// GSM:ST|fsm|act|step|ready|sim|net|operator|rssi|bars|smsu|smst|paused|qsize|qcap|call|peer
static void handle_gsm_st(char *body)
{
    char *f[17];
    int n = split_fields(body, f, 17);
    if (n < 16) return;

    GsmLive &g = hub.gsm;
    g.seen = true;
    strlcpy(g.fsm, f[0], sizeof(g.fsm));
    strlcpy(g.activity, f[1], sizeof(g.activity));
    strlcpy(g.step, f[2], sizeof(g.step));
    g.ready = atoi(f[3]) != 0;
    g.sim_ready = atoi(f[4]) != 0;
    g.net_ready = atoi(f[5]) != 0;
    strlcpy(g.operator_name, f[6], sizeof(g.operator_name));
    g.rssi = atoi(f[7]);
    g.bars = atoi(f[8]);
    g.sms_used = (uint16_t)atoi(f[9]);
    g.sms_total = (uint16_t)atoi(f[10]);
    g.queue_paused = atoi(f[11]) != 0;
    g.queue_size = (uint8_t)atoi(f[12]);
    g.queue_cap = (uint8_t)atoi(f[13]);
    strlcpy(g.call, f[14], sizeof(g.call));
    strlcpy(g.peer, f[15], sizeof(g.peer));

    hub.last_rx_ms = millis();
    hub.stm32_ok = true;
}

static void handle_req(char *body)
{
    if (strcmp(body, "SCAN") == 0)
    {
        // Device WiFi page opened: status first so "متصل به X" paints
        // immediately, then any cached list, then a fresh scan.
        net_send_wifi_status();
        if (hub.scan_count > 0 && !hub.scanning) net_send_scan_cache();
        net_scan_start();
    }
    else if (strcmp(body, "PING") == 0)
        uart_send("SERVER:PING_RES:%s", net_ping_server() ? "OK" : "FAIL");
    else if (strcmp(body, "CREDIT") == 0)
        uart_send("CREDIT:%ld", (long)hub.sim_credit);
}

static void handle_legacy(const char *line)
{
    if (strcmp(line, "WIFI:SCAN") == 0)
    {
        net_send_wifi_status();
        if (hub.scan_count > 0 && !hub.scanning) net_send_scan_cache();
        net_scan_start();
        return;
    }
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
        const bool changed = apply_wifi(ssid, pass, true);
        if (changed) store_save_wifi();
        if (changed || (hub.wifi_ssid[0] != '\0' && WiFi.status() != WL_CONNECTED))
            net_start_connect();
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

    if (strncmp(line, "GSM:LOG|", 8) == 0)
    {
        gsm_log_add(line + 8);
    }
    else if (strncmp(line, "GSM:ST|", 7) == 0)
    {
        char body[PROTO_LINE_MAX];
        strlcpy(body, line + 7, sizeof(body));
        handle_gsm_st(body);
    }
    else if (strncmp(line, "SET:", 4) == 0)
    {
        char body[PROTO_LINE_MAX];
        strlcpy(body, line + 4, sizeof(body));
        handle_set(body);
    }
    else if (strncmp(line, "STATE:", 6) == 0)
    {
        char body[PROTO_LINE_MAX];
        strlcpy(body, line + 6, sizeof(body));
        handle_state(body);
    }
    else if (strncmp(line, "REQ:", 4) == 0)
    {
        char body[PROTO_LINE_MAX];
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
    stm.setRxBufferSize(1024); // must be before begin()
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
        else if (rx_len < PROTO_LINE_MAX - 1)
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
    char buf[PROTO_LINE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    stm.print(buf);
    stm.print("\r\n");
    Serial.printf("[TX] %s\n", buf);
}
