#include "AdsAdvert.h"

#include <ArduinoBLE.h>
#include "utility/HCI.h"

// ADV_NONCONN_IND minimum interval is 100 ms (160 * 0.625 ms).
static constexpr uint16_t ADV_INTERVAL = 160;

// Low-rate branch: poll window must cover at least one full advertising event.
// First event fires within advDelay (0–10 ms); next at ADV_INTERVAL (100 ms).
// 120 ms = 100 ms interval + 20 ms margin.
static constexpr uint32_t ADV_WINDOW_LOW_MS = 120;

// High-rate threshold: if adsAdvertSend() is called more often than this,
// leave advertising on between calls (duty cycle already high enough).
static constexpr uint32_t ADV_HIGH_RATE_MS = 200;

static bool     gBleReady    = false;
static bool     gAdvRunning  = false;
static uint32_t gLastSendMs  = 0;

static uint8_t payloadChecksum(uint8_t b0, uint8_t b1, uint8_t b2) {
    return (uint8_t)(b0 + b1 + b2);
}

static void loadAdvertisingPayload(int16_t power_uW) {
    uint8_t adv[4];
    adv[0] = ADV_APP_CODE;
    adv[1] = (uint8_t)(uint16_t)power_uW;
    adv[2] = (uint8_t)((uint16_t)power_uW >> 8);
    adv[3] = payloadChecksum(adv[0], adv[1], adv[2]);
    HCI.leSetAdvertisingData(4, adv);

    uint8_t unused = 0;
    HCI.leSetScanResponseData(0, &unused);
}

bool adsAdvertBegin() {
    if (!BLE.begin()) {
        return false;
    }
    BLE.setConnectable(false);
    BLEAdvertisingData emptyScan;
    BLE.setScanResponseData(emptyScan);
    BLE.setAdvertisingInterval(ADV_INTERVAL);
    BLE.advertise();
    BLE.stopAdvertise();
    gBleReady = true;
    return true;
}

void adsAdvertSend(int16_t power_uW) {
    if (!gBleReady) {
        return;
    }

    const uint32_t now      = millis();
    const uint32_t interval = now - gLastSendMs;   // measured before updating
    gLastSendMs = now;

    loadAdvertisingPayload(power_uW);

    if (interval < ADV_HIGH_RATE_MS) {
        // High-rate path: leave advertising on; just update the payload.
        if (!gAdvRunning) {
            HCI.leSetAdvertiseEnable(0x01);
            gAdvRunning = true;
        }
    } else {
        // Low-rate path: pulse advertising for one guaranteed event, then off.
        if (!gAdvRunning) {
            HCI.leSetAdvertiseEnable(0x01);
        }
        BLE.poll(ADV_WINDOW_LOW_MS);
        HCI.leSetAdvertiseEnable(0x00);
        gAdvRunning = false;
    }
}
