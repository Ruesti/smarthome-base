#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

WebServer server(80);

struct DeviceInfo {
  String name = "ESP32";
  String type = "unknown";
  String ip;
  String mac;
} device;

void saveDeviceInfo() {
  DynamicJsonDocument doc(512);
  doc["name"] = device.name;
  doc["type"] = device.type;
  File file = LittleFS.open(DEVICE_FILE, "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("Geräteinfos gespeichert.");
  }
}

void loadDeviceInfo() {
  if (!LittleFS.exists(DEVICE_FILE)) return;
  File file = LittleFS.open(DEVICE_FILE, "r");
  if (file) {
    DynamicJsonDocument doc(512);
    deserializeJson(doc, file);
    device.name = doc["name"] | "ESP32";
    device.type = doc["type"] | "unknown";
    file.close();
    Serial.println("Geräteinfos geladen.");
  }
}

void saveWiFiConfig(String ssid, String password) {
  DynamicJsonDocument doc(256);
  doc["ssid"] = ssid;
  doc["password"] = password;
  File file = LittleFS.open(WIFI_FILE, "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("WLAN-Daten gespeichert.");
  }
}

bool connectWiFi() {
  if (!LittleFS.exists(WIFI_FILE)) return false;
  File file = LittleFS.open(WIFI_FILE, "r");
  if (!file) return false;

  DynamicJsonDocument doc(256);
  deserializeJson(doc, file);
  file.close();

  const char* ssid = doc["ssid"];
  const char* password = doc["password"];
  WiFi.begin(ssid, password);

  Serial.printf("Verbinde mit %s", ssid);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    device.ip = WiFi.localIP().toString();
    device.mac = WiFi.macAddress();
    Serial.println("WLAN verbunden: " + device.ip);
    return true;
  }

  Serial.println("WLAN-Verbindung fehlgeschlagen.");
  return false;
}

void setupWiFi() {
  if (LittleFS.exists("/wifi.json")) {
    File file = LittleFS.open("/wifi.json", "r");
    if (file) {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      if (!error) {
        const char* ssid = doc["ssid"];
        const char* password = doc["password"];

        Serial.printf("Versuche Verbindung zu WLAN: %s\n", ssid);
        WiFi.begin(ssid, password);

        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 20) {
          delay(500);
          Serial.print(".");
          retries++;
        }

        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\n✅ WLAN verbunden!");
          Serial.print("IP-Adresse: ");
          Serial.println(WiFi.localIP());
          return;
        } else {
          Serial.println("\n❌ Verbindung fehlgeschlagen.");
        }
      } else {
        Serial.println("❌ Fehler beim Lesen von wifi.json.");
      }
    }
  }

  // Fallback: Access Point
  bool success = WiFi.softAP("ESP32-Sensor");
  if (success) {
    Serial.println("📡 Access Point gestartet!");
    Serial.print("SSID: ");
    Serial.println("ESP32-Sensor");
    Serial.print("AP-IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("⚠️ Fehler beim Starten des Access Points.");
  }
}


void setupRoutes() {
  server.on("/info", HTTP_GET, []() {
    DynamicJsonDocument doc(512);
    doc["name"] = device.name;
    doc["type"] = device.type;
    doc["ip"] = device.ip;
    doc["mac"] = device.mac;

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
  });

  server.on("/config", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"No body\"}");
      return;
    }

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    if (doc.containsKey("name")) device.name = doc["name"].as<String>();
    if (doc.containsKey("type")) device.type = doc["type"].as<String>();
    if (doc.containsKey("ssid") && doc.containsKey("password")) {
      saveWiFiConfig(doc["ssid"], doc["password"]);
      saveDeviceInfo();
      server.send(200, "application/json", "{\"status\":\"Rebooting...\"}");
      delay(1000);
      ESP.restart();
      return;
    }

    saveDeviceInfo();
    server.send(200, "application/json", "{\"status\":\"Saved\"}");
  });
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nESP32 startet...");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS konnte nicht gestartet werden.");
    return;
  }

  loadDeviceInfo();
  setupWiFi();
  setupRoutes();
  server.begin();
  Serial.println("HTTP-Server läuft.");
}

void loop() {
  server.handleClient();
}
