#include "CapPower.h"
#include "CapEnergy.h"
#include "AdsVoltage.h"
#include "AdsSampler.h"

#include <Arduino.h>

enum ChargePhase : uint8_t {
    PHASE_V0,
    PHASE_CHARGE,
    PHASE_V1,
    PHASE_IDLE,
};

static ChargePhase gPhase = PHASE_IDLE;
static int32_t gSumUv = 0;
static int32_t gGot = 0;
static int32_t gV0Uv = 0;
static uint32_t gChargeStartMs = 0;
static int16_t gPower_uW = 0;

static void harvesterWrite(uint8_t level) {
    digitalWrite(HARVEST_PIN, level);
}

static void resetAverage() {
    gSumUv = 0;
    gGot = 0;
}

static int16_t powerUw(int32_t v0Uv, int32_t v1Uv) {
    const int64_t e0 = capEnergyNj(v0Uv, CAP_UF);
    const int64_t e1 = capEnergyNj(v1Uv, CAP_UF);
    // nJ / ms = µW
    const int64_t uw = (e1 - e0) / CHARGE_INTERVAL_MS;
    if (uw > 32767) {
        return 32767;
    }
    if (uw < -32768) {
        return -32768;
    }
    return (int16_t)uw;
}

static bool takeQuietSample(int16_t counts, int32_t* avgUv) {
    gSumUv += countsToUv(counts);
    ++gGot;
    if (gGot < VOLT_AVG_SAMPLES) {
        return false;
    }
    *avgUv = gSumUv / VOLT_AVG_SAMPLES;
    resetAverage();
    return true;
}

void capPowerBegin() {
    pinMode(HARVEST_PIN, OUTPUT);
    harvesterWrite(HARVEST_OFF);
    resetAverage();
    gPower_uW = 0;
    gPhase = PHASE_V0;
}

void capPowerRearm() {
    harvesterWrite(HARVEST_OFF);
    adsSamplerFlush();
    resetAverage();
    gPhase = PHASE_V0;
}

bool capPowerFeed(int16_t counts) {
    switch (gPhase) {
    case PHASE_V0: {
        int32_t avgUv = 0;
        if (!takeQuietSample(counts, &avgUv)) {
            return false;
        }
        gV0Uv = avgUv;
        harvesterWrite(HARVEST_ON);
        gChargeStartMs = millis();
        gPhase = PHASE_CHARGE;
        return false;
    }
    case PHASE_CHARGE:
        if ((int32_t)(millis() - gChargeStartMs) < CHARGE_INTERVAL_MS) {
            return false;
        }
        harvesterWrite(HARVEST_OFF);
        adsSamplerFlush();
        resetAverage();
        gPhase = PHASE_V1;
        return false;
    case PHASE_V1: {
        int32_t avgUv = 0;
        if (!takeQuietSample(counts, &avgUv)) {
            return false;
        }
        gPower_uW = powerUw(gV0Uv, avgUv);
        gPhase = PHASE_IDLE;
        return true;
    }
    case PHASE_IDLE:
    default:
        return false;
    }
}

int16_t capPowerGet_uW() {
    return gPower_uW;
}
