#ifndef BLE_STATUS_LED_H
#define BLE_STATUS_LED_H

#include <Arduino.h>

#include "BleNusPeripheral.h"

// Shows the BLE link state on the Nicla's RGB LED:
//   Idle        off
//   Pairing     blue, fast blink
//   Advertising blue, slow blink
//   Connected   green, solid
//
// This module is presentation only. A project that needs the LED for something
// else can delete it without touching the BLE stack.
class BleStatusLed {
public:
    void begin();

    // Refreshes the colour and advances the blink phase. Call every loop().
    void update(BleLinkState state);

private:
    void blinkBlue(uint32_t togglePeriodMs);
    void setColor(uint8_t red, uint8_t green, uint8_t blue);

    uint32_t _lastToggleMs = 0;
    bool     _blinkVisible = false;
    uint8_t  _red          = 0;
    uint8_t  _green        = 0;
    uint8_t  _blue         = 0;
};

#endif  // BLE_STATUS_LED_H
