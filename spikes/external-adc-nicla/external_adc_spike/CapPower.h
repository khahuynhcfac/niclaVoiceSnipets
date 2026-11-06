#pragma once

#include <stdint.h>

// Capacitor power via OLS over a 1-second window of ADS1115 samples.
//
// Call capPowerFeed() once per raw ADC count (from adsSamplerNext).
// Every PWR_WINDOW_SPS calls it runs OLS on V vs. sample index,
// evaluates the fitted line at both ends of the window, computes
// E = ½CV² at each end, and stores  P = ΔE / Δt.
//
// Positive result = energy increasing (charging).
// Negative result = energy decreasing (discharging).

#define CAP_UF         22000  // capacitance in µF (22 mF)
#define PWR_WINDOW_SPS   128  // samples per power window; must equal ADS_POLL_SPS

// Feed one raw ADS1115 count. Returns true when a full window is complete
// and a new power value is available via capPowerGet_uW().
bool capPowerFeed(int16_t counts);

// Latest power result in microwatts (µW). Valid after capPowerFeed() returns true.
int16_t capPowerGet_uW();
