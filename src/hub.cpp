/** hub.cpp - state storage + apply functions + JSON serialization. */
#include "hub.h"
#include "config.h"

HubState hub;

bool apply_wifi(const char *ssid, const char *pass, bool auto_connect)
{
    if (!ssid) return false;
    bool changed = strcmp(hub.wifi_ssid, ssid) != 0 || strcmp(hub.wifi_pass, pass ? pass : "") != 0 ||
                   hub.wifi_auto != auto_connect;

    strlcpy(hub.wifi_ssid, ssid, sizeof(hub.wifi_ssid));
    strlcpy(hub.wifi_pass, pass ? pass : "", sizeof(hub.wifi_pass));
    hub.wifi_auto = auto_connect;
    return changed;
}

bool apply_server(const char *domain, uint16_t rate)
{
    if (!domain || domain[0] == '\0') return false;
    bool changed = strcmp(hub.server_domain, domain) != 0 || hub.server_rate != rate;

    strlcpy(hub.server_domain, domain, sizeof(hub.server_domain));
    if (rate > 0) hub.server_rate = rate;
    return changed;
}

bool apply_sim(const char *p1, const char *p2, const char *p3, uint16_t mask)
{
    bool changed = false;
    const char *p[PHONE_COUNT] = {p1, p2, p3};
    for (int i = 0; i < PHONE_COUNT; i++)
    {
        if (strcmp(hub.sim_phone[i], p[i] ? p[i] : "") != 0)
        {
            strlcpy(hub.sim_phone[i], p[i] ? p[i] : "", sizeof(hub.sim_phone[i]));
            changed = true;
        }
    }
    if (hub.sim_alarm_enable != mask)
    {
        hub.sim_alarm_enable = mask;
        changed = true;
    }
    return changed;
}

bool apply_device(uint8_t id, int16_t sp_temp_x10, int16_t sp_hum_x10, uint16_t mask, bool paired)
{
    if (id < 1 || id > DEV_COUNT) return false;
    Device &d = hub.dev[id - 1];
    bool changed = d.sp_temp_x10 != sp_temp_x10 || d.sp_hum_x10 != sp_hum_x10 ||
                   d.alarm_enable != mask || d.paired != paired;

    d.sp_temp_x10 = sp_temp_x10;
    d.sp_hum_x10 = sp_hum_x10;
    d.alarm_enable = mask;
    d.paired = paired;
    return changed;
}

void hub_to_json(JsonDocument &doc)
{
    doc["link"]["stm32"] = hub.stm32_ok;
    doc["link"]["age_ms"] = hub.last_rx_ms ? (uint32_t)(millis() - hub.last_rx_ms) : 0;

    JsonObject w = doc["wifi"].to<JsonObject>();
    w["ssid"] = hub.wifi_ssid;
    w["pass_set"] = hub.wifi_pass[0] != '\0';
    w["auto"] = hub.wifi_auto;
    w["connected"] = hub.wifi_connected;
    w["ip"] = hub.wifi_ip;
    w["ap"] = AP_SSID;
    w["phase"] = hub.wifi_phase;
    w["err"] = hub.wifi_err;

    JsonObject s = doc["server"].to<JsonObject>();
    s["domain"] = hub.server_domain;
    s["rate"] = hub.server_rate;
    s["ok"] = hub.server_ok;

    JsonObject sim = doc["sim"].to<JsonObject>();
    sim["ok"] = hub.sim_ok;
    sim["credit"] = hub.sim_credit;
    JsonArray ph = sim["phones"].to<JsonArray>();
    for (int i = 0; i < PHONE_COUNT; i++) ph.add(hub.sim_phone[i]);
    sim["mask"] = hub.sim_alarm_enable;

    JsonObject g = doc["gsm"].to<JsonObject>();
    g["seen"] = hub.gsm.seen;
    g["fsm"] = hub.gsm.fsm;
    g["act"] = hub.gsm.activity;
    g["step"] = hub.gsm.step;
    g["ready"] = hub.gsm.ready;
    g["sim"] = hub.gsm.sim_ready;
    g["net"] = hub.gsm.net_ready;
    g["op"] = hub.gsm.operator_name;
    g["rssi"] = hub.gsm.rssi;
    g["bars"] = hub.gsm.bars;
    g["sms_used"] = hub.gsm.sms_used;
    g["sms_total"] = hub.gsm.sms_total;
    g["paused"] = hub.gsm.queue_paused;
    g["qsize"] = hub.gsm.queue_size;
    g["qcap"] = hub.gsm.queue_cap;
    g["call"] = hub.gsm.call;
    g["peer"] = hub.gsm.peer;

    JsonArray devs = doc["devices"].to<JsonArray>();
    for (int i = 0; i < DEV_COUNT; i++)
    {
        const Device &d = hub.dev[i];
        JsonObject o = devs.add<JsonObject>();
        o["id"] = i + 1;
        o["online"] = d.online;
        o["temp"] = d.temp;
        o["hum"] = d.hum;
        o["sp_t"] = d.sp_temp_x10 / 10.0f;
        o["sp_h"] = d.sp_hum_x10 / 10.0f;
        o["rot"] = d.rotation;
        o["alarms"] = d.alarms;
        o["en"] = d.alarm_enable;
        o["paired"] = d.paired;
    }
}
