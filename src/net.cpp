/**
 * net.cpp - everything here is non-blocking so loop() never stalls
 * (the old WiFi.scanNetworks() sync call + blocking ping froze the board).
 *
 * Scan:    WiFi.scanNetworks(true) async, results polled in net_service().
 *          Watchdog + extra retries while STA associates keep it alive even
 *          when the saved SSID is missing or the password is wrong.
 * Connect: state machine with wrong-password / timeout detection. Reconnect
 *          uses exponential backoff and NEVER gives up for good: one failed
 *          attempt must not leave the hub offline until the next reboot.
 *          The core's built-in auto-reconnect (a no-delay retry storm that
 *          aborted running scans) is switched off - timing lives here.
 * Ping:    own FreeRTOS task, loop() is never blocked by HTTP.
 */
#include "net.h"
#include "config.h"
#include "hub.h"
#include "store.h"
#include "uart_link.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_wifi.h> // esp_wifi_scan_stop(): watchdog for a stuck async scan

// older arduino-esp32 cores may not define it
#ifndef WL_WRONG_PASSWORD
#define WL_WRONG_PASSWORD 8
#endif

#ifndef WIFI_STATUS_PERIOD_MS
#define WIFI_STATUS_PERIOD_MS 8000u
#endif

// Written from the WiFi event task as well - keep the flags volatile.
static volatile bool connecting = false;
static volatile bool attempt_failed = false; // wifi_event_cb(): the attempt is over
static volatile uint8_t fail_reason = 0;     // disconnect reason of that failure
static uint32_t connect_start = 0;
static uint32_t retry_at = 0;  // earliest millis() for the next automatic attempt
static uint8_t retry_step = 0; // backoff exponent: WIFI_RETRY_BASE_MS << retry_step
static uint32_t last_status_ms = 0;
static volatile bool ping_busy = false;

// Scan retry state — first scan often fails/returns empty while STA associates.
static uint8_t scan_attempts = 0;
static bool scan_retry_pending = false;
static uint32_t scan_retry_at = 0;
static uint32_t scan_start_ms = 0; // watchdog reference of the running async scan

static void scan_start_one(void);
static void start_attempt(void);

/**
 * Runs in the Arduino WiFi event task, NOT in loop(). The driver reports here
 * WHY the attempt died: polling WiFi.status() alone cannot tell a wrong
 * password from a missing AP and always burns the full WIFI_TIMEOUT_MS.
 */
static void wifi_event_cb(arduino_event_id_t event, arduino_event_info_t info)
{
    if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED || !connecting) return;

    const uint8_t reason = info.wifi_sta_disconnected.reason;
    if (reason == WIFI_REASON_ASSOC_LEAVE) return; // our own WiFi.disconnect(), not a failure

    fail_reason = reason;
    attempt_failed = true;
}

/** Disconnect reasons that mean the stored credentials were rejected. */
static bool is_auth_reason(uint8_t reason)
{
    switch (reason)
    {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return true;
        default:
            return false;
    }
}

/**
 * Arm the next automatic attempt with exponential backoff (5s, 10s, 20s, ...
 * capped by WIFI_RETRY_MAX_MS). A dead or wrong-password SSID must not
 * hammer the radio - the old fixed 20s loop starved the scan every time.
 */
static void schedule_retry()
{
    uint32_t delay_ms = WIFI_RETRY_BASE_MS;
    for (uint8_t i = 0; i < retry_step && delay_ms < WIFI_RETRY_MAX_MS; i++) delay_ms <<= 1;
    if (delay_ms > WIFI_RETRY_MAX_MS) delay_ms = WIFI_RETRY_MAX_MS;
    if (retry_step < 8) retry_step++;
    retry_at = millis() + delay_ms;
}

/** Shared tail of every failed attempt: report it, then schedule the next try. */
static void connect_fail(uint8_t err)
{
    connecting = false;
    attempt_failed = false;
    hub.wifi_phase = 3;
    hub.wifi_err = err;

    if (err == 1)
    {
        // Bad credentials never fix themselves: skip the early backoff steps
        // and let the ladder keep growing from there (40s, 80s, ... 2 min).
        if (retry_step < WIFI_RETRY_AUTH_STEP) retry_step = WIFI_RETRY_AUTH_STEP;
        uart_send("WIFI:STATUS:FAILED:AUTH");
    }
    else uart_send("WIFI:STATUS:FAILED:TIMEOUT");

    schedule_retry();
}

static void report(bool connected)
{
    hub.wifi_connected = connected;
    if (connected)
    {
        hub.wifi_phase = 2;
        hub.wifi_err = 0;
        retry_step = 0;  // connected: any future drop starts a fresh backoff ladder
        retry_at = millis();
        IPAddress ip = WiFi.localIP();
        strlcpy(hub.wifi_ip, ip.toString().c_str(), sizeof(hub.wifi_ip));
        uart_send("WIFI:STATUS:CONNECTED:%s", hub.wifi_ssid);
    }
    else
    {
        hub.wifi_ip[0] = '\0';
        uart_send("WIFI:STATUS:DISCONNECTED");
    }
}

/** Push current STA state to the STM32 UI. Safe to call at any time. */
void net_send_wifi_status()
{
    last_status_ms = millis();

    if (WiFi.status() == WL_CONNECTED)
    {
        hub.wifi_connected = true;
        hub.wifi_phase = 2;
        hub.wifi_err = 0;
        retry_step = 0; // connected: keep the ladder reset for the next drop
        IPAddress ip = WiFi.localIP();
        strlcpy(hub.wifi_ip, ip.toString().c_str(), sizeof(hub.wifi_ip));
        uart_send("WIFI:STATUS:CONNECTED:%s",
                  hub.wifi_ssid[0] != '\0' ? hub.wifi_ssid : WiFi.SSID().c_str());
    }
    else if (hub.wifi_phase == 3)
    {
        hub.wifi_connected = false;
        if (hub.wifi_err == 1)
            uart_send("WIFI:STATUS:FAILED:AUTH");
        else if (hub.wifi_err == 2)
            uart_send("WIFI:STATUS:FAILED:TIMEOUT");
        else
            uart_send("WIFI:STATUS:FAILED:OTHER");
    }
    else
    {
        hub.wifi_connected = false;
        uart_send("WIFI:STATUS:DISCONNECTED");
    }
}

/** Push the cached scan list so the device UI can paint immediately. */
void net_send_scan_cache()
{
    char list[220] = "";
    for (int i = 0; i < hub.scan_count && i < 5; i++)
    {
        if (i > 0) strlcat(list, ",", sizeof(list));
        strlcat(list, hub.scan_list[i], sizeof(list));
    }
    uart_send("WIFI:SCAN_RES:%s", list);
}

void net_begin()
{
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, "88888888");

    /* The core's auto reconnect fires on EVERY disconnect (missing AP included)
     * with no delay at all - that storm aborted running scans and hammered a
     * non-existent SSID forever. The backoff state machine below owns all
     * retry timing from here on. */
    WiFi.setAutoReconnect(false);
    WiFi.onEvent(wifi_event_cb, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    if (hub.wifi_auto && hub.wifi_ssid[0] != '\0') net_start_connect();
}

static void start_attempt(void)
{
    hub.wifi_phase = 1;
    hub.wifi_err = 0;
    attempt_failed = false;
    connecting = true;                        // BEFORE begin(): a failure can be fast
    connect_start = millis();
    retry_at = millis() + WIFI_RETRY_MAX_MS;  // parked until success/failure re-arms it
    WiFi.begin(hub.wifi_ssid, hub.wifi_pass);
}

void net_start_connect()
{
    if (hub.wifi_ssid[0] == '\0') return;

    /* WiFi.begin() would abort a scan still in flight (ESP_ERR_WIFI_STATE).
     * A user-requested connect wins here; its caller can scan again later. */
    if (hub.scanning || scan_retry_pending)
    {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        hub.scanning = false;
        scan_retry_pending = false;
        scan_attempts = 0;
    }

    retry_step = 0; // explicit (re)connect: fresh credentials deserve a fast try
    start_attempt();
}

void net_scan_start()
{
    if (hub.scanning || scan_retry_pending) return;

    scan_attempts = 0;
    scan_start_one();
}

static void scan_schedule_retry()
{
    scan_attempts++;
    /* While STA associates the radio is busy and scans come back empty more
     * often: give it extra passes before telling the STM32 "no networks". */
    const uint8_t max_attempts = connecting ? WIFI_SCAN_RETRIES_BUSY : WIFI_SCAN_RETRIES;
    if (scan_attempts >= max_attempts)
    {
        hub.scan_count = 0;
        uart_send("WIFI:SCAN_RES:");
        return;
    }

    scan_retry_pending = true;
    scan_retry_at      = millis() + WIFI_SCAN_RETRY_DELAY_MS;
}

static void scan_start_one()
{
    scan_retry_pending = false;
    WiFi.scanDelete();

    // Longer dwell per channel finds weak APs the default pass misses.
    const int r = WiFi.scanNetworks(true, false, false, WIFI_SCAN_MAX_MS_PER_CHAN);

    if (r == WIFI_SCAN_RUNNING)
    {
        hub.scanning = true;
        scan_start_ms = millis(); // watchdog reference
        return;
    }

    hub.scanning = false;

    if (r > 0)
    {
        hub.scan_count = 0;
        for (int i = 0; i < r && hub.scan_count < SCAN_MAX; i++)
        {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            strlcpy(hub.scan_list[hub.scan_count++], ssid.c_str(), 33);
        }
        WiFi.scanDelete();
        scan_attempts = 0;
        net_send_scan_cache();
        return;
    }

    // r == 0 (empty) or r < 0 (failed): retry before telling STM32 there is nothing.
    scan_schedule_retry();
}

static void scan_service()
{
    /* Watchdog: an async scan that never reports done would keep hub.scanning
     * set forever and net_scan_start() would refuse to run again. Force-stop
     * the driver and let the normal retry logic take over. */
    if (hub.scanning && millis() - scan_start_ms > WIFI_SCAN_TIMEOUT_MS)
    {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        hub.scanning = false;
        scan_schedule_retry();
        return;
    }

    if (scan_retry_pending)
    {
        if ((int32_t)(millis() - scan_retry_at) >= 0) scan_start_one();
        return;
    }

    if (!hub.scanning) return;

    // int16: dense areas report more than 127 APs, int8_t turned that into -1
    const int16_t r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING) return;

    hub.scanning = false;

    if (r <= 0)
    {
        WiFi.scanDelete();
        scan_schedule_retry();
        return;
    }

    hub.scan_count = 0;
    for (int i = 0; i < r && hub.scan_count < SCAN_MAX; i++)
    {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue; // skip hidden
        strlcpy(hub.scan_list[hub.scan_count++], ssid.c_str(), 33);
    }

    WiFi.scanDelete();

    // Truly empty after a "successful" complete still counts as a miss — retry.
    if (hub.scan_count == 0)
    {
        scan_schedule_retry();
        return;
    }

    scan_attempts = 0;
    net_send_scan_cache();

    if (WiFi.status() == WL_CONNECTED)
    {
        connecting = false;
        report(true);
    }
}

static void connect_service()
{
    if (!connecting) return;

    if (WiFi.status() == WL_CONNECTED)
    {
        connecting = false;
        report(true);
        return;
    }

    /* The driver already said the attempt is over - classify it right away
     * instead of burning the full timeout. The grace period ignores the
     * disconnect WiFi.begin() posts for the previous configuration. */
    if (attempt_failed && millis() - connect_start >= WIFI_ATTEMPT_MIN_MS)
    {
        uint8_t err = 3;
        if (is_auth_reason(fail_reason)) err = 1;                 // wrong password
        else if (fail_reason == WIFI_REASON_NO_AP_FOUND) err = 2; // SSID not in range
        connect_fail(err);
        return;
    }

    // newer cores expose a rejected password directly through the status
    if (WiFi.status() == WL_WRONG_PASSWORD)
    {
        connect_fail(1);
        return;
    }

    // fallback for failures the event task never reported (e.g. DHCP stall)
    if (millis() - connect_start > WIFI_TIMEOUT_MS)
    {
        connect_fail(WiFi.status() == WL_NO_SSID_AVAIL ? 2 : 3);
    }
}

void net_service()
{
    scan_service();
    connect_service();

    /* Automatic reconnect. Deliberately NOT gated on wifi_phase != 3 anymore:
     * a single failure (AP off at boot, one wrong password) used to disable
     * it for good until the next reboot. The backoff timestamp paces the
     * attempts and an in-flight scan always keeps the radio for itself. */
    if (!connecting && WiFi.status() != WL_CONNECTED && hub.wifi_auto &&
        hub.wifi_ssid[0] != '\0' && !hub.scanning && !scan_retry_pending &&
        (int32_t)(millis() - retry_at) >= 0)
    {
        start_attempt();
    }
    else if (!connecting && WiFi.status() == WL_CONNECTED && !hub.wifi_connected)
    {
        report(true);
    }
    else if (WiFi.status() != WL_CONNECTED && hub.wifi_connected)
    {
        hub.wifi_connected = false;
        hub.wifi_ip[0] = '\0';
        if (hub.wifi_phase != 3) hub.wifi_phase = 0; // keep a fresh failure reason
        // Link lost after a working connection: get back fast, then back off.
        retry_step = 0;
        retry_at = millis() + WIFI_RETRY_BASE_MS;
        uart_send("WIFI:STATUS:DISCONNECTED");
    }

    /* Periodic STATUS so the device Wi-Fi page can restore "متصل به X"
     * after re-entering even if the board was already up before STM32 booted. */
    if (millis() - last_status_ms >= WIFI_STATUS_PERIOD_MS)
        net_send_wifi_status();
}

bool net_ping_server()
{
    if (WiFi.status() != WL_CONNECTED || ping_busy)
    {
        if (!ping_busy) hub.server_ok = false;
        return hub.server_ok;
    }

    ping_busy = true;
    HTTPClient http;
    String url = String("http://") + hub.server_domain + "/";
    bool ok = false;
    if (http.begin(url))
    {
        http.setTimeout(4000);
        int code = http.GET();
        ok = (code > 0);
        http.end();
    }
    ping_busy = false;

    hub.server_ok = ok;
    return ok;
}

// Periodic reachability check runs in its own task: HTTP timeout
// can never freeze the web server or the UART link anymore.
static void ping_task(void *)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(PING_PERIOD_MS));
        net_ping_server();
    }
}

void net_ping_task_start()
{
    xTaskCreatePinnedToCore(ping_task, "ping", 4096, nullptr, 1, nullptr, 1);
}
