#include "index_html.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// ---------- Pin Definitions & Settings ----------
#define RXD2 16
#define TXD2 17

const char *AP_SSID = "ESP32_HUB_AP";
const char *AP_PASS = "12345678";

WebServer server(80);

// ---------- Hub State Cache ----------
bool g_sim_ok = true;
int32_t g_sim_credit = 25000;
String g_wifi_ssid = "";
String g_wifi_pass = "";
bool g_wifi_connected = false;
String g_server_domain = "deuteriumStudio.ir";
uint16_t g_server_rate = 12;

String g_sim_phone1 = "09120000000";
String g_sim_phone2 = "";
String g_sim_phone3 = "";

// External HTML String
extern const char INDEX_HTML[] PROGMEM;

// ---------- UART Protocol Helper to STM32 ----------
void sendToStm(const String &msg)
{
  Serial2.println(msg);
  Serial.print("[TO_STM] ");
  Serial.println(msg);
}

// ---------- Ping / Reachability Check ----------
bool checkServerPing(const String &domain)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return false;
  }

  HTTPClient http;
  String url = "http://" + domain + "/";

  http.begin(url);
  http.setTimeout(3500);

  int code = http.GET();

  http.end();

  return (code > 0);
}

// ---------- Web Server Endpoints ----------
void handleRoot()
{
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleStatusApi()
{
  String json = "{";

  json += "\"sim_ok\":";
  json += (g_sim_ok ? "true" : "false");

  json += ",\"credit\":";
  json += String(g_sim_credit);

  json += ",\"wifi_ssid\":\"";
  json += g_wifi_ssid;
  json += "\"";

  json += ",\"wifi_connected\":";
  json += (g_wifi_connected ? "true" : "false");

  json += ",\"sim_p1\":\"";
  json += g_sim_phone1;
  json += "\"";

  json += ",\"sim_p2\":\"";
  json += g_sim_phone2;
  json += "\"";

  json += ",\"sim_p3\":\"";
  json += g_sim_phone3;
  json += "\"";

  json += "}";

  server.send(
      200,
      "application/json; charset=utf-8",
      json);
}

void handleWifiScanApi()
{
  int n = WiFi.scanNetworks();

  String json = "{\"networks\":[";

  for (int i = 0; i < n && i < 10; ++i)
  {
    if (i > 0)
    {
      json += ",";
    }

    json += "\"";
    json += WiFi.SSID(i);
    json += "\"";
  }

  json += "]}";

  server.send(
      200,
      "application/json; charset=utf-8",
      json);

  // Also forward to STM32 UI list
  String listMsg = "WIFI:SCAN_RES:";

  for (int i = 0; i < n && i < 5; ++i)
  {
    if (i > 0)
    {
      listMsg += ",";
    }

    listMsg += WiFi.SSID(i);
  }

  sendToStm(listMsg);
}

void handleWifiConnectApi()
{
  if (!server.hasArg("plain"))
  {
    server.send(
        400,
        "application/json",
        "{\"error\":\"Bad request\"}");

    return;
  }

  String body = server.arg("plain");

  // Simple manual JSON parse for ssid & pass
  int idxSSID = body.indexOf("\"ssid\":\"");
  int idxPass = body.indexOf("\"pass\":\"");

  if (idxSSID >= 0)
  {
    int startSSID = idxSSID + 8;
    int endSSID = body.indexOf("\"", startSSID);

    if (endSSID > startSSID)
    {
      g_wifi_ssid = body.substring(startSSID, endSSID);
    }
  }

  if (idxPass >= 0)
  {
    int startPass = idxPass + 8;
    int endPass = body.indexOf("\"", startPass);

    if (endPass > startPass)
    {
      g_wifi_pass = body.substring(startPass, endPass);
    }
  }

  WiFi.begin(
      g_wifi_ssid.c_str(),
      g_wifi_pass.c_str());

  unsigned long t0 = millis();
  bool ok = false;

  while (millis() - t0 < 8000)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      ok = true;
      break;
    }

    delay(200);
  }

  g_wifi_connected = ok;

  if (ok)
  {
    sendToStm(
        "WIFI:STATUS:CONNECTED:" +
        g_wifi_ssid);

    server.send(
        200,
        "application/json; charset=utf-8",
        "{\"ok\":true,\"msg\":\"با موفقیت متصل شد\"}");
  }
  else
  {
    sendToStm(
        "WIFI:STATUS:DISCONNECTED");

    server.send(
        200,
        "application/json; charset=utf-8",
        "{\"ok\":false,\"msg\":\"اتصال برقرار نشد\"}");
  }
}

void handleServerPingApi()
{
  bool ok = checkServerPing(g_server_domain);

  if (ok)
  {
    sendToStm("SERVER:PING_RES:OK");

    server.send(
        200,
        "application/json",
        "{\"ok\":true}");
  }
  else
  {
    sendToStm(
        "SERVER:PING_RES:FAIL:NO_RESPONSE");

    server.send(
        200,
        "application/json",
        "{\"ok\":false}");
  }
}

void handleSimSaveApi()
{
  server.send(
      200,
      "application/json; charset=utf-8",
      "{\"ok\":true,\"msg\":\"شماره‌ها با موفقیت ذخیره شدند\"}");
}

void handleServerSaveApi()
{
  server.send(
      200,
      "application/json; charset=utf-8",
      "{\"ok\":true,\"msg\":\"تنظیمات سرور اعمال شد\"}");
}

// ---------- Parse Commands from STM32 ----------
void processStmCommand(const String &line)
{
  if (line.startsWith("WIFI:SCAN"))
  {
    int n = WiFi.scanNetworks();

    String listMsg = "WIFI:SCAN_RES:";

    for (int i = 0; i < n && i < 5; ++i)
    {
      if (i > 0)
      {
        listMsg += ",";
      }

      listMsg += WiFi.SSID(i);
    }

    sendToStm(listMsg);
  }
  else if (line.startsWith("WIFI:CONNECT:"))
  {
    int firstColon = 13;
    int secondColon = line.indexOf(':', firstColon);

    if (secondColon > 0)
    {
      g_wifi_ssid = line.substring(
          firstColon,
          secondColon);

      g_wifi_pass = line.substring(
          secondColon + 1);
    }
    else
    {
      g_wifi_ssid = line.substring(firstColon);
      g_wifi_pass = "";
    }

    WiFi.begin(
        g_wifi_ssid.c_str(),
        g_wifi_pass.c_str());

    unsigned long t0 = millis();
    bool ok = false;

    while (millis() - t0 < 8000)
    {
      if (WiFi.status() == WL_CONNECTED)
      {
        ok = true;
        break;
      }

      delay(200);
    }

    g_wifi_connected = ok;

    if (ok)
    {
      sendToStm(
          "WIFI:STATUS:CONNECTED:" +
          g_wifi_ssid);
    }
    else
    {
      sendToStm(
          "WIFI:STATUS:DISCONNECTED");
    }
  }
  else if (line.startsWith("SERVER:PING:"))
  {
    String domain = line.substring(12);

    if (domain.length() > 0)
    {
      g_server_domain = domain;
    }

    bool ok = checkServerPing(g_server_domain);

    if (ok)
    {
      sendToStm("SERVER:PING_RES:OK");
    }
    else
    {
      sendToStm(
          "SERVER:PING_RES:FAIL:ERROR");
    }
  }
  else if (line.startsWith("SERVER:SYNC"))
  {
    bool ok = checkServerPing(g_server_domain);

    if (ok)
    {
      sendToStm("SERVER:SYNC_RES:OK");
    }
    else
    {
      sendToStm("SERVER:SYNC_RES:FAIL");
    }
  }
}

// ---------- Arduino Lifecycle ----------
void setup()
{
  Serial.begin(115200);

  Serial2.begin(
      115200,
      SERIAL_8N1,
      RXD2,
      TXD2);

  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP(
      AP_SSID,
      AP_PASS);

  server.on(
      "/",
      handleRoot);

  server.on(
      "/api/status",
      handleStatusApi);

  server.on(
      "/api/wifi/scan",
      handleWifiScanApi);

  server.on(
      "/api/wifi/connect",
      HTTP_POST,
      handleWifiConnectApi);

  server.on(
      "/api/server/ping",
      handleServerPingApi);

  server.on(
      "/api/sim/save",
      HTTP_POST,
      handleSimSaveApi);

  server.on(
      "/api/server/save",
      HTTP_POST,
      handleServerSaveApi);

  server.begin();

  Serial.println("ESP32 System Ready!");
}

String rxLine = "";

void loop()
{
  server.handleClient();

  while (Serial2.available())
  {
    char c = (char)Serial2.read();

    if (c == '\n' || c == '\r')
    {
      if (rxLine.length() > 0)
      {
        processStmCommand(rxLine);
        rxLine = "";
      }
    }
    else
    {
      rxLine += c;
    }
  }
}