#pragma once

#include <stdint.h>

// Average receiving power from the energy stored on the capacitor over one
// harvester-on interval.
//
//   harvester off, average VOLT_AVG_SAMPLES  -> V0
//   harvester on for CHARGE_INTERVAL_MS
//   harvester off, average VOLT_AVG_SAMPLES  -> V1
//   P = (E(V1) - E(V0)) / t
//
// Arduino pin 5 (J1 LPIO0_EXT, P0_24) drives an NMOS gate.
// HIGH makes the channel conduct and disables the harvester.
// LOW leaves the channel off and lets the harvester charge.

#define CAP_UF               22000  // capacitance in µF (22 mF)
#define HARVEST_PIN              5
#define HARVEST_OFF           HIGH  // NMOS on, harvester disabled
#define HARVEST_ON             LOW  // NMOS off, harvester charging
#define CHARGE_INTERVAL_MS    1000  // harvester on-time t
#define VOLT_AVG_SAMPLES        10  // quiet ADC reads at each end

// Drive the gate off and arm the first V0 average. Call after the 3.3 V
// LDO is up and before adsSamplerStart().
void capPowerBegin();

// Feed one raw ADS1115 count. Returns true when a cycle has finished and
// capPowerGet_uW() holds the new result. The harvester stays off until
// capPowerRearm().
bool capPowerFeed(int16_t counts);

// Latest power in microwatts (µW). Valid after capPowerFeed() returns true.
int16_t capPowerGet_uW();

// Drop queued samples and start the next V0 average. Call after the result
// has been sent, so the BLE wait is not included in t or in V0.
void capPowerRearm();
