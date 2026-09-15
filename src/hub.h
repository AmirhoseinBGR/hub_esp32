/**
 * hub.h - single place for ALL variables of the system.
 * Settings + device states + link health. If you add a variable,
 * this is where it lives; then update hub_to_json() and one SET parser line.
 */
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#define DEV_COUNT 8
#define PHONE_COUNT 3

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

    // Server (owned by ESP32, saved in NVS)
    char server_domain[64] = "deuteriumStudio.ir";
    uint16_t server_rate = 12; // sends per hour
    bool server_ok = false;

    // SIM (owned by STM32, cached here)
    bool sim_ok = false;
    int32_t sim_credit = 0;
    char sim_phone[PHONE_COUNT][12] = {"", "", ""};
    uint16_t sim_alarm_enable = 0x00AF;

    Device dev[DEV_COUNT];
};

extern HubState hub;

// Apply functions: return true when something actually changed.
bool apply_wifi(const char *ssid, const char *pass, bool auto_connect);
bool apply_server(const char *domain, uint16_t rate);
void apply_sim(const char *p1, const char *p2, const char *p3, uint16_t mask);
bool apply_device(uint8_t id, int16_t sp_temp_x10, int16_t sp_hum_x10, uint16_t mask, bool paired);

// Serialize the whole state for GET /api/state
void hub_to_json(JsonDocument &doc);
