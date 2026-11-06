#pragma once

#include <stdint.h>

// Must match ADV_APP_CODE in NiclaBleNus_AdvScanner/NiclaBleNus_AdvScanner.ino.
#define ADV_APP_CODE 0xC3

bool adsAdvertBegin();
void adsAdvertSend(int16_t power_uW);
