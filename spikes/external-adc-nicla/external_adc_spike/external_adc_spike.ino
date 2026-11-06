// Nicla Voice + ADS1115 reference snippet. Single-shot conversions on AIN0.
// Each sample is started from the TIMER1 tick. The I2C transfer runs in that
// interrupt, so loop() cannot move the interval.
// Do not call Wire after adsSamplerStart().
// J2 LPIO3 is Arduino pin 0 (P0_10). Wire is SDA pin 4 (P0_22), SCL pin 3 (P0_23).

#include "AdsAdvert.h"
#include "AdsSampler.h"
#include "AdsVoltage.h"
#include "CapPower.h"

#define SERIAL_BAUD 115200

void setup() {
    if (!adsSamplerBegin()) {
        Serial.begin(SERIAL_BAUD);
        Serial.println("ADS1115 not ready");
        return;
    }

    Serial.begin(SERIAL_BAUD);
    if (!adsAdvertBegin()) {
        Serial.println("BLE not ready");
    }
    adsSamplerStart();
}

void loop() {
    int16_t sample = 0;
    while (adsSamplerNext(&sample)) {
        Serial.println(countsToUv(sample));
        if (capPowerFeed(sample)) {
            adsAdvertSend(capPowerGet_uW());
        }
    }
}
