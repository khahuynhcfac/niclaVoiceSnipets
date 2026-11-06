# INA226 spike RESULTS

Filled 2026-07-27 during §9 Stage 0 closed-loop spike.

## Environment

| Item | Value |
|---|---|
| Date | 2026-07-27 |
| arduino-cli | 1.5.1 (`C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`) |
| FQBN | `arduino:mbed_nicla:nicla_voice` |
| Core | mbed_nicla (Nicla Voice on COM8, USB VID:PID=2341:0065) |
| INA226_WE | **1.3.0** vendored at `spikes/ina226/libraries/INA226_WE` (also `arduino-cli lib install INA226_WE`) |
| Etherkit Si5351 | 2.2.0 (user library, coexistence check) |
| Nicla USB COM | **COM8** |
| Spike flash | 64948 B (12%) |
| Spike RAM | 12840 B (19%) |

## 0A — Bring-up

| Item | Value |
|---|---|
| INA226_WE compile OK? | **YES** |
| I2C address found | **0x40 ACK** (scan) |
| Matches §2.3 `0x40`? | **YES** |
| `init()` | **OK** (`INA ok=1`) |
| Calibration | `setResistorRange(0.1, 0.8192)` |
| Default AVG / CT | 16 / 1100 µs — applied and confirmed via `info` |
| `avg 64` / `conv 2116` | **OK** (commands accepted; readings still return) |
| Bus V (this run) | **~0.0037 V** |
| Current (this run) | **~0 mA** (noise ±0.15 mA) |
| Chip power (this run) | **~0 mW** |
| Overflow / I2C err | ovf=0, i2c_err=0 |
| Vs meter notes | **Not done** — no ~7.5 V load present on VBUS during this run (reading consistent with unpowered / open sense) |

## 0B — Coexistence

| Item | Value |
|---|---|
| Si5351 + INA on same bus OK? | **YES** |
| Scan after both up | `0x40`, `0x60` ACK |
| Safe init order (this spike) | `nicla::begin()` → **one** `Wire.begin()` → `ina226.init()` + config → later `si5351.init()` on command `si5351` |
| Extra `Wire.begin` issues? | Not exercised beyond that single call + Etherkit’s internal `Wire.begin()` inside `si5351.init()` (known Mbed leak if repeated — call `si5351` only once per boot in this spike) |
| Si5351 status after init | `SYS_INIT=0 LOL_A=0 LOL_B=0 LOS=0 REVID=1` |
| INA still readable after Si5351 init? | **YES** (`read` / `info` with `SI5351 ok=1`) |

## Gate (§9 Stage 0)

| Criterion | Pass? |
|---|---|
| Builds on Nicla | **YES** |
| Device at documented address | **YES** (0x40) |
| Credible V / I / P with ~7.5 V load | **NOT YET** — chip talks; bus ≈ 0 V this session (wire rail / load and re-check) |
| Coexistence notes written | **YES** |
| §2.3 address/wiring update needed? | Address **no**. Wiring note: confirm VBUS→IN− and 7.5 V rail before closing the “credible readings” gate |

**Stage 0 software/lib gate: PASS.**  
**Stage 0 metrology gate: OPEN** until a powered high-side load is measured once and logged here.

## Notes

- Arduino IDE had COM8 open (Serial Monitor); process was closed so the verify script could attach.
- Raw transcript: `spikes/ina226/verify_log.txt`.
- Compile used:  
  `arduino-cli compile --fqbn arduino:mbed_nicla:nicla_voice --libraries spikes/ina226/libraries spikes/ina226/ina226_spike`
- Upload: `arduino-cli upload -p COM8 --fqbn … spikes/ina226/ina226_spike`
- Product driver should follow this init order and avoid repeated `Wire.begin()`.
