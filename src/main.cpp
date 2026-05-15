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

char time_buffer[10];

typedef struct __attribute__((packed, aligned(1))) {
    uint8_t mode;
    uint16_t recordings;
    uint64_t disk_remaining;
} teensy_data_t;

teensy_data_t audio_guestbook_data;

typedef enum { // State of the audio guestbook
    ERROR,
    INITIALISING,
    READY,
    RECORDMESSAGEPROMPT,
    RECORDING,
    PLAYING
} button_mode_t;
// end of teensy information setup

// Set web server port number to 80
AsyncWebServer server(80);

// Create an Event Source on /events
AsyncEventSource events("/events");

// Variables
unsigned long last_time = 0;
char runtime_buffer[10];


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

        Serial.printf("IP Address: %s\n", WiFi.localIP().toString());

        ntpTime();

        Serial.println("WiFi time setup complete...");

        disableWiFi();

    } else {
        Serial.println("Unable to connect to WiFi!");
    }

    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)…");

    WiFi.mode(WIFI_MODE_AP);

    // Taken from config.h
    WiFi.softAP(LOCAL_SSID, LOCAL_WIFI_PASSWORD);

    Serial.print("Connecting to: ");
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
    server.addHandler(&events);
    server.begin(); // Start server

    Serial.println("Local HTTP server started");
    Serial.println("");

    // Set up run time buffer to 5 seconds, waiting time above!
    sprintf(runtime_buffer, "%02d:%02d:%02d", 0, 0, 5);

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

    Serial.println("Time set...");
}

/**
 * @brief Handle requests from the web page.
 */
static String processor(const String &var) {
    Serial.println(var);

    if (var == "DISKSPACE") {
        return String(audio_guestbook_data.disk_remaining);
    } else if (var == "STATUS") {
        if (audio_guestbook_data.mode == READY) {
            return "READY";
        } else if (audio_guestbook_data.mode == RECORDING || audio_guestbook_data.mode == RECORDMESSAGEPROMPT) {
            return "RECORDING";
        } else if (audio_guestbook_data.mode == PLAYING) {
            return "PLAYING";
        } else if (audio_guestbook_data.mode == INITIALISING) {
            return "INITIALISING";
        } else {
            return "ERROR";
        }
    } else if (var == "RECORDINGS") {
        return String(audio_guestbook_data.recordings);
    } else if (var == "RUNTIME") {
        return String(runtime_buffer);
    }

    return String();
}

/**
 * @brief Send events to the web client so they can be viewed on the web page.
 */
static void send_events_to_web_client(void) {
    events.send("ping", NULL, millis());
    events.send(String(audio_guestbook_data.disk_remaining).c_str(), "diskspace", millis());

    if (audio_guestbook_data.mode == READY) {
        events.send("READY", "status", millis());
    } else if (audio_guestbook_data.mode == RECORDING || audio_guestbook_data.mode == RECORDMESSAGEPROMPT) {
        events.send("RECORDING", "status", millis());
    } else if (audio_guestbook_data.mode == PLAYING) {
        events.send("PLAYING", "status", millis());
    } else if (audio_guestbook_data.mode == INITIALISING) {
        events.send("INITIALISING", "status", millis());
    } else {
        events.send("ERROR", "status", millis());
    }

    events.send(String(audio_guestbook_data.recordings).c_str(), "recordings", millis());

    // So the user knows the application is still running!
    last_time = millis();

    sprintf(runtime_buffer, "%02d:%02d:%02d", (last_time / 1000) / 3600, ((last_time / 1000) % 3600) / 60,
            ((last_time / 1000) % 3600) % 60);

    Serial.print("Run time: ");
    Serial.println(runtime_buffer);

    events.send(String(runtime_buffer), "runtime", millis());
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
