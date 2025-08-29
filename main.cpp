// --- Library Includes ---
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <max6675.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <FirebaseJson.h>
#include <HTTPClient.h>
#include <math.h>
#include <time.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

// --- Pin Definitions ---
const int PH_SENSOR_PIN = 34;
const int TDS_SENSOR_PIN = 35;
const int TURBIDITY_SENSOR_PIN = 32;

const int BTN_UP_PIN    = 26;
const int BTN_DOWN_PIN  = 25;
const int BTN_ENTER_PIN = 33;
const int BTN_BACK_PIN  = 27;

const int THERMO_SCK_PIN = 18;
const int THERMO_CS_PIN  = 5;
const int THERMO_SO_PIN  = 19;

const int SD_SCLK_PIN = 14;
const int SD_MISO_PIN = 12;
const int SD_MOSI_PIN = 13;
const int MY_SD_CS_PIN = 15;

const int SENSOR_SAMPLE_COUNT = 100;
const unsigned long SENSOR_READ_INTERVAL = 1000;
const unsigned long LOG_INTERVAL = 1000;
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long STATUS_PRINT_INTERVAL = 5000;
const int WIFI_PORTAL_TIMEOUT = 180;

// --- WiFi & Relay Control ---
const char* RELAY_ESP8266_IP = "192.168.117.53";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;
const int   daylightOffset_sec = 0;

String currentClassification = "Initializing...";

// --- Firebase Setup ---
#define API_KEY "AIzaSyDrPFLaJE8XDDgWufp8EAJj-uX3L9F-Kso"
#define DATABASE_URL "https://esp-waterquality-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "saurabh98048@gmail.com"
#define USER_PASSWORD "ESPProject123!@#"

// --- Global Variables ---
float temperature = 0.0, phValue = 7.0, tdsValue = 0.0, turbidityValue = 0.0;
float phThresholdLow = 6.5, phThresholdHigh = 8.5, tdsThreshold = 150.0, turbidityThreshold = 5.0, tempThreshold = 30.0;

enum MenuState {
    MAIN_MENU, READINGS_MENU, LOGGING_MENU, RELAY_MENU,
    RELAY_CONTROL_SUBMENU, THRESHOLDS_MENU, WIFI_MENU
};
MenuState currentMenu = MAIN_MENU;
int menuCursor = 0, submenuCursor = 0;

enum LoggingMode { CONTINUOUS, THRESHOLD };
LoggingMode loggingMode = CONTINUOUS;
bool sdCardPresent = false, relayState = false;
bool contaminationAlertActive = false;

unsigned long lastSensorReadTime = 0, lastLogTime = 0, lastStatusPrintTime = 0, lastSendTime = 0;
const unsigned long sendInterval = 5000;

// --- Object Initializations ---
LiquidCrystal_I2C lcd(0x27, 20, 4);
MAX6675 thermocouple(THERMO_SCK_PIN, THERMO_CS_PIN, THERMO_SO_PIN);
File logFile;
SPIClass hspi(HSPI);

// --- NEW Firebase Global Objects ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- Function Prototypes ---
void readAllSensors();
bool sendRelayCommand(bool turnOn);
void handleButtons();
void updateDisplay();
void displayMainMenu();
void printSystemStatus();
void logData();
void displayReadingsMenu();
void displayWifiMenu();
void displayLoggingMenu();
void displayRelayMenu();
void displayThresholdsMenu();
void displayRelayControlSubmenu();
void checkThresholdsAndControlRelay();


// --- Setup Function ---
void setup() {
    Serial.begin(115200);
    Serial.println("\n\nInitializing Water Quality Monitoring System (Dual SPI)...");

    pinMode(BTN_UP_PIN, INPUT);
    pinMode(BTN_DOWN_PIN, INPUT);
    pinMode(BTN_ENTER_PIN, INPUT);
    pinMode(BTN_BACK_PIN, INPUT);
    
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0); lcd.print("Water Monitor v1.0");
    lcd.setCursor(0, 1); lcd.print("Initializing...");
    delay(2000);

    hspi.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, MY_SD_CS_PIN);
    
    if (!SD.begin(MY_SD_CS_PIN, hspi)) {
        Serial.println("SD Card initialization failed on HSPI bus!");
        sdCardPresent = false;
    } else {
        Serial.println("SD Card initialized on HSPI bus.");
        sdCardPresent = true;
        logFile = SD.open("/log.csv", FILE_READ);
        if (!logFile) {
            logFile = SD.open("/log.csv", FILE_WRITE);
            if (logFile) {
                logFile.println("Time,Temperature,PH,TDS,Turbidity,Contamination_Type");
                logFile.close();
            }
        } else {
            logFile.close();
        }
    }

    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    wm.setConfigPortalTimeout(1);
    wm.autoConnect("WaterMonitorSetup");

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // --- NEW Firebase Initialization ---
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    readAllSensors();
    updateDisplay();
    Serial.println("Initialization complete. System is running.");
}

// --- Main Loop ---
void loop() {
    unsigned long currentTime = millis();
    
    handleButtons();

    if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        lastSensorReadTime = currentTime;
        readAllSensors();
        if (currentMenu == READINGS_MENU) updateDisplay();
        checkThresholdsAndControlRelay();
    }

    if (sdCardPresent && loggingMode == CONTINUOUS && (currentTime - lastLogTime >= LOG_INTERVAL)) {
        lastLogTime = currentTime;
        logData();
    }
    
    if (currentTime - lastStatusPrintTime >= STATUS_PRINT_INTERVAL) {
        lastStatusPrintTime = currentTime;
        printSystemStatus();
    }

    // --- NEW Data Sending Logic ---
    if (Firebase.ready() && (currentTime - lastSendTime >= sendInterval)) {
        lastSendTime = currentTime;

        FirebaseJson json;
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char timeString[25];
            strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
            json.set("timestamp", timeString);
        } else {
            Serial.println("Failed to obtain time");
            json.set("timestamp", "NA");
        }

        json.set("ph", phValue);
        json.set("tds_ppm", tdsValue);
        json.set("temperature_c", temperature);
        json.set("turbidity_ntu", turbidityValue);
        json.set("contamination_type", currentClassification);

        String path = "/sensor_data";
        if (Firebase.RTDB.pushJSON(&fbdo, path.c_str(), &json)) {
            Serial.println("Firebase push successful: " + fbdo.dataPath() + "/" + fbdo.pushName());
        } else {
            Serial.println("Firebase push failed");
            Serial.println("REASON: " + fbdo.errorReason());
        }
    }
}

// --- Sensor Reading and Calibration ---
void readAllSensors() {
    const float VREF = 3.3;
    const int ADC_RESOLUTION = 4095;
    const float PH_NEUTRAL_OFFSET = 2048.0;

    float rawPhValue = analogRead(PH_SENSOR_PIN);
    phValue = 7.0 - ((PH_NEUTRAL_OFFSET - rawPhValue) * VREF * 1000.0) / (59.16 * ADC_RESOLUTION);

    float rawTdsValue = analogRead(TDS_SENSOR_PIN);
    float tdsVoltage = rawTdsValue * VREF / (float)ADC_RESOLUTION;
    float tdsTemp = 133.42 * pow(tdsVoltage, 3) - 255.86 * pow(tdsVoltage, 2) + 857.39 * tdsVoltage;
    tdsValue = tdsTemp * 0.5;
    if (tdsValue < 0) tdsValue = 0;

    float rawTurbidityValue = analogRead(TURBIDITY_SENSOR_PIN);
    turbidityValue = map(rawTurbidityValue, 0, 2800, 100, 0);
    if(turbidityValue < 0){
        turbidityValue = 0;
    }

    temperature = thermocouple.readCelsius();
}

// --- Button Debounce Helper ---
bool wasButtonPressed(int pin, int &lastState) {
    static unsigned long lastPressTime = 0;
    int currentState = digitalRead(pin);
    bool pressed = false;

    if (currentState == HIGH && lastState == LOW && (millis() - lastPressTime) > DEBOUNCE_DELAY) {
        lastPressTime = millis();
        pressed = true;
    }
    lastState = currentState;
    return pressed;
}

// --- User Interface and Button Handling ---
void handleButtons() {
    static int lastUpState = LOW, lastDownState = LOW, lastEnterState = LOW, lastBackState = LOW;
    bool buttonPressed = false;

    if (wasButtonPressed(BTN_UP_PIN, lastUpState)) {
        buttonPressed = true;
        switch (currentMenu) {
            case MAIN_MENU: menuCursor = (menuCursor > 0) ? menuCursor - 1 : 4; break;
            case LOGGING_MENU: menuCursor = (menuCursor > 0) ? menuCursor - 1 : 1; break;
            case RELAY_CONTROL_SUBMENU: submenuCursor = 0; break;
            case THRESHOLDS_MENU:
                switch(menuCursor) {
                    case 0: phThresholdLow += 0.1; break; case 1: phThresholdHigh += 0.1; break;
                    case 2: tdsThreshold += 10; break; case 3: turbidityThreshold += 1; break;
                    case 4: tempThreshold += 0.5; break;
                }
                break;
        }
    }

    if (wasButtonPressed(BTN_DOWN_PIN, lastDownState)) {
        buttonPressed = true;
        switch (currentMenu) {
            case MAIN_MENU: menuCursor = (menuCursor < 4) ? menuCursor + 1 : 0; break;
            case LOGGING_MENU: menuCursor = (menuCursor < 1) ? menuCursor + 1 : 0; break;
            case RELAY_CONTROL_SUBMENU: submenuCursor = 1; break;
            case THRESHOLDS_MENU:
                switch(menuCursor) {
                    case 0: phThresholdLow -= 0.1; break; case 1: phThresholdHigh -= 0.1; break;
                    case 2: tdsThreshold -= 10; break; case 3: turbidityThreshold -= 1; break;
                    case 4: tempThreshold -= 0.5; break;
                }
                break;
        }
    }

    if (wasButtonPressed(BTN_ENTER_PIN, lastEnterState)) {
        buttonPressed = true;
        switch (currentMenu) {
            case MAIN_MENU:
                switch (menuCursor) {
                    case 0: currentMenu = READINGS_MENU; break;
                    case 1: currentMenu = LOGGING_MENU; menuCursor = (loggingMode == CONTINUOUS) ? 0 : 1; break;
                    case 2: currentMenu = RELAY_MENU; break;
                    case 3: currentMenu = THRESHOLDS_MENU; menuCursor = 0; break;
                    case 4: currentMenu = WIFI_MENU; break;
                }
                break;
            case LOGGING_MENU: loggingMode = (menuCursor == 0) ? CONTINUOUS : THRESHOLD; currentMenu = MAIN_MENU; break;
            case RELAY_MENU: currentMenu = RELAY_CONTROL_SUBMENU; submenuCursor = relayState ? 1 : 0; break;
            case RELAY_CONTROL_SUBMENU: sendRelayCommand(submenuCursor == 0); currentMenu = RELAY_MENU; break;
            case THRESHOLDS_MENU: menuCursor = (menuCursor < 4) ? menuCursor + 1 : 0; break;
            case WIFI_MENU:
                {
                    lcd.clear();
                    lcd.setCursor(0, 0); lcd.print("Starting Portal...");
                    lcd.setCursor(0, 1); lcd.print("AP: WaterMonitorSetup");
                    
                    WiFiManager wm;
                    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
                    
                    if (wm.startConfigPortal("WaterMonitorSetup")) {
                        lcd.clear();
                        lcd.print("WiFi Connected!");
                    } else {
                        lcd.clear();
                        lcd.print("Portal Timed Out");
                    }
                    delay(2000);
                    currentMenu = MAIN_MENU;
                }
                break;
        }
    }

    if (wasButtonPressed(BTN_BACK_PIN, lastBackState)) {
        buttonPressed = true;
        switch (currentMenu) {
            case RELAY_CONTROL_SUBMENU: currentMenu = RELAY_MENU; break;
            default: currentMenu = MAIN_MENU; menuCursor = 0; break;
        }
    }

    if (buttonPressed) updateDisplay();
}

void updateDisplay() {
    lcd.clear();
    switch (currentMenu) {
        case MAIN_MENU: displayMainMenu(); break;
        case READINGS_MENU: displayReadingsMenu(); break;
        case LOGGING_MENU: displayLoggingMenu(); break;
        case RELAY_MENU: displayRelayMenu(); break;
        case RELAY_CONTROL_SUBMENU: displayRelayControlSubmenu(); break;
        case THRESHOLDS_MENU: displayThresholdsMenu(); break;
        case WIFI_MENU: displayWifiMenu(); break;
    }
}

void displayMainMenu() {
    const char* menuItems[] = {"Sensor Readings", "Data Logging", "Remote Relay", "Edit Thresholds", "WiFi Settings"};
    int startItem = (menuCursor > 3) ? menuCursor - 3 : 0;
    for (int i = 0; i < 4; i++) {
        int idx = startItem + i;
        if (idx < 5) {
            lcd.setCursor(0, i);
            lcd.print(menuCursor == idx ? "> " : "  ");
            lcd.print(menuItems[idx]);
        }
    }
}

void displayReadingsMenu() {
    lcd.setCursor(0, 0); lcd.print("Temperature: "); lcd.print(temperature, 1); lcd.print(" C");
    lcd.setCursor(0, 1); lcd.print("pH: "); lcd.print(phValue, 2);
    lcd.setCursor(0, 2); lcd.print("TDS: "); lcd.print(tdsValue, 0); lcd.print(" PPM");
    lcd.setCursor(0, 3); lcd.print("Turbidity: "); lcd.print(turbidityValue, 1); lcd.print(" %");
}

void displayLoggingMenu() {
    lcd.setCursor(0, 0); lcd.print("SD Card: "); lcd.print(sdCardPresent ? "Detected" : "Not Found");
    lcd.setCursor(0, 2); lcd.print(menuCursor == 0 ? "> Continuous" : "  Continuous");
    lcd.setCursor(0, 3); lcd.print(menuCursor == 1 ? "> On Threshold" : "  On Threshold");
}

void displayRelayMenu() {
    lcd.setCursor(0, 0); lcd.print("Remote Relay");
    lcd.setCursor(0, 2); lcd.print("Status: "); lcd.print(relayState ? "ON" : "OFF");
    lcd.setCursor(0, 3); lcd.print("ENTER to change");
}

void displayRelayControlSubmenu() {
    lcd.setCursor(0, 0); lcd.print("Set Relay State");
    lcd.setCursor(0, 2); lcd.print(submenuCursor == 0 ? "> Turn ON" : "  Turn ON");
    lcd.setCursor(0, 3); lcd.print(submenuCursor == 1 ? "> Turn OFF" : "  Turn OFF");
}

void displayThresholdsMenu() {
    lcd.setCursor(0, 0); lcd.print(menuCursor == 0 ? ">" : " "); lcd.print("PH Low:  "); lcd.print(phThresholdLow, 1);
    lcd.setCursor(0, 1); lcd.print(menuCursor == 1 ? ">" : " "); lcd.print("PH High: "); lcd.print(phThresholdHigh, 1);
    lcd.setCursor(0, 2); lcd.print(menuCursor == 2 ? ">" : " "); lcd.print("TDS:     "); lcd.print(tdsThreshold, 0);
    lcd.setCursor(0, 3); lcd.print(menuCursor == 3 ? ">" : " "); lcd.print("Turbid:  "); lcd.print(turbidityThreshold, 0);
    if (menuCursor == 4) {
      lcd.clear();
      lcd.setCursor(0,0); lcd.print(menuCursor == 4 ? ">" : " "); lcd.print("Temp:    "); lcd.print(tempThreshold, 1);
    }
}

void displayWifiMenu() {
    lcd.setCursor(0, 0); lcd.print("WiFi Settings");
    lcd.setCursor(0, 2);
    if (WiFi.status() == WL_CONNECTED) {
        lcd.print("Connected: ");
        lcd.setCursor(0, 3); lcd.print(WiFi.SSID());
    } else {
        lcd.print("Not Connected.");
        lcd.setCursor(0, 3); lcd.print("ENTER to connect.");
    }
}

void logData() {
    if (!sdCardPresent) return;
    logFile = SD.open("/log.csv", FILE_APPEND);
    if (logFile) {
        logFile.print(millis()); logFile.print(","); logFile.print(temperature); logFile.print(",");
        logFile.print(phValue); logFile.print(","); logFile.print(tdsValue); logFile.print(",");
        logFile.println(turbidityValue);
        logFile.print(",");
        logFile.println(currentClassification); // ADDED the ML result to the log
        logFile.close();
    }
}

void checkThresholdsAndControlRelay() {
    bool isContaminated = (phValue < phThresholdLow || phValue > phThresholdHigh ||
                              tdsValue > tdsThreshold || turbidityValue > turbidityThreshold ||
                              temperature > tempThreshold);

    if (isContaminated && !contaminationAlertActive) {
        Serial.println("ALERT: Contamination detected! Turning relay ON.");
        sendRelayCommand(true);
        contaminationAlertActive = true;
        if (loggingMode == THRESHOLD && sdCardPresent) {
            logData();
        }
    } else if (!isContaminated && contaminationAlertActive) {
        Serial.println("INFO: Water is clean. Turning relay OFF.");
        sendRelayCommand(false);
        contaminationAlertActive = false;
    }
}

void printSystemStatus() {
    Serial.println("\n--- SYSTEM STATUS ---");
    Serial.printf("  Temperature: %.2f C\n", temperature);
    Serial.printf("  pH: %.2f\n", phValue);
    Serial.printf("  TDS: %.2f ppm\n", tdsValue);
    Serial.printf("  Turbidity: %.2f %%\n", turbidityValue);
    Serial.printf("  ML Classification: %s\n", currentClassification.c_str());
    Serial.print("  Logging Mode: "); Serial.println(loggingMode == CONTINUOUS ? "Continuous" : "On Threshold");
    Serial.print("  Relay Status: "); Serial.println(relayState ? "ON" : "OFF");
    Serial.print("  SD Card: "); Serial.println(sdCardPresent ? "Detected" : "Not Found");
    Serial.print("  WiFi: ");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Connected to "); Serial.println(WiFi.SSID());
    } else {
        Serial.println("Not Connected");
    }
    Serial.println("  Thresholds:");
    Serial.printf("    pH: %.1f - %.1f\n", phThresholdLow, phThresholdHigh);
    Serial.printf("    TDS: %.0f ppm\n", tdsThreshold);
    Serial.printf("    Turbidity: %.0f %%\n", turbidityThreshold);
    Serial.printf("    Temperature: %.1f C\n", tempThreshold);
    Serial.println("---------------------\n");
}

bool sendRelayCommand(bool turnOn) {
    if (WiFi.status() != WL_CONNECTED) return false; 
    HTTPClient http;
    String serverPath = "http://" + String(RELAY_ESP8266_IP) + (turnOn ? "/on" : "/off");
    
    http.begin(serverPath.c_str());
    http.setConnectTimeout(1000);
    
    int httpResponseCode = http.GET();
    http.end();
    if (httpResponseCode > 0) {
        relayState = turnOn;
        return true;
    }
    return false;
}

void runInference() {
    float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

    features[0] = temperature;
    features[1] = phValue;
    features[2] = tdsValue;
    features[3] = turbidityValue;

    signal_t signal;
    int err = numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
    if (err != 0) {
        Serial.printf("Failed to create signal from buffer. Error: %d\n", err);
        return;
    }

    ei_impulse_result_t result = { 0 };
    err = run_classifier(&signal, &result, false);
    if (err != EI_IMPULSE_OK) {
        Serial.printf("ERR: Failed to run classifier (%d)\n", err);
        return;
    }

    float max_confidence = 0;
    String best_label = "Unknown";
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > max_confidence) {
            max_confidence = result.classification[ix].value;
            best_label = result.classification[ix].label;
        }
    }
    
    // UPDATE the global variable with the latest classification
    currentClassification = best_label;
}