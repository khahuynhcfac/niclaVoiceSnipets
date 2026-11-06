# ADC rate spike — RESULTS

**Board:** Arduino Nicla Voice  
**Resolution:** 12-bit (`analogReadResolution(12)`), pin `A0`  
**Date:** 2026-08-12

## Runs

| Case | Sustained S/s | Notes |
|---|---:|---|
| raw (≥2 s) | _pending hardware_ | |
| ewma α=0.2 (≥2 s) | _pending hardware_ | |

## Product lock (provisional until bench filled)

Until a physical run fills the table above, product firmware uses a **conservative** lock consistent with design intent and `N_cap=2000`:

| Macro | Value | Rationale |
|---|---:|---|
| `ENERGY_SAMPLE_RATE_DEFAULT_HZ` | 100 | 10 ms/tick so `AVER:COUN` fits; was 1000 |
| `ENERGY_SAMPLE_RATE_MAX_HZ` | 2000 | Caps `RATE×DUR≤2000` at DUR≥1 s; leaves headroom if SAADC/`analogRead` exceeds 1 kHz |

After bench measurement: replace MAX with `floor(0.8 * measured_ewma_Sps)` (and lower default if that is &lt; 1000).

## Flash / RAM (spike sketch)

Sketch uses **61592** bytes (11%) flash; globals **12768** bytes (19%) RAM.
