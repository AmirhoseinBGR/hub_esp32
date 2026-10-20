/**
 * hub.h - single place for ALL variables of the system.
 * Settings + device states + link health + GSM modem live status.
 * If you add a variable: it lives here, then update hub_to_json() + one parser line.
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#define DEV_COUNT 16 /* STM32 UI_MACHINE_COUNT / hub_core HUB_DEVICE_COUNT */
#define PHONE_COUNT 3
#define SCAN_MAX 12
#define GSM_PEER_MAX 24

// One incubator: live values (from STM32) + settings (from STM32, editable from web)
struct Device
{
    // live state
    bool online = false;
    float temp = 0.0f;
    float hum = 0.0f;
    uint8_t rotation = 0; // 0=fault 1=left 2=right
    uint16_t alarms = 0;  // bit mask, 9 alarm kinds

    // settings
    int16_t sp_temp_x10 = 375; // 37.5 C
    int16_t sp_hum_x10 = 600;  // 60.0 %
    uint16_t alarm_enable = 0x00AF; // defaults match STM32 ui_settings
    bool paired = false;
};

// Live mirror of the GSM modem that runs on the STM32 (fed by GSM:ST lines)
struct GsmLive
{
    bool seen = false;          // received at least one GSM:ST
    char fsm[20] = "";          // modem state machine name
    char activity[16] = "";
    char step[16] = "";
    bool ready = false;         // modem fully up
    bool sim_ready = false;
    bool net_ready = false;
    char operator_name[20] = "";
    int rssi = 0;
    int bars = 0;
    uint16_t sms_used = 0;
    uint16_t sms_total = 0;
    bool queue_paused = false;
    uint8_t queue_size = 0;
    uint8_t queue_cap = 0;
    char call[14] = "idle";
    char peer[GSM_PEER_MAX] = "";
};

struct HubState
{
    // link health
    bool stm32_ok = false;
    uint32_t last_rx_ms = 0;

    // WiFi (owned by ESP32, saved in NVS)
    char wifi_ssid[33] = "";
    char wifi_pass[65] = "";
    bool wifi_auto = true;
    bool wifi_connected = false;
    char wifi_ip[16] = "";
    // connection progress, drives the web page error messages:
    // phase: 0 idle, 1 connecting, 2 connected, 3 failed
    // err:   0 none, 1 wrong password, 2 timeout / no network, 3 other
    // phase 3 is NOT final: net.cpp keeps auto-retrying with backoff until
    // the AP answers, new credentials arrive, or wifi_auto is switched off.
    uint8_t wifi_phase = 0;
    uint8_t wifi_err = 0;

    // Server (owned by ESP32, saved in NVS)
    char server_domain[64] = "deuteriumStudio.ir";
    uint16_t server_rate = 12; // sends per hour
    bool server_ok = false;

    // SIM (owned by STM32, cached here)
    bool sim_ok = false;
    int32_t sim_credit = 0;
    char sim_phone[PHONE_COUNT][12] = {"", "", ""};
    uint16_t sim_alarm_enable = 0x00AF;

    // WiFi scan cache (async, filled by net.cpp)
    bool scanning = false;
    uint8_t scan_count = 0;
    char scan_list[SCAN_MAX][33];

    GsmLive gsm;

    Device dev[DEV_COUNT];
};

extern HubState hub;

// Apply functions: return true when something actually changed.
bool apply_wifi(const char *ssid, const char *pass, bool auto_connect);
bool apply_server(const char *domain, uint16_t rate);
bool apply_sim(const char *p1, const char *p2, const char *p3, uint16_t mask);
bool apply_device(uint8_t id, int16_t sp_temp_x10, int16_t sp_hum_x10, uint16_t mask, bool paired);

// Serialize the whole state for GET /api/state
void hub_to_json(JsonDocument &doc);
