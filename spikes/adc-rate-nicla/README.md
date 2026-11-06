# ADC rate spike — Nicla Voice

Throwaway proof for Energize RX **Stage A** / design O3
(`SCPI_Energize_RX_NICLA/docs/SCPI_Energize_RX_NICLA.md` §4.4.9).
Nothing here is product firmware. Record findings in `RESULTS.md`.

## Goal

Measure sustained 12-bit `analogRead(A0)` samples/s on Nicla Voice, raw and with one EWMA update per sample.

## Build / upload

```bash
CLI="$LOCALAPPDATA/arduino-cli/arduino-cli.exe"
FQBN=arduino:mbed_nicla:nicla_voice
ROOT="D:/OneDrive - Cardiff University/PhD/Research/SCPI_Si5351"

"$CLI" compile -b "$FQBN" "$ROOT/spikes/adc-rate-nicla/adc_rate_spike"
"$CLI" upload -p COMx --fqbn "$FQBN" "$ROOT/spikes/adc-rate-nicla/adc_rate_spike"
```

USB Serial @ **115200**.

### Commands

| Command | Action |
|---|---|
| `help` | List commands |
| `raw` | ≥2 s tight-loop `analogRead(A0)` count |
| `ewma` | Same + EWMA α=0.2 per sample |

## Gate

Copy sustained S/s into `RESULTS.md`, then set `ENERGY_SAMPLE_RATE_MAX_HZ` (and revise default 1000 only if needed) in Energize `config_nicla.h` + design doc.
