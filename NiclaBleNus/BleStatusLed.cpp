#include "BleStatusLed.h"

#include <Nicla_System.h>

#include "BleNusConfig.h"

static constexpr uint8_t LED_FULL = 255;

void BleStatusLed::begin() {
    nicla::leds.begin();
    nicla::leds.setColor(0, 0, 0);
}

void BleStatusLed::update(BleLinkState state) {
    switch (state) {
        case BleLinkState::Idle:
            setColor(0, 0, 0);
            break;
        case BleLinkState::Pairing:
            blinkBlue(BleNusConfig::PAIRING_BLINK_PERIOD_MS);
            break;
        case BleLinkState::Advertising:
            blinkBlue(BleNusConfig::ADVERTISING_BLINK_PERIOD_MS);
            break;
        case BleLinkState::Connected:
            setColor(0, LED_FULL, 0);
            break;
    }
}

void BleStatusLed::blinkBlue(uint32_t togglePeriodMs) {
    const uint32_t now = millis();
    if (now - _lastToggleMs >= togglePeriodMs) {
        _lastToggleMs = now;
        _blinkVisible = !_blinkVisible;
    }
    setColor(0, 0, _blinkVisible ? LED_FULL : 0);
}

void BleStatusLed::setColor(uint8_t red, uint8_t green, uint8_t blue) {
    if (red == _red && green == _green && blue == _blue) return;
    _red   = red;
    _green = green;
    _blue  = blue;
    nicla::leds.setColor(red, green, blue);
}
