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
static void print_state(void);

//#define RGB_BRIGHTNESS 10 // Change white brightness (max 255)

// the setup function runs once when you press reset or power the board
#ifdef RGB_BUILTIN
#undef RGB_BUILTIN
#endif
#define RGB_BUILTIN 21

#define IRRIGATION_GPIO_PIN 7

// Set web server port number to 80
AsyncWebServer server(80);

// Create an Event Source on /events
AsyncEventSource events("/events");

// Variables
unsigned long last_time = 0;
bool irrigationState = false;
int waterLevel = 3;
char time_buffer[10];
uint32_t watering_timer = 0;    // How long we've been watering for
uint16_t update_timer = 0;      // How long we've been going - triggers when == update_webpage - ## upgrade to uint32_t if larger than 1 minute ##

// Schedule time to start watering - 15:50 every day
typedef struct WateringScheduleStruct {
    uint8_t const hour = 10;
    uint8_t const minute = 15;
    bool wateringStarted = false; // stop multiple firings
} WateringScheduleStruct;

WateringScheduleStruct watering;

typedef enum { // Keep track of current state of the device
    ERROR,
    INITIALISING,
    READY,
    START_WATERING,
    WATERING,
    STOP_WATERING
} irrigation_state_t;
irrigation_state_t state = INITIALISING;

static const uint16_t update_webpage = 30000;   // Update web page every minute (60000). ## Any more than 1 min then upgrade to uint32_t ##
static const uint32_t stop_watering = 120000;   // Watering time limit (in milliseconds) - 5 mins (300000)
// 60000 = 1 min
// 120000 =  2 mins
// 240000 = 4 mins
// 600000 = 10 mins

void setup() {
    // Define pin we're using to control water pump and turn it off
    pinMode(IRRIGATION_GPIO_PIN, OUTPUT);
    digitalWrite(IRRIGATION_GPIO_PIN, LOW);
    neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off

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

    Serial.println("Irrigation pin set low");

    state = INITIALISING;
    print_state();

    // Enable wifi so we can retrieve the real time and update our local time.
    if (enableWiFi() == true) {
        // ipAddress = WiFi.localIP().toString();
        // Serial.printf("IP Address: %s\n", ipAddress);
        Serial.printf("IP Address: ");
        Serial.println(WiFi.localIP());

        //ntpTime();
        configTime(0, 3600, SNTP_TIME_SERVER);
        //printLocalTime();
        update_local_time();
        
        Serial.println("Time setup complete");

        delay(500);

        // Disable wifi as we're going to set up our own local access point
        disableWiFi();

    } else {
        Serial.println("Unable to connect to WiFi!");
        state = ERROR;
        print_state();
    }

    //printLocalTime();

    // Set up our own local Wi-Fi network with SSID and password
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
            Serial.printf("%s Client reconnected, message ID: %u\n", time_buffer, client->lastId());
        }
        // send event with message "hello!", id current millis
        // and set reconnect delay to 1 second
        client->send("hello!", NULL, millis(), 10000);
    });

    // Handle LED toggle request
    server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Serial.println("/toggle");

        irrigationState = !irrigationState;
        state = START_WATERING;
        print_state();
        //digitalWrite(ledPin, ledState);
        request->send(200, "text/plain", irrigationState ? "ON" : "OFF");
    });

    // Handle LED state request
    server.on("/state", HTTP_GET,
              [](AsyncWebServerRequest *request) { request->send(200, "text/plain", irrigationState ? "WATERING" : "OFF"); });

    server.addHandler(&events);
    server.begin(); // Start server

    digitalWrite(IRRIGATION_GPIO_PIN, LOW);
    Serial.println("Irrigation pin set low");

    Serial.println("Program started");
    //update_local_time();
    Serial.println("");

    send_events_to_web_client();

    //update_local_time();

    // Set up run time buffer to 5 seconds, waiting time above!
    //sprintf(time_buffer, "%02d:%02d:%02d", 0, 0, 5);

}

void loop() {
    static uint32_t webTimer = 0;
    static uint32_t wateringTimer = 0;

    // #ifdef RGB_BUILTIN
    //     delay(4000);
    //     neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0); // Red

    //     Serial.println("Pin set high");
    //     digitalWrite(IRRIGATION_GPIO_PIN, HIGH);

    //     //printLocalTime();

    //     update_local_time();
    //     Serial.print("Time: ");
    //     Serial.printf("%s\n", time_buffer);

    //     // TESTING
    //     // waterLevel++;
    //     // irrigationState = !irrigationState;
    //     // send_events_to_web_client();

    //     delay(2000);

    //     neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off
    //     digitalWrite(IRRIGATION_GPIO_PIN, LOW);
    //     Serial.println("Irrigation pin set low");

    //     // digitalWrite(RGB_BUILTIN, HIGH);   // Turn the RGB LED white
    //     // neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, RGB_BRIGHTNESS, RGB_BRIGHTNESS); // Red
    //     // delay(1000);

    //     // neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off
    //     // delay(1000);

    //     // neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0); // Red

    //     // digitalWrite(IRRIGATION_GPIO_PIN, LOW);
    //     // Serial.println("Irrigation pin set low");

    //     // delay(1000);

    //     // neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0); // Green
    //     // delay(1000);

    //     // neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS); // Blue
    //     // delay(1000);

    //     // neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off / black
    //     // delay(1000);

    //     // TESTING
    //     // waterLevel++;
    //     // irrigationState = !irrigationState;
    //     //send_events_to_web_client();

    // #endif
    // See what mode we're in and act accordingly
    switch (state) {
        case ERROR:
            // Error message to web page
            //print_state();
            break;

        case INITIALISING:
            // Program initialising finished
            state = READY;
            print_state();
            Serial.printf("%s Starting program loop, watering scheduled for: %d:%d every day.\n", time_buffer, watering.hour, watering.minute);
            webTimer = millis();
            break;

        case READY:
            // Everything okay and ready to start irrigating the courgettes.
            // Update web page that we're ready and then every nn seconds/minutes with status.
            // update web page every minute
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                if (timeinfo.tm_hour == watering.hour && timeinfo.tm_min == watering.minute && !watering.wateringStarted) {

                    // --- YOUR EVENT HAPPENS HERE ---
                    update_local_time();
                    Serial.printf("%s Daily watering of courgettes started.\n", time_buffer);
                    // -------------------------------

                    watering.wateringStarted = true; // Mark as done for today
                    state = START_WATERING;
                    irrigationState = !irrigationState;
                }
            }
            break;

        case START_WATERING:
            print_state();
            digitalWrite(IRRIGATION_GPIO_PIN, HIGH);
            //neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0); // Red
            neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS); // Blue
            Serial.printf("%s Water pump on, watering has started.\n", time_buffer);
            state = WATERING;
            print_state();
            wateringTimer = millis();
            break;

        case WATERING:
            // Watering the courgettes.
            // Stop after nn seconds/minutes.
            if (millis() - wateringTimer >= stop_watering) {
                wateringTimer = millis();
                waterLevel--;
                irrigationState = !irrigationState;
                send_events_to_web_client();
                state = STOP_WATERING;
                print_state();
            }

            break;

        case STOP_WATERING:
            digitalWrite(IRRIGATION_GPIO_PIN, LOW);
            neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off
            irrigationState = !irrigationState;
            watering.wateringStarted = false;   // reset schedule
            Serial.printf("%s Water pump off, watering has stopped\n", time_buffer);
            state = READY;
            print_state();
            send_events_to_web_client();
            break;

        default:
            // Should never get here!
            Serial.println("ERROR - UNKNOWN STATE, SHOULD NOT BE HERE!");
            break;
    }

    // Update web page every 30 seconds
    if (millis() - webTimer >= update_webpage) {
        webTimer = millis();
        update_local_time();
        print_state();
        //Serial.print("Time: ");
        //Serial.printf("Time %s\n", time_buffer);

        waterLevel++;
        //irrigationState = !irrigationState;
        send_events_to_web_client();
    }
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
    // According to various forums configTime on the ESP32 does not honor the TZ env
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
        return irrigationState ? "WATERING" : "OFF";
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
    events.send(String(waterLevel), "waterlevel", millis());

    // // So the user knows the application is still running!
    // last_time = millis();

    // sprintf(runtime_buffer, "%02d:%02d:%02d", (last_time / 1000) / 3600, ((last_time / 1000) % 3600) / 60,
    //         ((last_time / 1000) % 3600) % 60);

    //update_local_time();
    //Serial.print("Time: ");
    //Serial.printf("%s\n", time_buffer);

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

/**
 * @brief For debugging only, print out what state we are set to.
 */
static void print_state(void) {
    update_local_time();
    Serial.printf("%s State:", time_buffer);

    switch (state) {
        case ERROR:
            Serial.println(" ERROR");
            break;

        case INITIALISING:
            Serial.println(" INITIALISING");
            break;

        case READY:
            Serial.println(" READY");
            break;

        case START_WATERING:
            Serial.println(" START WATERING");
            break;

        case WATERING:
            Serial.println(" WATERING");
            break;

        case STOP_WATERING:
            Serial.println( " STOP WATERING");
            break;

        default:
            Serial.println(" UNDEFINED");
            break;
    }
}
