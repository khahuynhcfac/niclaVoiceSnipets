# SCPI Energize RX — Nicla Voice Variant

**Status:** v1 **implemented** (firmware matches this document).  
**Board:** Arduino Nicla Voice (nRF52832, Arduino Mbed OS Nicla Boards)  
**Source of truth:** This document. Parent reference: `SCPI_SI5351_NICLA/docs/SCPI_SI5351_NICLA.md`.

**Build:** ~306 KB flash (57%); ~37 KB static RAM (~57%) — online OLS, no 16 KB scratch (needed for Cordio BLE).

**O3 rate lock:** Provisional `ENERGY_SAMPLE_RATE_MAX_HZ=2000`, default **100** — see `spikes/adc-rate-nicla/RESULTS.md` (bench S/s still pending; re-lock after hardware runs).

---

## 0. Working rules

1. **Essential only.** No extras unless written here first.
2. **Architecture parity** with the Nicla Si5351 port: `IScpiTransport`, dual `ScpiParser`, `SensorManager`, FlashIAP style, error codes, coding conventions.
3. **No speculative robustness.**
4. **Docs stay truthful.** Planned vs implemented must be explicit.
5. **Document first, implement later.** Behaviour changes update this file before code.
6. **Open items** stay open until explicitly agreed (§8).

---

## 1. Overview

Energize RX is a Nicla Voice **SCPI instrument** for a wireless receiver / energy-harvest path. It keeps the working UART + BLE SCPI pipeline; the primary measurement job is **capacitor voltage → stored energy → charge/discharge power**, not clock generation.

| Keep | Change |
|---|---|
| USB-CDC + BLE NUS, dual `ScpiParser` | BLE local name → `EnergizeRX-…` |
| Logical sensors MEAS/FETC/SENS | Ch1 extended for energy/power; INA **optional** |
| `*IDN?`, `*RST`, `*OPC`/`*OPC?`, `*SAV`/`*RCL`, `SYST:ERR?` | New IDN/version strings |
| FlashIAP BLE + sensor sectors | Remove Si5351 sector/driver/`SOURCE`/`OUTPUT` |

### 1.1 Goals (v1)

1. Measure capacitor voltage on **ADC1** (`A0`), 12-bit, scaled by \(S\).
2. \(E = \tfrac12 C V_{\mathrm{s}}^{2}\) with host-set \(C\) (**no default**).
3. \(\hat P = \mathrm{d}E/\mathrm{d}t\) via **OLS** on an in-query sample log over `SAMPle:DURation`.
4. Configurable rate, duration, filters; SCPI style matching parent AVG/APER/MIN/MAX.
5. Keep USB + BLE SCPI usable with or without INA226.

---

## 2. Hardware

### 2.1 Board

| Item | Value |
|---|---|
| MCU | nRF52832 (ANNA-B112) |
| Core | Arduino Mbed OS Nicla Boards |
| Rails | 1.8 V internal; VDDIO_EXT 3.3 V via `nicla::begin()` |

### 2.2 Si5351 — **removed**

| Reason | Detail |
|---|---|
| Product | No generator SCPI/NVM on this receiver |
| Firmware | `Si5351Driver.*`, Etherkit, `SOURCE`/`OUTPUT`, Si5351 halt/recovery **gone**; `Wire.begin()` once in `setup()` |

No optional-Si5351 path in v1.

### 2.3 ADC1 (capacitor sense) — primary

| Item | Value |
|---|---|
| Channel | **1** |
| Pin | `A0` / P0_02 (`PIN_ADC_CH1`) |
| Resolution | **12-bit** (full-scale 4095) |
| Reference | **1.8 V** fixed |
| \(V_{\mathrm{pin}}\) | Code → volts at pin, 0…1.8 V |
| \(V\) | \(S \times V_{\mathrm{pin}}\) |

**Scale \(S\):** `SENSE1:VOLTage:SCALe` (default **1**). Divider that presents \(1/N\) of capacitor voltage → set \(S=N\). Bounds: `1e-3` … `1e6`.

### 2.4 ADC2 — keep

Channel **2** = `A1` / P0_30; same 12-bit / 1.8 V rules. Voltage only.

### 2.5 INA226 — **optional** (never halt)

| Ch | Quantity | Absent INA |
|---|---|---|
| 3 | Load voltage | `0` + `-241`; no halt |
| 4 | Shunt current | same |
| 5 | INA **chip** power register (≠ capacitor \(\hat P\)) | same |

**Boot policy:** probe → WARN if missing; `_inaOk=false`; ADC/energy continue; no fault LED / recovery spam solely because INA was never present. Runtime probe only (no compile-out flag).

### 2.6 I2C

External bus: SDA=4, SCL=3 (INA if fitted). No Si5351.

---

## 3. Software architecture

### 3.1 Target files

| File | Role |
|---|---|
| `SCPI_Energize_RX_NICLA.ino` | `nicla::begin()`; UART+BLE parsers; fault LED; **no Si5351** |
| `IScpiTransport.h` | Unchanged |
| `ScpiParser.*` | Sensors + energy SCPI; **no** `SOURCE`/`OUTPUT` |
| `Sensors.*` | ADC + optional INA + **energy burst/OLS** |
| `Ble.*` | NUS / NVM / `ble …` / LED |
| `config_nicla.h` | IDN, ADC/INA/energy macros |
| `pinDef_nicla.h` | ADC pins |

**Delete when implementing:** `Si5351Driver.h` / `.cpp`.

### 3.2 Data path

```
Host ──USB-CDC──► SerialTransport ──► ScpiParser (uart)
Host ──BLE NUS──► BleTransport    ──► ScpiParser (ble)
                         │
                         ▼
                  SensorManager (shared)
```

Line-oriented SCPI; SET silent; query one line; `SYST:ERR?` queue.

### 3.3 Energy path (inside `SensorManager`)

On `MEAS1:ENER?` / `POW?` only: block for `DURation` at `RATE`, filter each sample, accumulate online OLS sums, then reply (§4.4). **Not** a background `loop()` task.

| Knobs | Used by |
|---|---|
| `AVER:COUN` | `VOLT?`, and each ENER/POW tick (back-to-back analogReads) |
| `APER` | **`MEAS1:VOLT?` only** |
| `SAMP:RATE` / `DUR` / `FILT` | **`MEAS1:ENER?` / `POW?` only** |

### 3.4 Conventions

- `SET_ONLY` for SENSE SETs; persist via `*SAV` / boot.
- NR3 replies (parent Nicla formatter); no `Serial.printf`.
- Errors: `-102` syntax, `-108` bad channel, `-221` conflict (incl. **\(C\) unset**), `-222` out of range / `RATE×DUR` overflow, `-231` &lt;2 samples for power, `-241` hardware, `-350` queue overflow.

---

## 4. SCPI

### 4.1 System

| Command | Behaviour |
|---|---|
| `*IDN?` | `Arduino,EnergizeRX-NiclaVoice,001,0.1.0` |
| `SYST:VERS?` | `V0.1.0` |
| `*OPC` / `*OPC?` | Parent Nicla: `*OPC?` → `1` |
| `*RST` | `NVIC_SystemReset` after flush |
| `*SAV` / `*RCL` [`n`] | Single working set; `n` ignored |
| `SYST:ERR?` | Pop queue |

### 4.2 Removed

`SOURCE…`, `OUTPUT…` — unsupported after excision.

### 4.3 Channel legality

| Ch | VOLT | CURR | POW | ENER |
|---|---|---|---|---|
| 1 | yes | -221 | yes (OLS \(\hat P\)) | yes |
| 2 | yes | -221 | -221 | -221 |
| 3 | yes (INA) | -221 | -221 | -221 |
| 4 | -221 | yes (INA) | -221 | -221 |
| 5 | -221 | -221 | yes (INA reg) | -221 |

`MEASURE` ≡ `FETCH` in v1. Short forms: `MEAS`/`FETC`/`SENS`, `VOLT`/`CURR`/`POW`/`ENER`, `CAP`, `SCAL`, `SAMP:RATE`, `SAMP:DUR`, `FILT`, `WIND`, `ALPH`.

### 4.4 Capacitor energy (normative v1)

#### 4.4.1 Physics

\[
V = S\,V_{\mathrm{pin}},\quad
V_{\mathrm{s}}=\mathrm{Filter}(V),\quad
E=\tfrac12 C V_{\mathrm{s}}^{2}
\]

- \(C\): **no default.** Unset → `CAP?` / `ENER?` / `POW?` reply `0` + **-221**. `VOLT?` OK without \(C\).
- No Clear-C command; unset via boot / `*RCL` of unset image / `*RST`.
- `*SAV` may persist unset (`_capSet` flag).

#### 4.4.2 Query-driven burst

On each `MEAS1:ENER?` / `POW?` (needs \(C\)):

1. Reset filter state for this burst.
2. For duration \(T\), sample at \(f_s\): mean of `AVER:COUN` analogReads (no `APER`) → \(V\) → filter → \(V_{\mathrm{s}}\), \(E\), \(t=\)**`micros()`**.
3. Accumulate online OLS sums and last \(E\) (`N ≤ N_{\mathrm{cap}}`); **no** full sample log in RAM (Nicla + Cordio BLE cannot spare 16 KB BSS).
4. Reply one NR3 (§4.4.2.1). Transports blocked ~\(T\).

| Query | Reply |
|---|---|
| `MEAS1:VOLT?` | Parent **AVG/APER** burst × \(S\) — **not** the energy window |
| `MEAS1:ENER?` | \(E\) from **last** sample in the window |
| `MEAS1:POW?` | OLS \(\hat P=\mathrm{d}E/\mathrm{d}t\) over **all** points in the window (signed: charge \(>0\), discharge \(<0\)) |

`POW?` with &lt;2 samples → `0` + **-231**.  
**Do not** error on small \(\Delta V\) (valid idle zero).  
**Do not** use adaptive duration in v1.  
**Do not** use endpoint-only \(\Delta E/\Delta t\).

#### 4.4.3 OLS (log + math)

Single-pass float sums \(\sum t,\sum E,\sum t^{2},\sum tE\), then one divide. No `double`, no heap in the fit.

| \(N\) | OLS cost (order) | Dominated by |
|---:|---|---|
| ≤2000 | few ms | `analogRead` + filter over \(T\) |

CPU: nRF52832 M4F + FPU — **feasible**.

#### 4.4.4 Memory cap

| Macro | Value |
|---|---|
| `ENERGY_SAMPLE_CAP` (`N_cap`) | **2000** (max samples per burst; **not** a 16 KB BSS log) |

Reject SET if `RATE × DURation > N_cap` → **-222**.  
Examples: 1000 S/s → \(T_{\max}=2\,\mathrm{s}\); 500 S/s → \(T_{\max}=4\,\mathrm{s}\).

v1 fits OLS online (running sums only). A full scratch log was dropped after Cordio HCI failed to allocate (`_stack_buffer != NULL`) at ~82% static RAM.

No persistent always-on ring in v1.

#### 4.4.5 Filters (all four in v1)

| TYPE | Algorithm | Default use |
|---|---|---|
| `OFF` | \(V_{\mathrm{s}}=V\) | **Default** — no smoothing until the host selects a filter |
| `EWMA` | \(\alpha V+(1-\alpha)V_{\mathrm{s}}\) | Host opt-in (`SENS1:FILT EWMA`) |
| `MOV` | Boxcar `WINDow` | FIR SNR |
| `MED` | Median `WINDow` | Impulse reject |

Inapplicable param SET (e.g. `ALPH` while `OFF`) → **-221** (ignored). Queries may still return stored values.

#### 4.4.6 Sense knobs (ch1)

```text
SENS1:CAP <F>|MIN|MAX
SENS1:VOLT:SCAL <S>|MIN|MAX
SENS1:SAMP:RATE <Hz>|MIN|MAX
SENS1:SAMP:DUR <s>|MIN|MAX
SENS1:FILT OFF|EWMA|MOV|MED
SENS1:FILT:WIND <n>|MIN|MAX
SENS1:FILT:ALPH <a>|MIN|MAX
SENS1:AVER:COUN …          ; VOLT? and each ENER/POW tick (no APER on burst)
SENS1:APER …               ; VOLT? only
```

#### 4.4.7 Defaults / bounds

Same MIN/MAX style as parent (`config` macros + helpers + SCPI `MIN`/`MAX`).

| Item | Default | Bounds |
|---|---|---|
| `CAPacitance` | **unset** | `1e-12` … `10` F |
| `VOLTage:SCALe` | **1** | `1e-3` … `1e6` |
| `SAMPle:RATE` | **100** | Min **1**; max **2000** (provisional O3 — §4.4.9) |
| `SAMPle:DURation` | **0.1** s | Min ≥ \(2/f_s\); max so `RATE×DUR≤N_cap` (and ≤ **10** s wall absolute) |
| `FILTer` | **OFF** | enum |
| `FILTer:WINDow` | **10** | 1 … 64 |
| `FILTer:ALPHa` | **0.2** | 0 … 1 |
| `APER` (ch1/ch2) | **0** | 0 … 1 s (`VOLT?` spacing; 0 = back-to-back) |

**Host note:** short `DUR` + slow \(\mathrm{d}V/\mathrm{d}t\) + ADC noise can yield \(\hat P\approx 0\). Raise `DUR` and/or filter; averaging many short zero `POW?` replies is invalid methodology.

#### 4.4.8 Persistence

Version-bump sensor NVM: `_capSet`, \(C\) (if set), \(S\), rate, duration, filter type/window/alpha. Keep magic family with a **version field** (do not silently lengthen old struct).

#### 4.4.9 Spike — ADC rate (O3)

| Item | Status |
|---|---|
| Path | `spikes/adc-rate-nicla/` (README + RESULTS) |
| Setup | 12-bit `analogRead(A0)`; ± EWMA; optional BLE |
| Lock | **Provisional:** MAX **2000**, default **100** until RESULTS table is filled on hardware |

After bench: set MAX ≈ `floor(0.8 × measured_ewma_Sps)`; lower default if that is &lt; 1000.

---

### 4.5 Command table (wire API)

**Q** = one reply line; **S** = no reply.

#### System

| Cmd | | Example |
|---|:---:|---|
| `*IDN?` | Q | → `Arduino,EnergizeRX-NiclaVoice,001,0.1.0` |
| `*OPC?` | Q | → `1` |
| `*OPC` / `*RST` / `*SAV` / `*RCL` | S | |
| `SYST:ERR?` | Q | `0,"No error"` or `code,"msg"` |
| `SYST:VERS?` | Q | → `V0.1.0` |

#### Channel 1

| Cmd | | Notes / example |
|---|:---:|---|
| `MEAS1:VOLT?` | Q | AVG/APER × \(S\) |
| `MEAS1:ENER?` | Q | Blocks `DUR`; last \(E\); needs \(C\) |
| `MEAS1:POW?` | Q | Blocks `DUR`; OLS \(\hat P\); signed |
| `FETC1:…` | Q | = `MEAS1` in v1 |
| `SENS1:CAP` / `?` | S/Q | Unset query → `0`+`-221` |
| `SENS1:VOLT:SCAL` / `?` | S/Q | Default 1 |
| `SENS1:SAMP:RATE` / `?` | S/Q | Default 100 |
| `SENS1:SAMP:DUR` / `?` | S/Q | Default 0.1 |
| `SENS1:FILT` / `WIND` / `ALPH` | S/Q | |
| `SENS1:AVER:COUN` / `APER` | S/Q | `COUN` also used per ENER/POW tick; `APER` is `VOLT?` only |

#### Channels 2–5

| Cmd | Notes |
|---|---|
| `MEAS2:VOLT?` | ADC2 |
| `MEAS3:VOLT?` / `MEAS4:CURR?` / `MEAS5:POW?` | INA; absent → `0`+`-241` |
| `SENS2…5:AVER` / `APER` | Ch2 independent; 3–5 share INA |

#### Example session

```text
*IDN?
SENS1:VOLT:SCAL 1
SENS1:CAP 1e-4
SENS1:SAMP:RATE 100
SENS1:SAMP:DUR 0.1
SENS1:FILT OFF              ; default; host may set EWMA/MOV/MED
*SAV
*OPC?
MEAS1:VOLT?
MEAS1:ENER?
MEAS1:POW?
SYST:ERR?
```

---

## 5. Transport

| Item | Value |
|---|---|
| USB-CDC | 115200; `\n` lines; `ble …` USB-only |
| BLE | HardwareBLESerial NUS; name `EnergizeRX-AABBCC`; one central |
| BLE NVM magic | **Keep** `0x5351B001` (pairing survives rename). Unrelated to multi-board BLE. |

---

## 6. Flash layout (4 KB sectors)

| Sector | Owner |
|---|---|
| Last | Was Si5351 — **unused** after removal (do not repurpose without agreement) |
| 2nd-to-last | Reserved / occupied — do not use |
| 3rd-to-last | BLE `0x5351B001` |
| 4th-to-last | Sensors (versioned energy fields) |

---

## 7. Work breakdown

Detailed tasks, file maps, APIs, and test checklists:  
**[`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md)** (stages A–E).

| Stage | Scope | Status |
|---|---|---|
| **A. Spike** | `spikes/adc-rate-nicla/` | Done (provisional RATE lock; bench pending) |
| **B. Excision** | Remove Si5351; IDN/BLE name; optional INA | **Implemented** |
| **C. Config/NVM** | Energy macros, `N_cap`, versioned NVM | **Implemented** |
| **D. Energy burst** | Filters, scratch log, OLS, SCPI | **Implemented** |
| **E. Guide + test** | Driver guide + smoke matrix | Docs done; hardware smoke ☐ |

---

## 8. Decision register

### 8.1 Agreed (v1 normative)

| ID | Decision |
|---|---|
| — | Remove Si5351; INA optional never-halt; ADC2 keep |
| O1 | Scale \(S\), default 1; bounds `1e-3`…`1e6` |
| O2 | No default \(C\); unset → `-221` on CAP/ENER/POW |
| O4 | Filters OFF/EWMA/MOV/MED; **default OFF** (EWMA only if host sets `SENS1:FILT EWMA`) |
| O5 | Query-driven `DUR` window (not always-on) |
| O6 | `VOLT?` = parent AVG/APER × \(S\) |
| O7 | &lt;2 samples → `-231` |
| O9 | Keep BLE magic |
| O10 | Signed \(\hat P\) |
| O12 | \(C\) bounds `1e-12`…`10` F |
| O13 | 12-bit ADC |
| O14 | `micros()` |
| O15 | Keep ch1 AVER/APER for VOLT?; ENER/POW uses `AVER:COUN` per tick without `APER` |
| O16 | No Clear-C |
| O17 | Default `DUR` = **0.1 s** |
| O18 | **L1** in-query log + OLS; no W3; no adaptive \(T\); no L2 in v1 |
| O18c | `N_cap` = **2000** (sample count limit; online OLS, no 16 KB log) |

### 8.2 Open / gated

| ID | Item | Notes |
|---|---|---|
| O3 | `SAMP:RATE` MAX **2000**, default **100** | Default lowered for 22 mF dV/dt; MAX still provisional |
| O18b | Raise default DUR to 1.0 s | **Declined for now** — keep 0.1; host tunes |

### 8.3 Explicitly out of v1

Adaptive duration; always-on ring log; residual/σ query; Savitzky–Golay; Clear-C; multi-central BLE; optional Si5351.

---

## 9. Firmware tree

`SCPI_Energize_RX_NICLA/` implements this document (no Si5351). Host guide: [`SCPI_DRIVER_GUIDE.md`](SCPI_DRIVER_GUIDE.md).
