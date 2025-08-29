# Intelligent Water Quality Monitoring System with TinyML and Cloud Integration

![Project Banner](<!-- placeholder: a photo of your assembled project -->)

An advanced, low-cost IoT system for real-time monitoring and intelligent classification of water quality. This project leverages an ESP32 microcontroller, a suite of environmental sensors, and an on-device machine learning model (TinyML) to provide autonomous anomaly detection. Data is streamed to a Google Firebase backend and visualized on a dynamic, real-time web dashboard.

---

## Key Features

- **Multi-Parameter Sensing:** Measures four critical water quality parameters: Temperature, pH, Total Dissolved Solids (TDS), and Turbidity.
- **On-Device Intelligence (TinyML):** Utilizes a trained neural network running directly on the ESP32 to classify water contamination types (e.g., Normal, Runoff, Chemical) in real-time, without cloud dependency.
- **Cloud Integration & Real-Time Dashboard:** Streams sensor data and ML classifications to a Google Firebase Realtime Database. A custom web application provides a live dashboard with data cards and interactive graphs.
- **Local Data Logging:** Features a dual-mode SD card logging system (continuous or on-change) to ensure data integrity even without an internet connection.
- **Closed-Loop Control:** Includes a remote ESP8266-based relay module to automatically control a water pump or filtration system based on the ML model's output.
- **Local User Interface:** An onboard 20x4 LCD and push-button interface allow for local data viewing, system configuration, and manual control.
- **Robust Hardware Design:** Employs a dual-SPI bus architecture on the ESP32 to prevent hardware conflicts between the SD card and other peripherals.

---

## System Architecture

The system is built around an ESP32 microcontroller which acts as the central hub. It collects data from the sensor array, performs on-device inference using the TinyML model, displays status locally, logs data to an SD card, and communicates with the cloud and the remote relay module.

![System Block Diagram](<img width="479" height="313" alt="system-diagram" src="https://github.com/user-attachments/assets/bd90ca9b-f814-4e7c-87b9-2affc42578a2" />
)

---

## Hardware Components

| Component | Model/Type | Purpose |
| :--- | :--- | :--- |
| **Main Controller** | ESP32-WROOM-32 Dev Module | Core processing, WiFi, and sensor interface |
| **Temperature Sensor** | K-Type Thermocouple + MAX6675 Module | Measures water temperature |
| **pH Sensor** | E-201-C Glass Electrode + Signal Board | Measures acidity/alkalinity |
| **TDS Sensor** | Generic Analog TDS Meter | Measures total dissolved solids |
| **Turbidity Sensor** | Generic Analog Turbidity Sensor | Measures water clarity |
| **Display** | 20x4 I2C LCD | Local user interface |
| **Storage** | MicroSD Card Module | Local data logging |
| **Relay Module** | ESP-01 with 5V Relay | Remote pump/valve control |
| **User Input** | 4x Tactile Push Buttons | Menu navigation |
| **Power Supply** | 5V 2A Power Adapter / Breadboard PSU | System power |

---

## Circuit Diagram

The components are connected to the ESP32 as detailed in the Fritzing diagram below. A key design choice is the use of both the VSPI and HSPI buses to isolate the SD card from the MAX6675 sensor, preventing data corruption.

![Fritzing Circuit Diagram](<!-- placeholder: link to your Fritzing diagram image -->)

---

## Software, Libraries, and Setup

This project is developed using **PlatformIO in Visual Studio Code**.

### Main ESP32 Module (`platformio.ini`)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps = 
    marcoschw/LiquidCrystal_I2C@^1.1.4
    adafruit/MAX6675 library@^1.1.2
    mobizt/FirebaseClient@^2.1.8
    tzapu/WiFiManager@^2.0.17-beta
lib_ldf_mode = deep+
```

### Remote Relay Module (ESP-01)

- **Framework:** Arduino IDE or PlatformIO (for `esp01_1m` board).
- **Libraries:**
    - `ESP8266WiFi`
    - `ESP8266WebServer`
    - `WiFiManager` by tzapu

---

## Installation & Configuration Guide

### 1. Firebase Setup

1.  **Create Project:** Go to the [Firebase Console](https://console.firebase.google.com/) and create a new project.
2.  **Create Realtime Database:** In the "Build" menu, select "Realtime Database" and create a new database. Start in **test mode** for initial setup.
3.  **Set Up Authentication:** Go to "Authentication," select the "Sign-in method" tab, and enable **Email/Password**. Go to the "Users" tab and add the user with the email and password you've defined in the ESP32 code.
4.  **Get Credentials:** In "Project Settings" (gear icon), find your **Web API Key** and **Database URL**. You will need these for your ESP32 code.

### 2. ESP-01 Relay Module Setup

1.  Open the ESP-01 relay module sketch in the Arduino IDE.
2.  Upload the code to your ESP-01 module using a suitable programmer.
3.  On first boot, the module will create a WiFi network named **"RelaySetup"**. Connect to this network.
4.  A captive portal should open. Configure it to connect to your home WiFi network.
5.  Open the Arduino IDE's Serial Monitor to find the IP address assigned to the relay (e.g., `192.168.1.108`).
6.  Update the `RELAY_ESP8266_IP` variable in the main ESP32 sketch with this IP address.

### 3. Main ESP32 Module Setup

1.  Clone this repository or download the source code.
2.  Open the project in VS Code with the PlatformIO extension.
3.  In the `src/main.cpp` file, update the Firebase credentials (`Web_API_KEY`, `DATABASE_URL`, `USER_EMAIL`, `USER_PASS`).
4.  PlatformIO will automatically install the required libraries as defined in `platformio.ini`.
5.  Connect your ESP32 board and click the "Upload" button in PlatformIO.

### 4. Web Dashboard Deployment

1.  **Install Firebase CLI:** If you haven't already, install the Firebase command-line tools: `npm install -g firebase-tools`.
2.  **Login:** Run `firebase login` and authenticate with your Google account.
3.  **Initialize:** Navigate to the `web-dashboard` folder in your terminal and run `firebase init hosting`. Select your Firebase project when prompted. Use `public` as the public directory and answer "No" to configuring as a single-page app.
4.  **Configure:** Open the `public/index.html` file and paste your Firebase `firebaseConfig` object into the designated placeholder.
5.  **Deploy:** Run `firebase deploy`. The terminal will provide a URL where your live dashboard is hosted.

---

## Machine Learning (TinyML) Workflow

The core intelligence of this system comes from an on-device neural network.

### 1. Data Generation

A custom dataset is crucial for model accuracy. A Python script (`synthetic_data_generator.py`) is included to generate large, labeled datasets for three classes: `normal`, `runoff`, and `chemical`. This script produces CSV files with realistic noise, formatted for direct use in Edge Impulse.

### 2. Model Training with Edge Impulse

1.  **Create Project:** Sign up for a free account at [Edge Impulse](https://www.edgeimpulse.com/) and create a new project.
2.  **Upload Data:** Upload the generated CSV files, assigning the correct label to each file.
3.  **Create Impulse:** Design an impulse with a 4-feature input, a "Raw Data" processing block, and a "Classification (Keras)" learning block.
4.  **Train:** Train the neural network. The platform provides detailed performance metrics, including accuracy and a confusion matrix.

![Edge Impulse Training Results](<!-- placeholder: link to your screenshot of the EI training results page -->)

### 3. Deployment to ESP32

1.  In Edge Impulse, go to the **Deployment** tab.
2.  Select the **C++ Library** option and build the library.
3.  Unzip the downloaded file and copy the contents (`edge-impulse-sdk`, `model-parameters`, `tflite-model`) into the `lib` folder of your PlatformIO project.
4.  The ESP32 code is already configured to include and run the classifier from this library.

---

## Usage

- **Local Interface:** Use the four push buttons to navigate the LCD menu. You can view live sensor readings, change the logging mode, and manually control the relay.
- **Remote Dashboard:** Access the Firebase Hosting URL from any browser to see the live data cards and graphs update in real-time.

![Web Dashboard Screenshot](<!-- placeholder: link to a screenshot of your live dashboard -->)
