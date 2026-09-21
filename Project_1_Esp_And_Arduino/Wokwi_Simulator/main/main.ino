#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ==============================================================================
// CONFIGURATION
// ==============================================================================
// WiFi settings
// WiFi settings for Wokwi Simulator
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// WebSocket Server settings
// If testing locally on your network, use your machine's local IP (e.g., 192.168.1.X)
// If using a tunnel (like Cloudflare), use the domain without the 'ws://' prefix
const char* ws_host = "192.168.1.100"; 
const int   ws_port = 8000;
const char* ws_path = "/readings/ws";

// Sensor Pin Definitions
#define DHTPIN 4      // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22 // DHT 22 (AM2302)
#define LDR_PIN 34    // Analog pin connected to the LDR

// ==============================================================================
// INITIALIZATION
// ==============================================================================
DHT dht(DHTPIN, DHTTYPE);
WebSocketsClient webSocket;

unsigned long lastUpdate = 0;
const unsigned long updateInterval = 5000; // Send reading every 5 seconds

// WebSocket event handler
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("[WS] Disconnected!");
            break;
        case WStype_CONNECTED:
            Serial.printf("[WS] Connected to url: %s\n", payload);
            break;
        case WStype_TEXT:
            Serial.printf("[WS] Message from server: %s\n", payload);
            break;
        case WStype_BIN:
        case WStype_ERROR:			
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize Sensors
    dht.begin();
    pinMode(LDR_PIN, INPUT);

    // Connect to WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected. IP address: ");
    Serial.println(WiFi.localIP());

    // Setup WebSocket
    webSocket.begin(ws_host, ws_port, ws_path);
    webSocket.onEvent(webSocketEvent);
    
    // Automatically attempt to reconnect if connection drops
    webSocket.setReconnectInterval(5000);
}

void loop() {
    webSocket.loop();

    // Non-blocking delay to send data periodically
    if (millis() - lastUpdate > updateInterval) {
        lastUpdate = millis();

        // Read sensors
        float h = dht.readHumidity();
        float t = dht.readTemperature();
        int lightValue = analogRead(LDR_PIN); // 0-4095 range on ESP32

        // Check if any DHT reads failed
        bool error = isnan(h) || isnan(t);
        
        // Prepare JSON Document
        // Based on the ReadingRequest schema in your FastAPI backend
        StaticJsonDocument<256> doc;
        
        if (error) {
            Serial.println("Failed to read from DHT sensor!");
            doc["error_message"] = "DHT22 read failure";
        } else {
            // We omit time/date so the server automatically populates them
            doc["temperature"] = t;
            doc["humidity"] = h;
            
            // Convert ADC 12-bit value (0-4095) to a percentage (0-100%) for easier frontend display
            doc["light"] = (lightValue / 4095.0) * 100.0; 
        }

        // Serialize and send via WebSocket
        String payload;
        serializeJson(doc, payload);
        
        if(webSocket.isConnected()) {
            webSocket.sendTXT(payload);
            Serial.print("Sent payload: ");
            Serial.println(payload);
        } else {
            Serial.println("WebSocket not connected. Payload not sent.");
        }
    }
}