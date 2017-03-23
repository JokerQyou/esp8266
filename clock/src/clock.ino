/***************************************************************************
 ***************************************************************************/

/**
  Required PlatformIO libraries: (ID)
  Sensors: 31, 166;
  SSD1306 OLED screen: 13, 135;
  MQTT client: 89;
 **/
extern "C" {
    #include "user_interface.h"
}
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include "Monaco12pt7b.h"

#include <Wire.h> // must be included here so that Arduino library object file references work
#include <RtcDS3231.h>

// User configuration
#include "user_config.h"
#ifndef TIMEZONE
    #define TIMEZONE 0
#endif

#define countof(a) (sizeof(a) / sizeof(a[0]))

// SSD1306 screen in hardware SPI mode
// Notice: these are GPIO pin numbers
// #define OLED_SDA     2
// #define OLED_SCL     15
Adafruit_SSD1306 display(15);
RtcDS3231<TwoWire> Rtc(Wire);

long last_refresh_screen = 0;
long last_connection_attempt = 0;

void setup() {
    Serial.begin(115200);
    Rtc.Begin();
    setupDisplay();

    // Enable light sleep
    wifi_set_opmode(NULL_MODE);
    wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
    wifi_fpm_open();

    // setupWiFi();
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

void setupDisplay() {
    Serial.println(F("Init screen"));
    display.begin(SSD1306_SWITCHCAPVCC);
    // Default contrast is too high, dim the display
    display.dim(true);
    // Default logo will be displayed
    display.display();
    delay(1000);
    display.clearDisplay();

    display.setFont(&Monaco12pt7b);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.clearDisplay();

    Serial.println(F("Screen inited"));
}

void loop() {
    long now = millis();

    // If current uptime is smaller than last publish time, then we're hitting
    // the max uptime value for unsigned int type, reset some values accordingly
    // if (now - last_refresh_screen < 0) {
    //     last_refresh_screen = 0;
    // }

    // wifi_fpm_set_wakeup_cb(callback);
    // Here we're using light sleep to save some energy
    refreshScreen();
    wifi_fpm_do_sleep(999999); // ~1sec in us
    delay(1000); // 1sec in ms
}

void refreshScreen() {
    display.clearDisplay();
    // Cursor position should indicate the top left corner of next string to be
    // printed, according to the documentation of Adafruit GFX library.
    // However it is to be found to present the bottom left corner instead.
    // Strange behavior.
    display.setCursor(6, 18);

    RtcDateTime dt = Rtc.GetDateTime();
    char datestring[9];
    int hour = dt.Hour() + TIMEZONE;

    if (hour >= 24) {
        hour = 24 - hour;
    } else if (hour < 0) {
        hour = 24 + hour;
    }

    snprintf_P(datestring,
        countof(datestring),
        PSTR("%02u:%02u:%02u"),
        // dt.Month(),
        // dt.Day(),
        // dt.Year(),
        hour,
        dt.Minute(),
        dt.Second()
    );
    display.println(datestring);
    // Show
    display.display();
}
