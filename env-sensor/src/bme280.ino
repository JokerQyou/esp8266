/***************************************************************************
 ***************************************************************************/

/**
  Required PlatformIO libraries: (ID)
  Sensors: 31, 166;
  SSD1306 OLED screen: 13, 135;
  MQTT client: 89;
 **/
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include "../lib/Adafruit SSD1306/Adafruit_SSD1306.h"
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// User configuration
#include "user_config.h"
// A modified version of ESP8366 HTTPClient
#include "ESP8266HTTPClient.h"
#include "Monaco9pt7b.h"

#ifndef VERSION
    #define VERSION "0.2.3"
#endif
#ifndef DEVICE_ID
    #error "Please define DEVICE_ID in user_config.h"
#endif
// Data publish interval, in MS.
#ifndef PUBLISH_INTERVAL
    #define PUBLISH_INTERVAL 600000  // 600 seconds
#endif

// SSD1306 screen in hardware SPI mode
// Notice: these are GPIO pin numbers
#define OLED_DC     2
#define OLED_CS     15
#define OLED_RESET  0
Adafruit_SSD1306 display(OLED_DC, OLED_RESET, OLED_CS);

Adafruit_BME280 bme; // sensor in I2C mode
HTTPClient http;
char SESSION_TOKEN[26] = "";

void setupWiFi() {
    Serial.println(F("Connecting to WiFi"));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // Sometimes the WiFi has an IP address, but reports itself as NOT CONNECTED
    // This can happen for some Asus routers.
    // Also happens when you use static MAC-IP binding.
    while (WiFi.status() != WL_CONNECTED && !WiFi.localIP()) {
        delay(500);
        Serial.print(F("."));
    }
    Serial.println();
    Serial.print(F("Connected, IP address: "));
    Serial.println(WiFi.localIP());
}

void initSensors() {
    Serial.println(F("Init sensors"));
    // default settings
    if (!bme.begin()) {
        Serial.println(F("Could not find a valid BME280 sensor, check wiring!"));
        while (1);
    }
    Serial.println(F("Sensors inited"));
}

void setupDisplay() {
    Serial.println(F("Init screen"));
    display.begin(SSD1306_SWITCHCAPVCC);
    // Default contrast is too high, dim the display
    display.dim(true);
    // Default logo will be displayed
    display.display();
    delay(1000);
    display.clearDisplay();

    display.setFont(&Monaco9pt7b);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.clearDisplay();

    Serial.println(F("Screen inited"));
}

// TODO Return something useful
// Ref: https://arduinojson.org/v5/faq/why-does-the-generated-json-contain-garbage/#example-1-the-jsonbuffer-is-destructed
template<typename TJsonBuffer>
JsonObject& callAPI(String api, JsonObject& data, TJsonBuffer& jsonBuffer, bool withToken=false) {
    if (withToken && strlen(SESSION_TOKEN) == 0) {
        Serial.println("No token. Please login first");
        panic();
    }

    http.begin(api, true);
    http.setUserAgent(String("EnvSensor v") + VERSION);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-LC-Id", API_APP_ID);
    http.addHeader("X-LC-Key", API_APP_KEY);
    if (withToken) {
        http.addHeader("X-LC-Session", SESSION_TOKEN);
    }

    // JsonObject => String
    String post_data;
    data.printTo(post_data);
    // Serial.printf("Sending data: %s to URL: %s\n", post_data.c_str(), api.c_str());

    int httpCode = http.POST(post_data);
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
            // Parse response into JSON
            // Use https://arduinojson.org/v5/assistant/ to calculate maximum size of your JSON object
            JsonObject& json_data = jsonBuffer.parseObject(http.getString());
            if (!json_data.success()) {
                Serial.println("Error parsing JSON");
                panic();
            }
            http.end();
            return json_data;
        } else {
            Serial.printf("HTTP %i: %s\n", httpCode, http.getString().c_str());
            panic();
        }
    } else {
        Serial.printf("HTTP %i: %s\n", httpCode, http.errorToString(httpCode).c_str());
        panic();
    }
}

/**
 * Login and sets `SESSION_TOKEN`
 */
void login() {
    if (strlen(SESSION_TOKEN) > 0) {
        return;
    }

    char eof[2+1] = "OK";

    Serial.println("Reading token...");
    EEPROM.begin(32);
    EEPROM.get(0, SESSION_TOKEN);
    char okay[2+1];
    EEPROM.get(0+sizeof(SESSION_TOKEN), okay);
    EEPROM.end();

    if (String(okay) == String(eof)) {
        Serial.printf("Got saved token: %s\n", SESSION_TOKEN);
    } else {
        Serial.println("No saved token, login...");
        DynamicJsonBuffer sendBuffer(JSON_OBJECT_SIZE(2));
        JsonObject& user = sendBuffer.createObject();
        user["username"] = API_USERNAME;
        user["password"] = API_PASSWORD;

        DynamicJsonBuffer recvBuffer(JSON_OBJECT_SIZE(8));
        String token = callAPI(API_LOGIN, user, recvBuffer)["sessionToken"].as<String>();
        token.toCharArray(SESSION_TOKEN, sizeof(SESSION_TOKEN));
        Serial.printf("Login got token: %s\n", SESSION_TOKEN);
        sendBuffer.clear();
        recvBuffer.clear();

        // Save sessionToken
        EEPROM.begin(32);
        EEPROM.put(0, SESSION_TOKEN);
        EEPROM.put(0+sizeof(SESSION_TOKEN), eof);
        EEPROM.commit();
        Serial.printf("Token saved: %s\n", SESSION_TOKEN);
    }
}

JsonObject& readBME280(StaticJsonBuffer<200>& buffer) {
    JsonObject& data = buffer.createObject();
    data["sensor"] = DEVICE_ID;
    // 2 digits precision will be fine
    data["temperature"] = round(bme.readTemperature() * 100) / 100.0;
    data["humidity"] = round(bme.readHumidity() * 100) / 100.0;
    // convert air pressure value to hPa at the same time
    data["pressure"] = round(bme.readPressure() * 100) / 10000.0;
    return data;
}

void refreshScreen(JsonObject& data) {
    display.clearDisplay();
    // Cursor position should indicate the top left corner of next string to be
    // printed, according to the documentation of Adafruit GFX library.
    // However it is to be found to present the bottom left corner instead.
    // Strange behavior.
    display.setCursor(1, 14);

    // Temperature
    String temperature = String(data["temperature"].as<float>(), 2);
    display.print(temperature);
    // Degree symbol is not supported by Adafruit GFX fonts,
    // so we manually draw a circle here.
    display.drawCircle(display.getCursorX() + 6, 4, 2, WHITE);
    display.println(F(" C"));

    // Relative humidity
    display.print(String(data["humidity"].as<float>(), 1));
    display.println(F("%"));

    // Pressure
    display.print(String(data["pressure"].as<float>(), 2));
    display.println(F("hPa"));

    // Show
    display.display();
}

void loop() {
    // Read sensor data
    StaticJsonBuffer<200> buffer;
    JsonObject& sensor_data = readBME280(buffer);
    refreshScreen(sensor_data);
    sensor_data.printTo(Serial);

    // Send data to server
    DynamicJsonBuffer recvBuffer(JSON_OBJECT_SIZE(2));
    callAPI(API_PUBLISH, sensor_data, recvBuffer, true);

    // deepsleep 10 minutes
    ESP.deepSleep(600e6);
}

void setup() {
    Serial.begin(115200);
    Serial.println();

    setupDisplay();
    setupWiFi();
    initSensors();
    login();
}
