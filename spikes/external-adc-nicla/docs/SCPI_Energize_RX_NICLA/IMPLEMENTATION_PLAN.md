# Energize RX — Detailed Implementation Plan (v1)

**Normative design:** [`SCPI_Energize_RX_NICLA.md`](SCPI_Energize_RX_NICLA.md)  
**Host guide (refresh in Stage E):** [`SCPI_DRIVER_GUIDE.md`](SCPI_DRIVER_GUIDE.md)  
**Tree:** `SCPI_Energize_RX_NICLA/` (v1 implemented)  
**Board / FQBN:** `arduino:mbed_nicla:nicla_voice`  
**Post-D build:** ~305944 B flash (57%); ~52872 B RAM (82%).

**Rules:** Essential only; match parent Nicla coding style; update the design doc if behaviour must change; do not invent extras.

**Order:** A → B → C → D → E. Do not start D until A’s RATE max is written into the design doc and `config_nicla.h`. Stage C may overlap late B only for macros that do not depend on spike RESULTS.

---

## 0. Success definition (v1 done)

1. Board **without Si5351** boots to UART+BLE SCPI (no halt).
2. Board **without INA226** boots with WARN only; ch1 energy works; ch3–5 → `0`+`-241`.
3. Energy path matches design §4.4–§4.5: `CAP`, `SCAL`, `SAMP:RATE`/`DUR`, `FILT`, `MEAS1:ENER?`/`POW?` (OLS), unset-\(C\) → `-221`.
4. `*IDN?` → `Arduino,EnergizeRX-NiclaVoice,001,0.1.0`; BLE name `EnergizeRX-AABBCC`.
5. Flash/RAM reported; driver guide matches wire behaviour; smoke checklist signed off.

---

## 1. Build / flash baseline

```bash
CLI="$LOCALAPPDATA/arduino-cli/arduino-cli.exe"   # or your install
FQBN=arduino:mbed_nicla:nicla_voice
SKETCH="…/SCPI_Energize_RX_NICLA"
USER_LIBS="…/Documents/Arduino/libraries"         # Etherkit not needed after B

"$CLI" compile -b "$FQBN" --library "$USER_LIBS/ArduinoBLE" \
  --library "$USER_LIBS/HardwareBLESerial" \
  --library "$USER_LIBS/INA226_WE" \
  "$SKETCH"
```

Record **flash %** and **RAM %** after Stages B and D.

---

## Stage A — ADC rate spike (gate for O3)

**Goal:** Measure max sustainable 12-bit `analogRead(A0)` on Nicla Voice; lock `SAMP:RATE` MAX (and default 1000 only if sustainable with ~80% margin).

### A.1 Create spike tree

| Path | Action |
|---|---|
| `spikes/adc-rate-nicla/README.md` | Purpose, build/upload, how to read Serial |
| `spikes/adc-rate-nicla/RESULTS.md` | Fill after runs (template in README) |
| `spikes/adc-rate-nicla/adc_rate_spike/adc_rate_spike.ino` | Measurement sketch |

Style: follow `spikes/ina226/` (throwaway, not product firmware).

### A.2 Sketch behaviour

1. `nicla::begin()`; `analogReadResolution(12)`.
2. **Case 1 — raw:** tight loop, count `analogRead(A0)` for ≥2 s (`micros()`), print sustained S/s + min/max inter-sample µs.
3. **Case 2 — +EWMA:** same loop with one EWMA update (`α=0.2`) per sample; print S/s.
4. **Case 3 (optional):** `BLE.begin()` + advertise idle; repeat case 2; note impact.
5. Serial @ 115200; commands e.g. `raw`, `ewma`, `ble` (optional), `help`.

No SCPI, no Si5351, no INA required.

### A.3 Exit criteria

| Check | Pass |
|---|---|
| RESULTS.md has numbers for case 1 and 2 | Yes |
| Design doc §4.4.7 / §8.2 O3 updated with MAX (and default if revised) | Yes |
| `config_nicla.h` gains `ENERGY_SAMPLE_RATE_MAX_HZ` (and default if needed) | In Stage C or immediately after A |

**Stop:** If raw max ≪ 1000 S/s, lower default RATE in design before Stage D.

---

## Stage B — Si5351 excision + identity

**Goal:** Product boots on Energize hardware; no generator code paths.

### B.1 Delete / stop compiling

| File | Action |
|---|---|
| `Si5351Driver.h` | **Delete** |
| `Si5351Driver.cpp` | **Delete** |
| Etherkit `#include` / link | Remove from all remaining sources |

### B.2 `SCPI_Energize_RX_NICLA.ino`

| Task | Detail |
|---|---|
| Remove `Si5351Driver` include/instance | — |
| `ScpiParser` ctor | Sensors + transport only (see B.3) |
| `setup()` | `nicla::begin()` → LED → Serial → **`Wire.begin()` once** (INA/ADC need bus; was previously via Si5351 init) → `sensors.begin()` → `bleManager.begin()` |
| Banner | `=== Energize RX SCPI (Nicla Voice) ===` |
| Halt loop | **Remove** Si5351 fail-halt |
| `loop()` fault | `sensors.isFaulted()` only; recovery only `sensors.tryRecover()`; messages without “Si5351” |
| Comments | Point at Energize design doc |

### B.3 `ScpiParser` — remove generator API

| Task | Detail |
|---|---|
| Ctor | `ScpiParser(SensorManager&, IScpiTransport&)` — drop `Si5351Driver&` |
| Remove | `#include <si5351.h>`, `Si5351Driver.h`, `_drv`, `toClock`, `cmdFuncShape`, `cmdOutput`, `cmdFreqFixed`, `cmdVoltAmpl/Offs`, `cmdPhase`, `cmdImpedance`, SOURCE/OUTPUT dispatch |
| `*SAV` / `*RCL` | Call **`_sensors.storeAll()` / `recallAll()` only** |
| Keep | IDN, RST, OPC, SYST, MEAS/FETC/SENS for sensors (extended in D) |

Unsupported XDG leftovers (if any) stay on existing unsupported path.

### B.4 `config_nicla.h`

| Task | Detail |
|---|---|
| Remove | `#include <si5351.h>`, all `SI5351_*` macros |
| Set | `SCPI_IDN_STRING` → `Arduino,EnergizeRX-NiclaVoice,001,0.1.0` |
| Set | `SCPI_VERSION_STRING` → `V0.1.0` |
| Keep | INA/ADC macros; add energy macros in Stage C if not already |

### B.5 `Ble.cpp`

| Task | Detail |
|---|---|
| `deriveLocalName` | `EnergizeRX-%s` (last 3 MAC bytes, hex uppercase) |
| Magic | **Keep** `0x5351B001` |
| Comments | Cite Energize design §5 |

### B.6 `Sensors.cpp` / headers

| Task | Detail |
|---|---|
| Comments | Drop “Wire already up from Si5351”; document `.ino` owns `Wire.begin()` |
| `begin()` | Still must not halt if INA missing |
| Fault at boot | Ensure missing INA does **not** set `_faulted` or trip recovery spam (design §2.5) |

### B.7 `pinDef_nicla.h`

Update comments (no Si5351 status pin note as driver owner).

### B.8 Exit criteria

| Check | Pass |
|---|---|
| Compiles without Etherkit / Si5351Driver | Yes |
| Flash without Si5351: reaches “UART+BLE SCPI ready” | Yes |
| No Si5351 → no hang | Yes |
| No INA → WARN, still answers `*IDN?` / `MEAS1:VOLT?` | Yes |
| `*IDN?` Energize string; BLE advertises `EnergizeRX-…` | Yes |
| `SOURCE1:FREQ?` unsupported (not Si5351 behaviour) | Yes |
| Flash/RAM noted | Yes |

---

## Stage C — Config, bounds, versioned NVM

**Goal:** All energy knobs exist in `SensorManager` + NVM **even if** burst/OLS is stubbed until D; SCPI MIN/MAX wired.

### C.1 Macros in `config_nicla.h`

```text
ENERGY_SAMPLE_CAP              2000
ENERGY_SAMPLE_RATE_DEFAULT_HZ  100
ENERGY_SAMPLE_RATE_MIN_HZ      1
ENERGY_SAMPLE_RATE_MAX_HZ      <from spike>
ENERGY_SAMPLE_DUR_DEFAULT_S    0.1f
ENERGY_SAMPLE_DUR_MAX_S        10.0f         # also capped by N_cap/rate
CAP_MIN_F / CAP_MAX_F          1e-12f / 10.f
VOLT_SCALE_DEFAULT             1.0f
VOLT_SCALE_MIN / MAX           1e-3f / 1e6f
FILTER defaults                OFF, WIND 10, ALPHA 0.2f (ALPH/WIND unused until host selects EWMA/MOV/MED)
FILTER_WIND_MIN/MAX            1 / 64
```

Enforce: `rate * dur ≤ ENERGY_SAMPLE_CAP` and `dur ≤ ENERGY_SAMPLE_DUR_MAX_S`.

### C.2 `SensorManager` state (header)

Add (names flexible if style matches):

| Member | Role |
|---|---|
| `_capSet` | bool |
| `_capacitanceF` | float (valid only if `_capSet`) |
| `_voltScale` | float, default 1 |
| `_sampleRateHz` | float or uint32 |
| `_sampleDurS` | float |
| `_filtType` | enum OFF/EWMA/MOV/MED |
| `_filtWindow` | uint16 |
| `_filtAlpha` | float |
| Scratch | `static` or member array length `ENERGY_SAMPLE_CAP` of `{uint32_t t_us; float e;}` (or `v_s`) |
| Filter hist | buffer size 64 for MOV/MED |

### C.3 API surface (implement setters/getters now; burst in D)

```text
bool capacitanceSet() const;
bool setCapacitance / getCapacitance
bool setVoltScale / getVoltScale
bool setSampleRateHz / getSampleRateHz
bool setSampleDurationS / getSampleDurationS
bool setFilterType / getFilterType   // token mapping in parser
bool setFilterWindow / getFilterWindow
bool setFilterAlpha / getFilterAlpha
static bool *MinMax helpers for each (like avgMinMax)
bool runEnergyBurst(float& eOut, float& pOut, bool wantPower);
  // Stage C: can return false / stub; Stage D: real
```

`readValue` ch1 voltage: apply **`_voltScale`** to pin volts (also used by `MEAS1:VOLT?`).

`channelSupports`: ch1 allow Voltage, **Energy**, **Power** (extend `Quantity` enum).

### C.4 NVM (`SensorNvmData`)

- Add **version** field (e.g. `uint16_t ver = 2`) + energy fields + `_capSet`.
- New or bumped magic; reject old layout → defaults + `_capSet=false`.
- Sector remains **fourth-to-last**.
- `storeAll` / `recallAll` / boot load include energy knobs.
- Do not touch last sector (former Si5351).

### C.5 Exit criteria

| Check | Pass |
|---|---|
| Macros match design after spike | Yes |
| `*SAV` / `*RCL` / reboot restores scale/rate/dur/filt; unset CAP stays unset | Yes |
| Invalid NVM → defaults, CAP unset | Yes |

---

## Stage D — Energy burst, filters, OLS, SCPI

**Goal:** Full §4.4 wire behaviour.

### D.1 Burst engine (`Sensors.cpp`)

`runEnergyBurst(eOut, pOut, wantPower)`:

1. If `!_capSet` → caller queues `-221`.
2. Compute `N = round(rate * dur)`; if `N < 2` and `wantPower` → fail `-231`; if `N > N_cap` → should have been rejected at SET.
3. Reset filter state.
4. Loop until elapsed ≥ `dur` (use `micros()`):
   - Pace to ~`1/rate` (busy-wait or delayMicroseconds; keep simple).
   - Mean of `AVER:COUN` analogReads (no `APER`) → \(V_{\mathrm{pin}}\) → \(V=S\cdot V_{\mathrm{pin}}\) → filter → \(V_s\).
   - \(E=0.5f * C * V_s * V_s\); store `{t_us, E}` (or accumulate OLS online **and** keep last E).
5. `eOut = last E`.
6. If `wantPower`: single-pass OLS on stored points (or online sums during loop — prefer **online sums** to avoid needing full buffer for OLS only; still OK to keep buffer for debugging; design allows scratch).  
   Use `float`; time in seconds `(t_us - t0) * 1e-6f`.  
   Guard denom ~0 → `pOut=0` (flat); still success if \(N≥2\).
7. Return true.

**Pacing note:** If `analogRead`+filter cannot meet rate, document actual N in comments/RESULTS; do not invent complex RTOS timing.

### D.2 Filters

| TYPE | Implementation |
|---|---|
| OFF | \(V_s=V\) |
| EWMA | first sample seeds; then α blend |
| MOV | ring mean of last `min(filled, W)` |
| MED | copy window, insertion sort or partial, median |

### D.3 `ScpiParser` energy commands

| Path | Behaviour |
|---|---|
| `MEAS`/`FETC` + `ENER` / `ENERGY` | `runEnergyBurst`; NR3 `eOut`; unset C → `0`+`-221` |
| `MEAS`/`FETC` + `POW` on ch1 | burst + OLS; NR3 `pOut`; &lt;2 → `0`+`-231` |
| `MEAS1:VOLT?` | existing ADC path × scale (no energy burst) |
| `SENS1:CAP` | SET/GET/MIN/MAX; unset GET → `0`+`-221` |
| `SENS1:VOLT:SCAL` | SET/GET/MIN/MAX |
| `SENS1:SAMP:RATE` / `DUR` | SET/GET/MIN/MAX; SET checks `rate*dur≤N_cap` and dur max → `-222` |
| `SENS1:FILT` | tokens `OFF`/`EWMA`/`MOV`/`MED` (accept long forms per design) |
| `SENS1:FILT:WIND` / `ALPH` | SET inapplicable TYPE → `-221`; GET OK |

Reuse `formatNr3`, parent SENSE parsing style (`cmdSense` extension or sibling).

Ch1 `POW` must **not** route to INA; ch5 unchanged.

### D.4 `*SAV`/`*RCL`

Already sensors-only after B; ensure energy fields included (C).

### D.5 Exit criteria

| Test | Expected |
|---|---|
| No `SENS1:CAP` then `MEAS1:ENER?` | `0` + `-221` |
| `SENS1:CAP 1e-4` then ENER/POW | NR3; no error |
| `SENS1:SAMP:RATE 1000` + `DUR 3` (if N>2000) | SET `-222` |
| Idle flat V, POW? | ~0, **no** `-231` if N≥2 |
| DUR too short for 2 samples | `0`+`-231` |
| Charge vs discharge | POW sign + / − |
| `MEAS1:VOLT?` | Independent of DUR (fast) |
| FILT EWMA/MOV/MED/OFF | No crash; values change plausibly |
| Compile RAM | Scratch 16 KB visible in BSS; still boots with BLE |

---

## Stage E — Docs + integration test

### E.1 Docs

| File | Action |
|---|---|
| `SCPI_Energize_RX_NICLA.md` | Mark energy/Si5351 removal **implemented**; paste spike RESULTS summary; clear transitional warnings |
| `SCPI_DRIVER_GUIDE.md` | Align with real wire behaviour; remove “design only” where done |
| This plan | Check off stages |

### E.2 Smoke checklist (sign off)

**Hardware matrix:** (1) no Si5351, no INA (2) no Si5351, with INA.

| # | Step | USB | BLE |
|---|---|:---:|:---:|
| 1 | `*IDN?` Energize string | ☐ | ☐ |
| 2 | Boot log: no Si5351 halt; INA OK or WARN | ☐ | — |
| 3 | `MEAS1:VOLT?` | ☐ | ☐ |
| 4 | Unset CAP → ENER/POW `-221` | ☐ | ☐ |
| 5 | Set CAP/RATE/DUR/FILT; `*SAV`; reboot; `*RCL`/boot restore | ☐ | ☐ |
| 6 | `MEAS1:ENER?` / `POW?` block ~DUR | ☐ | ☐ |
| 7 | Charging → POW &gt; 0; discharging → POW &lt; 0 (approx) | ☐ | ☐ |
| 8 | `MEAS3:VOLT?` with/without INA | ☐ | ☐ |
| 9 | `SOURCE1:FREQ 1e6` unsupported | ☐ | ☐ |
| 10 | BLE name `EnergizeRX-…`; `ble` USB-only still works | ☐ | ☐ |

### E.3 Exit criteria

- [x] Design marked implemented; driver guide matches wire API  
- [x] Flash/RAM recorded (~306 KB / 57% flash; ~53 KB / 82% RAM)  
- [ ] Hardware smoke matrix signed off on USB±BLE ±INA (checklist in driver guide §4)

---

## 2. File change map (summary)

| File | A | B | C | D | E |
|---|---|---|---|---|---|
| `spikes/adc-rate-nicla/**` | create | | | | |
| `Si5351Driver.*` | | delete | | | |
| `SCPI_Energize_RX_NICLA.ino` | | major | | minor | |
| `ScpiParser.*` | | remove SOURCE; ctor | | energy SCPI | |
| `Sensors.*` | | Wire comment / fault | state+NVM+API | burst/OLS/filt | |
| `config_nicla.h` | RATE max | IDN; drop SI5351 | energy macros | | |
| `Ble.cpp` | | name prefix | | | |
| `pinDef_nicla.h` | | comments | | | |
| `docs/*` | O3 update | | | | finalise |

---

## 3. Risks and mitigations

| Risk | Mitigation |
|---|---|
| 1000 S/s not sustainable | Stage A; lower default/MAX |
| 16 KB scratch + BLE RAM pressure | Measure after D; if needed reduce `N_cap` in design first |
| Burst blocks BLE/USB for `DUR` | Documented; keep default 0.1 s; host raises consciously |
| OLS denom zero | Return P=0 when variance(t)≈0 |
| `Wire.begin()` after Si5351 removal | Exactly once in `setup()` before `sensors.begin()` |
| Old sensor NVM layout | Version field; invalid → defaults + unset C |

---

## 4. Out of scope (do not implement in v1)

Adaptive duration; always-on ring (L2); residual/σ query; Savitzky–Golay; Clear-C; multi-central BLE; optional Si5351; Watchdog / bus-reset layers.

---

## 5. Suggested execution cadence

1. **Day/session 1:** Stage A spike + doc/config RATE lock.  
2. **Session 2:** Stage B excision + boot smoke (no Si5351).  
3. **Session 3:** Stage C NVM/macros + partial SCPI MIN/MAX.  
4. **Session 4–5:** Stage D burst/OLS/filters + energy SCPI.  
5. **Session 6:** Stage E matrix + doc freeze.

After each stage: compile, flash, short Serial log in chat or RESULTS; update design doc status lines.
