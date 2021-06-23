#include <WiFi.h>
#include "WebCopy.h"
#include "WiFiClientSecure.h"

using namespace tinyhttp;

WiFiClientSecure client;

void setup() {
    Serial.begin(115200);
    // connect to WIFI
    WiFi.begin("Phil Schatzmann", "sabrina01");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    // setup logger
    Log.setLogger(Serial, Info);

    // start the dump 
    WebCopy webCopy(client, false);
    webCopy.start("https://www.pschatzmann.ch/home");
}

void loop() {
}