/** store.cpp - Preferences wrapper. Only WiFi + Server are owned by ESP32. */
#include "store.h"
#include "hub.h"
#include <Preferences.h>

static Preferences prefs;

void store_load()
{
    prefs.begin("hub", true);
    prefs.getString("wifi_ssid", hub.wifi_ssid, sizeof(hub.wifi_ssid));
    prefs.getString("wifi_pass", hub.wifi_pass, sizeof(hub.wifi_pass));
    hub.wifi_auto = prefs.getBool("wifi_auto", true);
    prefs.getString("srv_domain", hub.server_domain, sizeof(hub.server_domain));
    hub.server_rate = prefs.getUShort("srv_rate", 12);
    prefs.end();
}

void store_save_wifi()
{
    prefs.begin("hub", false);
    prefs.putString("wifi_ssid", hub.wifi_ssid);
    prefs.putString("wifi_pass", hub.wifi_pass);
    prefs.putBool("wifi_auto", hub.wifi_auto);
    prefs.end();
}

void store_save_server()
{
    prefs.begin("hub", false);
    prefs.putString("srv_domain", hub.server_domain);
    prefs.putUShort("srv_rate", hub.server_rate);
    prefs.end();
}
