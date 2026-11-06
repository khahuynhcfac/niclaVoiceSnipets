# INA226 — hardware / library spike

Throwaway proofs for **§9 Stage 0** of
`SCPI_SI5351_NICLA/docs/SCPI_SI5351_NICLA.md`.
Nothing here is product firmware. Record findings in `RESULTS.md`.

## Tools

```bash
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
FQBN=arduino:mbed_nicla:nicla_voice
ROOT="D:/OneDrive - Cardiff University/PhD/Research/SCPI_Si5351"
```

**INA226_WE 1.3.0** is vendored under `libraries/INA226_WE` for reproducible
`--libraries` builds. Etherkit Si5351 comes from the normal Arduino user libs
(coexistence check).

## Sketch

`ina226_spike/ina226_spike.ino`

```bash
"$CLI" compile --fqbn $FQBN --libraries "$ROOT/spikes/ina226/libraries" \
  "$ROOT/spikes/ina226/ina226_spike"
"$CLI" board list
"$CLI" upload -p COM8 --fqbn $FQBN "$ROOT/spikes/ina226/ina226_spike"
```

USB Serial @ **115200**. Close Arduino IDE Serial Monitor first if COM access is denied.

### Commands

| Command | Action |
|---|---|
| `help` | List commands |
| `scan` | ACK scan on `Wire` |
| `read` | One V / I / P sample (chip power register) |
| `auto on` / `auto off` | 1 Hz print |
| `avg <n>` | Legal on-chip averages: 1,4,16,64,128,256,512,1024 |
| `conv <us>` | Legal CT: 140,204,332,588,1100,2116,4156,8244 |
| `si5351` | One-shot Etherkit init on same bus (coexistence) |
| `info` | Addr, shunt, Imax, avg, conv, flags |

### Expected checks (§9 Stage 0)

1. `nicla::begin()`, one `Wire.begin()`, INA226 at `0x40`.
2. `setResistorRange(0.1, 0.8192)`; AVG 16 + CT 1100; continuous.
3. With ~7.5 V load on VBUS/IN−: compare `read` to a meter (still open in RESULTS if rail not connected).
4. Change `avg` / `conv` from Serial.
5. `si5351` then `scan` / `read` — both `0x40` and `0x60`, INA still works.
6. Record flash/RAM and findings in `RESULTS.md`.

*Gate:* see §9 Stage 0 and current `RESULTS.md`.
