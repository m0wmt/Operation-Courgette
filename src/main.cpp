/**
 * Program to control a water pump to irrigate our courgette plants.
 *
 * Using an ESP32-S3-ZERO : https://www.waveshare.com/wiki/ESP32-S3-Zero
 * 
 * 
 */
#include <Arduino.h>
// Load Wi-Fi library
#include "lwip/apps/sntp.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "../include/config.h"
#include "../include/index.h"

#include "esp32-hal-timer.h"

static bool enableWiFi(void);
static void disableWiFi(void);
static void ntpTime(void);
static String processor(const String &var);
static void send_events_to_web_client(void);
static void update_local_time(void);

//#define RGB_BRIGHTNESS 10 // Change white brightness (max 255)

// the setup function runs once when you press reset or power the board
#ifdef RGB_BUILTIN
#undef RGB_BUILTIN
#endif
#define RGB_BUILTIN 21

// Set web server port number to 80
AsyncWebServer server(80);

// Create an Event Source on /events
AsyncEventSource events("/events");

// Variables
unsigned long last_time = 0;
bool irrigationState = false;
int waterLevel = 3;
char time_buffer[10];

void setup() {
    Serial.begin(115200);
    delay(5000); // delay for serial to begin, can be very slow to start serial output!


    Serial.println("\n##################################");
    Serial.println(F("ESP32 Information:"));
    Serial.printf("Internal Total Heap %d, Internal Used Heap %d, Internal Free Heap %d\n", ESP.getHeapSize(),
    ESP.getHeapSize()-ESP.getFreeHeap(), ESP.getFreeHeap()); Serial.printf("Sketch Size %d, Free Sketch Space %d\n",
    ESP.getSketchSize(), ESP.getFreeSketchSpace()); Serial.printf("SPIRam Total heap %d, SPIRam Free Heap %d\n",
    ESP.getPsramSize(), ESP.getFreePsram()); Serial.printf("Chip Model %s, ChipRevision %d, Cpu Freq %d, SDK Version %s\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(), ESP.getSdkVersion()); 
    Serial.printf("Flash Size %d, Flash Speed %d\n", ESP.getFlashChipSize(), ESP.getFlashChipSpeed());
    Serial.println("##################################\n");

    if (enableWiFi() == true) {

        //Serial.printf("IP Address: %s\n", WiFi.localIP().toString());

        ntpTime();

        Serial.println("Time setup complete");

        delay(500);

        disableWiFi();

    } else {
        Serial.println("Unable to connect to WiFi!");
    }

    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting Up AP (Access Point)...");

    WiFi.mode(WIFI_MODE_AP);

    // Taken from config.h
    WiFi.softAP(LOCAL_SSID, LOCAL_WIFI_PASSWORD);

    Serial.print("Acces Point Created: ");
    Serial.println(LOCAL_SSID);

    // If connection successful show IP address in serial monitor
    IPAddress IP = WiFi.softAPIP();
    Serial.print("LOCAL AP IP address: ");
    Serial.println(IP);

    // Handle Web Server
    server.on("/", HTTP_GET,
              [](AsyncWebServerRequest *request) { request->send(200, "text/html", MAIN_page, processor); });

    // Handle Web Server Events
    events.onConnect([](AsyncEventSourceClient *client) {
        if (client->lastId()) {
            Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
        }
        // send event with message "hello!", id current millis
        // and set reconnect delay to 1 second
        client->send("hello!", NULL, millis(), 10000);
    });

    // Handle LED toggle request
    server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("/toggle");

        irrigationState = !irrigationState;
        //digitalWrite(ledPin, ledState);
        request->send(200, "text/plain", irrigationState ? "ON" : "OFF");
    });

    // Handle LED state request
    server.on("/state", HTTP_GET,
              [](AsyncWebServerRequest *request) { request->send(200, "text/plain", irrigationState ? "ON" : "OFF"); });

    server.addHandler(&events);
    server.begin(); // Start server

    Serial.println("Program started");
    Serial.println("");

    //update_local_time();

    // Set up run time buffer to 5 seconds, waiting time above!
    //sprintf(time_buffer, "%02d:%02d:%02d", 0, 0, 5);
}

void loop() {
#ifdef RGB_BUILTIN
    // digitalWrite(RGB_BUILTIN, HIGH);   // Turn the RGB LED white
    neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, RGB_BRIGHTNESS, RGB_BRIGHTNESS); // Red
    delay(1000);

    neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off
    delay(1000);

    neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0); // Red
    delay(1000);

    neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0); // Green
    delay(1000);

    neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS); // Blue
    delay(1000);

    neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off / black
    delay(1000);

    // TESTING
    // waterLevel++;
    // irrigationState = !irrigationState;
    send_events_to_web_client();
#endif
}


/**
 * @brief Enable WiFi
 *
 * @return true if connected, else false
 */
static bool enableWiFi(void) {
    bool connected = true;
    uint8_t wifi_connect_counter = 0;

    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA); // switch off AP
    WiFi.setAutoReconnect(true);

    WiFi.begin(SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);

        wifi_connect_counter++;

        // Give the wifi a few seconds to connect
        if (wifi_connect_counter > 30) {
            connected = false;
            break;
        }
    }

    return connected;
}

/**
 * @brief Disable WiFi
 *
 */
static void disableWiFi(void) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    Serial.println("WiFi mode set to WIFI_OFF");
}

/**
 * @brief Connect to NTP time server for accurate time.
 *
 */
static void ntpTime(void) {
    // According to various forums configTime on the ESP32 does not honor the TX env
    // configTime(0, 0, SNTP_TIME_SERVER);

    // Set timezone - London for us
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    // configTime on the ESP32 does not honor the TZ env, unlike the ESP8266

    sntp_stop();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

    sntp_setservername(0, SNTP_TIME_SERVER);

    sntp_init();

    //update_local_time();

    Serial.println("Time set...");
}

/**
 * @brief Handle requests from the web page.
 */
static String processor(const String &var) {
    Serial.print("processor: ");
    Serial.println(var);

    if (var == "RUNTIME") {
        return String(time_buffer);
    }
    else if (var == "STATE") {
        return irrigationState ? "ON" : "OFF";
    } else if (var == "LEVEL") {
        waterLevel++;
        return String(waterLevel);
    }

    return String();
}

/**
 * @brief Send events to the web client so they can be viewed on the web page.
 */
static void send_events_to_web_client(void) {
    events.send(String(waterLevel++), "waterlevel", millis());

    // // So the user knows the application is still running!
    // last_time = millis();

    // sprintf(runtime_buffer, "%02d:%02d:%02d", (last_time / 1000) / 3600, ((last_time / 1000) % 3600) / 60,
    //         ((last_time / 1000) % 3600) % 60);

    update_local_time();
    Serial.print("Time: ");
    Serial.printf("%s\n", time_buffer);

    events.send(String(time_buffer), "runtime", millis());
}


static void update_local_time(void) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }

    // Update buffer with current time
    strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &timeinfo);
}
