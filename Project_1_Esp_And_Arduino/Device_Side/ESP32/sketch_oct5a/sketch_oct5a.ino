#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// ==============================================================================
// CONFIGURATION
// ==============================================================================
// WiFi Credentials
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// WebSocket Server Settings
const char* ws_host = "combining-caused-achieving-numerous.trycloudflare.com";
const int   ws_port = 443;
const char* ws_path = "/readings/ws";

// Hardware Serial Pins (Connecting to Arduino via TXS0108E Level Shifter)
#define RX2_PIN 16 // ESP32 RX2 -> Level Shifter HV -> Arduino TX
#define TX2_PIN 17 // ESP32 TX2 -> Level Shifter HV -> Arduino RX

// ==============================================================================
// INITIALIZATION
// ==============================================================================
WebSocketsClient webSocket;

unsigned long lastPoll = 0;
const unsigned long pollInterval = 10000; // Poll Arduino for fresh data every 10 seconds

// Forward declaration
void parseAndForwardArduinoData(String csvData);
void sendCommandToArduino(String cmd);

// WebSocket event handler
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected!");
            break;
            
        case WStype_CONNECTED:
            Serial.printf("[WS] Connected to URL: %s\n", payload);
            break;
            
        case WStype_TEXT: {
            Serial.printf("[WS] Command received from server: %s\n", payload);
            
            // Check if server sent raw text command or JSON object
            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            
            if (!err && doc.containsKey("command")) {
                // Example JSON from server: {"command": "FORCE_WATER_1"}
                const char* cmd = doc["command"];
                sendCommandToArduino(String(cmd));
            } else {
                // Fallback for plain text commands (e.g., "FORCE_WATER_1")
                String cmd = String((char*)payload);
                cmd.trim();
                sendCommandToArduino(cmd);
            }
            break;
        }
            
        case WStype_ERROR:
            Serial.printf("[WS] Error: %s\n", payload ? (char*)payload : "unknown");
            break;
            
        default:
            break;
    }
}

void setup() {
    // Hardware Debug Serial (USB to PC)
    Serial.begin(115200);
    
    // Hardware Serial2 (ESP32 <-> Arduino TXS0108E level shifter)
    Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);

    // Connect to WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected. IP: ");
    Serial.println(WiFi.localIP());

    // Configure Secure WebSocket Connection
    webSocket.beginSSL(ws_host, ws_port, ws_path);
    webSocket.setExtraHeaders("ngrok-skip-browser-warning: true");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
}

void loop() {
    webSocket.loop();

    // 1. Listen for incoming CSV messages from Arduino
    if (Serial2.available() > 0) {
        String csvData = Serial2.readStringUntil('\n');
        csvData.trim();
        if (csvData.length() > 0) {
            parseAndForwardArduinoData(csvData);
        }
    }

    // 2. Periodically request full telemetry data from Arduino
    if (millis() - lastPoll >= pollInterval) {
        lastPoll = millis();
        sendCommandToArduino("REQ_DATA");
    }
}

// ==============================================================================
// HELPER FUNCTIONS
// ==============================================================================

// Parse CSV format from Arduino: EVENT,LIGHT,M1,M2,TEMP
void parseAndForwardArduinoData(String csvData) {
    Serial.print("[Arduino -> ESP32 Raw]: ");
    Serial.println(csvData);

    // Split CSV tokens
    int comma1 = csvData.indexOf(',');
    int comma2 = csvData.indexOf(',', comma1 + 1);
    int comma3 = csvData.indexOf(',', comma2 + 1);
    int comma4 = csvData.indexOf(',', comma3 + 1);

    if (comma1 == -1 || comma2 == -1 || comma3 == -1 || comma4 == -1) {
        Serial.println("[Error] Invalid CSV structure received from Arduino");
        return;
    }

    String eventStr = csvData.substring(0, comma1);
    int light = csvData.substring(comma1 + 1, comma2).toInt();
    int m1 = csvData.substring(comma2 + 1, comma3).toInt();
    int m2 = csvData.substring(comma3 + 1, comma4).toInt();
    float temp = csvData.substring(comma4 + 1).toFloat();

    // Convert values and format JSON for server payload
    StaticJsonDocument<256> doc;
    doc["event"] = eventStr;
    doc["light"] = light; // 0 - 1023 analog reading
    doc["moisture_1"] = m1;
    doc["moisture_2"] = m2;
    doc["temperature"] = temp;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Transmit JSON over WebSocket
    if (webSocket.isConnected()) {
        webSocket.sendTXT(jsonPayload);
        Serial.print("[ESP32 -> WS Sent]: ");
        Serial.println(jsonPayload);
    } else {
        Serial.println("[WS] Not connected. Data dropped.");
    }
}

// Forward string command over Serial2 to Arduino
void sendCommandToArduino(String cmd) {
    Serial.print("[ESP32 -> Arduino Command]: ");
    Serial.println(cmd);
    Serial2.println(cmd);
}