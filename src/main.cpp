#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ---------- Access Point (Hotspot) settings ----------
const char *AP_SSID = "ESP32_AP"; // Hotspot network name
const char *AP_PASS = "12345678"; // Password (at least 8 characters)

// ---------- Serial2 settings ----------
// According to ESP32 docs: GPIO16 = RX2, GPIO17 = TX2
#define RXD2 16
#define TXD2 17

// ---------- Create web server on port 80 ----------
WebServer server(80);

// ---------- Handler for root request (/) ----------
void handleRoot()
{
  // Send "سلام" (Salam) response to the browser
  server.send(200, "text/plain; charset=utf-8", "سلام");
}

void setup()
{
  // 1. Initialize main serial (USB) for communication with PC
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 Started");

  // 2. Initialize Serial2 on pins 16 and 17
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  Serial.println("Serial2 started (RX:16, TX:17)");

  // 3. Create hotspot (Access Point)
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP()); // Usually 192.168.4.1

  // 4. Define web server routes
  server.on("/", handleRoot);
  server.begin();
  Serial.println("Web server started");
}

void loop()
{
  // ---------- Bridge data between Serial and Serial2 ----------
  // Anything received from USB (main Serial) is forwarded to Serial2
  while (Serial.available() > 0)
  {
    Serial2.write(Serial.read());
  }
  // Anything received from Serial2 is forwarded to USB (main Serial)
  while (Serial2.available() > 0)
  {
    Serial.write(Serial2.read());
  }

  // ---------- Handle web client requests ----------
  server.handleClient();
}