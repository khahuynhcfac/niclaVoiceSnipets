#pragma once

#include <stdint.h>
#include "AdsVoltage.h"

#define ADS_POLL_SPS 128

bool adsSamplerBegin();
void adsSamplerStart();
bool adsSamplerNewest(int16_t* sample);
bool adsSamplerNext(int16_t* out);  // drain one sample from the ring; returns false when empty
void adsSamplerPrintMicrovolts();
