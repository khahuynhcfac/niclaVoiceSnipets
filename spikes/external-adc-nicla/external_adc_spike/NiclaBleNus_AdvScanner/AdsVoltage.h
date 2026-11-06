#pragma once

#include <stdint.h>
#include <Adafruit_ADS1X15.h>

static constexpr adsGain_t ADS_GAIN = GAIN_ONE;

int32_t countsToUv(int16_t counts);
