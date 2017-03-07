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
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <CustomFonts/Monaco9pt7b.h>

// User configuration
#include "user_config.h"
// SSD1306 screen in hardware SPI mode
// Notice: these are GPIO pin numbers
#define OLED_DC     2
#define OLED_CS     15
#define OLED_RESET  16
#define SSD1306_128_64
Adafruit_SSD1306 display(OLED_DC, OLED_RESET, OLED_CS);

Adafruit_BME280 bme; // sensor in I2C mode

long last_publish = 0;
long last_refresh_screen = 0;
long last_connection_attempt = 0;

struct BME280_data {
    float temperature;
    float pressure; // in Pa
    float humidity; // in percentage
};
struct BME280_data sensor_data;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void setup() {
    Serial.begin(115200);
    setupDisplay();
    setupWiFi();
    initSensors();
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    Serial.println();
}

void setupWiFi() {
    Serial.println(F("Connecting to WiFi"));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(F("."));
    }
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

// Reconnect MQTT client
void reconnectMQTT() {
    Serial.println(F("Attempting MQTT connection"));
    mqttClient.connect(String(MQTT_CLIENT_ID).c_str(), MQTT_USERNAME, MQTT_PASSWORD);
}

void loop() {
    long now = millis();

    // If current uptime is smaller than last publish time, then we're hitting
    // the max uptime value for unsigned int type, reset some values accordingly
    if (now - last_refresh_screen < 0) {
        last_refresh_screen = 0;
        last_publish = 0;
        last_connection_attempt = 0;
    }

    if (mqttClient.connected()) {
        mqttClient.loop();

        if (now - last_publish > 30000) {
            last_publish = now;
            publishSensorData();
        }

    } else {
        if (now - last_connection_attempt > 5000) {
            last_connection_attempt = now;
            reconnectMQTT();
        }
    }

    if (now - last_refresh_screen > 2000) {
        last_refresh_screen = now;
        sensor_data = readBME280();
        refreshScreen();
    }
}

struct BME280_data readBME280() {
    struct BME280_data data;
    data.temperature = bme.readTemperature();
    data.pressure = bme.readPressure();
    data.humidity = bme.readHumidity();
    return data;
}

void publishSensorData() {
    Serial.println(F("Publishing sensor data"));
    // Publish sensor message
    String temperature = String(sensor_data.temperature, 2);
    String pressure = String(sensor_data.pressure / 100.0F, 2);
    String humidity = String(sensor_data.humidity, 1);
    mqttClient.publish("/nodemcu/temperature", temperature.c_str());
    mqttClient.publish("/nodemcu/pressure", pressure.c_str());
    mqttClient.publish("/nodemcu/humidity", humidity.c_str());
}

void refreshScreen() {
    display.clearDisplay();
    // Cursor position should indicate the top left corner of next string to be
    // printed, according to the documentation of Adafruit GFX library.
    // However it is to be found to present the bottom left corner instead.
    // Strange behavior.
    display.setCursor(1, 14);

    // Temperature
    String temperature = String(sensor_data.temperature, 2);
    display.print(temperature);
    // Degree symbol is not supported by Adafruit GFX fonts,
    // so we manually draw a circle here.
    display.drawCircle(display.getCursorX() + 6, 4, 2, WHITE);
    display.println(F(" C"));

    // Relative humidity
    display.print(String(sensor_data.humidity, 1));
    display.println(F("%"));

    // Pressure
    display.print(String(sensor_data.pressure / 100.0F, 2));
    display.println(F("hPa"));

    // Show
    display.display();
}
