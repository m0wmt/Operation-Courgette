/**
 * Program to control a water pump to irrigate our courgette plants.
 *
 * Using an ESP32-S3-ZERO : https://www.waveshare.com/wiki/ESP32-S3-Zero
 *
 * https://github.com/arkhipenko/IoT_apis2/blob/master/IoT_apis2.ino
 * https://github.com/arkhipenko/apis/blob/master/README
 *
 * https://github.com/qqueke/freeRTOS-Alarm/blob/main/main.cpp - looking at this one at the mo 
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

// RTOS headers
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static bool enableWiFi(void);
static void disableWiFi(void);
static void ntpTime(void);
static String processor(const String &var);
static void send_events_to_web_client(void);
static void update_local_time(void);
static void print_state(void);

static void webTimerCallback(TimerHandle_t xTimer);
void webTask(void *pvParameters);
void clockTask(void *pvParameters);
void waterTask(void *pvParameters);
static void startWatering(void);
static void stopWatering(void);

// TEST/DEBUG defines
// ------------------
#define _DEBUG_
// #define _TEST_

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
bool isWatering = false;
int waterLevel = 3;
char time_buffer[10];
uint32_t watering_timer = 0;    // How long we've been watering for
uint16_t update_timer = 0;      // How long we've been going - triggers when == update_webpage - ## upgrade to uint32_t if larger than 1 minute ##

// Schedule time to start watering - 9:15 and 15:50 every day
// typedef struct WateringScheduleStruct {
//     bool isWatering = false; // stop multiple firings
//     uint8_t const WMH = 9;      // morning hour to water
//     uint8_t const WMM = 15;     // morning minute to water
//     uint8_t const WAH = 16;     // afternoon hour to water
//     uint8_t const WAM = 15;     // afternoon minute to water
//     uint8_t const duration = 5; // number of minutes to water for  
// } WateringScheduleStruct;
// WateringScheduleStruct watering;


typedef enum { // Keep track of current state of the device
    ERROR,
    INITIALISING,
    READY,
    START_WATERING,
    WATERING,
    STOP_WATERING
} irrigation_state_t;
irrigation_state_t state = INITIALISING;

typedef enum { // Keep track of current state of the device
    IR_ERROR,
    IR_INITIALISING,
    IR_READY,
    IR_START_WATERING,
    IR_WATERING,
    IR_STOP_WATERING
} ir_state_t;

typedef struct WateringScheduleStruct {
    uint8_t hour;
    uint8_t minute;
} WateringScheduleStruct;
WateringScheduleStruct watering[2] = {{9, 15}, {16, 14}}; // Alarm times for watering

static const uint16_t update_webpage = 10000;   // Update web page every 10 seconds (10000). ## Any more than 1 min then upgrade to uint32_t ##
static const uint32_t stop_watering = 120000;   // Watering time limit (in milliseconds) - 5 mins (300000)
static const uint32_t watering_duration = 300000; // Watering time limit (in milliseconds) - 5 mins (300000)
// 60000 = 1 min
// 120000 =  2 mins
// 240000 = 4 mins
// 600000 = 10 mins

// RTOS tasks
TaskHandle_t webTaskHandle;
TaskHandle_t waterTaskHandle;

TimerHandle_t webTimerHandle;

volatile uint8_t CLKH = 0; // Clock hours
volatile uint8_t CLKM = 0; // Clock minutes
volatile uint8_t CLKS = 0; // Clock seconds

void setup() {
    // Define pin we're using to control water pump and turn it off
    pinMode(IRRIGATION_GPIO_PIN, OUTPUT);
    digitalWrite(IRRIGATION_GPIO_PIN, LOW);
    neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off

#if defined(_DEBUG_)
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
#endif

    state = INITIALISING;
    print_state();

    // Enable wifi so we can retrieve the real time and update our local time.
    if (enableWiFi() == true) {
        // ipAddress = WiFi.localIP().toString();
        // Serial.printf("IP Address: %s\n", ipAddress);
        #if defined(_DEBUG_) 
            Serial.printf("IP Address: ");
            Serial.println(WiFi.localIP());
        #endif

        //ntpTime();
        configTime(0, 3600, SNTP_TIME_SERVER);
        //printLocalTime();
        update_local_time();

        #if defined(_DEBUG_)
            Serial.println("Time setup complete");
        #endif

        delay(500);

        // Disable wifi as we're going to set up our own local access point
        disableWiFi();

    } else {
        state = ERROR;
        digitalWrite(IRRIGATION_GPIO_PIN, HIGH);
        neopixelWrite(RGB_BUILTIN, RGB_BRIGHTNESS, 0, 0); // Red
        #if defined(_DEBUG_)
            Serial.println("Unable to connect to WiFi!");
            print_state();
        #endif
    }

    //printLocalTime();

    // Set up our own local Wi-Fi network with SSID and password
    #if defined(_DEBUG_)
        Serial.println("Setting Up AP (Access Point)...");
    #endif

    WiFi.mode(WIFI_MODE_AP);

    // Taken from config.h
    WiFi.softAP(LOCAL_SSID, LOCAL_WIFI_PASSWORD);

    #if defined(_DEBUG_)
        Serial.print("Access Point Created: ");
        Serial.println(LOCAL_SSID);
    #endif

    // If connection successful show IP address in serial monitor
    #if defined(_DEBUG_)
        IPAddress IP = WiFi.softAPIP();
        Serial.print("LOCAL AP IP address: ");
        Serial.println(IP);
    #endif

    // Handle Web Server
    server.on("/", HTTP_GET,
              [](AsyncWebServerRequest *request) { request->send(200, "text/html", MAIN_page, processor); });

    // Handle Web Server Events
    events.onConnect([](AsyncEventSourceClient *client) {
        if (client->lastId()) {
            #if defined(_DEBUG_)
                Serial.printf("%s Client reconnected, message ID: %u\n", time_buffer, client->lastId());
            #endif
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
        #if defined(_DEBUG_)
            print_state();
        #endif
        //digitalWrite(ledPin, ledState);
        request->send(200, "text/plain", irrigationState ? "ON" : "OFF");
    });

    // Handle LED state request
    server.on("/state", HTTP_GET,
              [](AsyncWebServerRequest *request) { request->send(200, "text/plain", irrigationState ? "WATERING" : "OFF"); });

    server.addHandler(&events);
    server.begin(); // Start server

    digitalWrite(IRRIGATION_GPIO_PIN, LOW);
    #if defined(_DEBUG_)
        Serial.println("Irrigation pin set low");
    #endif

    #if defined(_DEBUG_)
        Serial.println("Program started");
        Serial.println("");
    #endif

    // Create Tasks
    // xTaskCreate(clockTask, "Clock_Task", 2*configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreatePinnedToCore(clockTask, "Clock_Task", 2 * configMINIMAL_STACK_SIZE, NULL, 3, NULL, 1);

        // xTaskCreate(webTask, "Web_Task", 2048, NULL, 2, &webTaskHandle);
        // xTaskCreate(waterTask, "Water Task", 2048, NULL, 2, &waterTaskHandle);

        // // Create Timer (10 second period, auto-reload)
        // webTimerHandle = xTimerCreate("Web_Timer", pdMS_TO_TICKS(10000), pdTRUE, NULL, webTimerCallback);
        // if (webTimerHandle != NULL) {
        //     xTimerStart(webTimerHandle, 0);
        //     #if defined(_DEBUG_)
        //         Serial.println("Web timer started successfully.");
        //     #endif
        // } else {
        //     #if defined(_DEBUG_)
        //         Serial.println("Web timer creation failed!");
        //     #endif
        // }

    send_events_to_web_client();

    //update_local_time();

    // Set up run time buffer to 5 seconds, waiting time above!
    // sprintf(time_buffer, "%02d:%02d:%02d", 0, 0, 5);

}

void loop() {
    // All done in tasks
}

/**
 * @brief Trigger web task with this timer
 */
void webTimerCallback(TimerHandle_t xTimer) {
    // Notify the LED task
    xTaskNotifyGive(webTaskHandle);
}

/**
 * @brief Web task to update the web page
 */
void webTask(void *pvParameters) {
    while (1) {
        // Wait until web timer notifies
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Update web page
        //update_local_time();
        #if defined(_DEBUG_)
            print_state();
        #endif
        //send_events_to_web_client();
    }
}

/**
 * @brief Water task 
 */
void waterTask(void *pvParameters) {
    #if defined(_DEBUG_)
        Serial.println("waterTask");
    #endif

    // while (1) {
    //     // Wait until water timer notifies
    //     ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    //     // Update web page
    //     update_local_time();
    //     #if defined(_DEBUG_)
    //         Serial.println("waterTask");
    //     #endif
    //     // send_events_to_web_client();
    // }
}

/**
 * @brief Clock task, runs every second
 */
void clockTask(void *pvParameters) { 
    TickType_t xLastWakeTime;
    TickType_t xStartedWatering;

    // Get the current time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        #if defined(_DEBUG_)
            Serial.println("Failed to obtain time");
        #endif
    } else {
        CLKH = timeinfo.tm_hour;
        CLKM = timeinfo.tm_min;
        CLKS = timeinfo.tm_sec;

        Serial.printf("%d:%d:%d Clock Task Initiated\n", CLKH, CLKM, CLKS);
    }

    for ( ;; ) {
        xLastWakeTime = xTaskGetTickCount();

        // Update hours, mins and seconds
        CLKS = (CLKS + 1) % 60;

        if (CLKS == 0) {
            CLKM = (CLKM + 1) % 60;

            #if defined(_DEBUG_)
                Serial.printf("%d:%d:00 Clock Task\n", CLKH, CLKM);
            #endif

            if (CLKM == 0) {
                CLKH = (CLKH + 1) % 24;
            }
        }

        if (!isWatering) {  // We're not currently watering, check if we need to start or not
            for(int i = 0; i < 2; i++) {
                if ((CLKH == watering[i].hour) && (CLKM == watering[i].minute) && (!isWatering)) {
                    xStartedWatering = xTaskGetTickCount();
                    startWatering();
                    break;
                }
            }
        }

        if (isWatering) { // We're currently watering, check if we need to stop
            if (xStartedWatering + watering_duration <= xTaskGetTickCount()) {
                stopWatering();
            }
        }
           
        // Delay for a second
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

/**
 * @brief Start watering!
 */
static void startWatering(void){
    isWatering = true;

    digitalWrite(IRRIGATION_GPIO_PIN, HIGH);
    neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS); // Blue

    #if defined(_DEBUG_)
        Serial.printf("%d:%d:%d Started Watering\n", CLKH, CLKM, CLKS);
    #endif

    send_events_to_web_client();
}

/**
 * @brief Stop watering!
 */
static void stopWatering(void) {
    isWatering = false;

    digitalWrite(IRRIGATION_GPIO_PIN, LOW);
    neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off

    #if defined(_DEBUG_)
        Serial.printf("%d:%d:%d Stopped Watering\n", CLKH, CLKM, CLKS);
    #endif

    send_events_to_web_client();
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

#if defined(_DEBUG_)
        Serial.println("WiFi mode set to WIFI_OFF");
    #endif
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

    #if defined(_DEBUG_)
        Serial.println("Time set...");
    #endif
}

/**
 * @brief Handle requests from the web page.
 */
static String processor(const String &var) {
    #if defined(_DEBUG_)
        Serial.print("processor: ");
        Serial.println(var);
    #endif

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

    update_local_time();
    //Serial.print("Time: ");
    //Serial.printf("%s\n", time_buffer);

    events.send(String(time_buffer), "runtime", millis());
}


static void update_local_time(void) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        #if defined(_DEBUG_)
            Serial.println("Failed to obtain time");
        #endif
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
    
    #if defined(_DEBUG_)
        //Serial.printf("%d:%d:%d State:", CLKH, CLKM, CLKS);
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
    #endif
}
