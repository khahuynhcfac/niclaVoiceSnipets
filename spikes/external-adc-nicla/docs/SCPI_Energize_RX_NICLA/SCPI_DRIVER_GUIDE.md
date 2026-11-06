# SCPI Communication Guide — SCPI_Energize_RX_NICLA

**Audience:** Host-driver authors.  
**Normative design:** [`SCPI_Energize_RX_NICLA.md`](SCPI_Energize_RX_NICLA.md).  
**Tree:** `SCPI_Energize_RX_NICLA/` (Energize RX v1 firmware).

Wire behaviour below matches the shipped firmware.

---

## 0. Status

| Area | Now |
|---|---|
| USB + BLE SCPI framing | Implemented |
| ADC/INA MEAS/SENS | Implemented (INA optional) |
| Si5351 SOURCE/OUTPUT | **Removed** (`-113` / query `0`) |
| Energy ENER/POW / CAP / SAMP / FILT | **Implemented** (§4.4) |
| IDN `EnergizeRX-…` | `Arduino,EnergizeRX-NiclaVoice,001,0.1.0` |

**Build note:** ~306 KB flash (57%); ~37 KB RAM (57%). Online OLS (no 16 KB sample log). `N_cap=2000` still caps `RATE×DUR`.

---

## 1. Identity

| Item | Value |
|---|---|
| `*IDN?` | `Arduino,EnergizeRX-NiclaVoice,001,0.1.0` |
| `SYSTEM:VERSION?` | `V0.1.0` |
| Primary | ADC1 → \(V\), \(E=\tfrac12 C V_s^2\), \(\hat P=\mathrm{d}E/\mathrm{d}t\) (OLS) |
| INA226 | Optional (ch 3–5); never halt if missing |

Use full `SYSTEM:…` headers (this firmware does not accept short `SYST:ERR?`).

---

## 2. Transports

| | |
|---|---|
| USB-CDC | 115200; `\n`; `ble …` USB-only |
| BLE NUS | Local name `EnergizeRX-AABBCC`; one central |

SET → no reply. Query → one line. Check `SYSTEM:ERROR?` after suspicious `0` replies.

Errors: `-102`, `-108`, `-113`, `-221` (incl. \(C\) unset), `-222`, `-231` (&lt;2 power samples), `-241`, `-350`.

---

## 3. Commands (summary)

Full table: design **§4.5**.

```text
*IDN?
SENS1:VOLT:SCAL 1
SENS1:CAP 1e-4              ; required before ENER/POW
SENS1:SAMP:RATE 100         ; MAX provisional 2000 (spike)
SENS1:SAMP:DUR 0.1          ; blocks ENER/POW for this long
; SENS1:FILT is OFF by default — host may set EWMA/MOV/MED
*SAV
*OPC?

MEAS1:VOLT?                 ; AVG/APER path × scale (not energy window)
MEAS1:ENER?                 ; last E in window (~DUR block)
MEAS1:POW?                  ; OLS dE/dt over window (signed)
SYSTEM:ERROR?
```

- No default \(C\) → CAP?/ENER?/POW? without CAP → `0` + `-221`.
- `POW?` charge \(>0\), discharge \(<0\); &lt;2 samples → `0` + `-231`.
- Raise `DUR` if slow curves look like zero power; do not average many short zero `POW?` as truth.
- `RATE × DUR` must stay ≤ 2000 samples or SET → `-222`.
- No `SOURCE` / `OUTPUT`.
- INA absent: ch3–5 queries → `0` + `-241`; ch1 energy still works.

### Channel map

| Ch | Quantities |
|---|---|
| 1 | `VOLT`, `ENER`, `POW` (+ `CAP` / `SCAL` / `SAMP` / `FILT`) |
| 2 | `VOLT` (ADC2, no scale) |
| 3–5 | INA `VOLT` / `CURR` / `POW` |

---

## 4. VOLT? vs ENER?/POW? — different sampling paths

These are **not** the same measurement pipeline. Do not expect `MEAS1:ENER?` to equal \(\tfrac12 C\,(\mathtt{MEAS1:VOLT?})^2\) sample-for-sample.

| | `MEAS1:VOLT?` | `MEAS1:ENER?` / `MEAS1:POW?` |
|---|---|---|
| Purpose | Quick capacitor / pin voltage | Query-driven energy window |
| Needs \(C\)? | No | **Yes** (`SENS1:CAP`) |
| Timing | Fast (AVG loop only) | Blocks ~`SAMP:DUR` |
| ADC | `AVER:COUN` analogReads spaced by `APER` (default **0** → back-to-back) | `AVER:COUN` analogReads **back-to-back** per `SAMP:RATE` tick (`APER` not applied) |
| Smoothing | **`SENS1:AVER:COUN` + `SENS1:APER`** | Same `AVER:COUN`, then **`SENS1:FILT`** (OFF/EWMA/MOV/MED) |
| Scale \(S\) | `SENS1:VOLT:SCAL` | Same \(S\) on each burst sample |
| Result | \(V = S \times V_{\mathrm{pin,avg}}\) | \(V_s=\mathrm{Filter}(S\cdot V_{\mathrm{pin}})\), \(E=\tfrac12 C V_s^2\); ENER = last \(E\); POW = OLS \(\mathrm{d}E/\mathrm{d}t\) |

Defaults that matter:

| Knob | Default | Used by |
|---|---|---|
| `AVER:COUN` | 16 | `VOLT?`, and each ENER/POW tick |
| `APER` | **0** (no wait between the 16 reads) | `VOLT?` only |
| `SAMP:RATE` | 100 Hz | ENER/POW only |
| `SAMP:DUR` | 0.1 s | ENER/POW only |
| `FILT` | **OFF** | ENER/POW only |
| `FILT:ALPH` | 0.2 (stored) | ENER/POW when EWMA |
| `FILT:WIND` | 10 | ENER/POW when MOV/MED |
| `VOLT:SCAL` | 1 | Both |

---

## 5. Host procedure — energy / average power

Use this sequence when you want trustworthy `MEAS1:ENER?` / `MEAS1:POW?` (OLS “average” power over the window).

### 5.1 Must set (required)

| Step | Command | Why |
|---|---|---|
| 1 | Know hardware \(C\) and divider | Physics input |
| 2 | `SENS1:CAP <F>` | No default \(C\); else ENER/POW → `0` + `-221` |
| 3 | `SENS1:VOLT:SCAL <S>` if divider | Pin sees \(V_{\mathrm{cap}}/N\) → set \(S=N\); else leave 1 |
| 4 | `SENS1:SAMP:RATE <Hz>` | Burst sample rate (default 100; max provisional 2000) |
| 5 | `SENS1:SAMP:DUR <s>` | Window length; query blocks this long; need \(\geq 2/\mathrm{RATE}\) samples for POW |

Constraint: `RATE × DUR ≤ 2000` (and `DUR ≤ 10` s wall). Violation on SET → `-222`.

### 5.2 Should set (recommended before POW?)

| Step | Command | Guidance |
|---|---|---|
| 6 | `SENS1:FILT …` | Default is **OFF**. Set `EWMA`/`MOV`/`MED` only if you want smoothing |
| 7 | Filter params | `ALPH` (EWMA) or `WIND` (MOV/MED) — inapplicable while OFF → `-221` |
| 8 | `*SAV` | Persist working set across reboot (optional but usual) |
| 9 | `*OPC?` | Host sync after SETs (returns `1`) |

### 5.3 Filter choice

| `SENS1:FILT` | When to use | Params |
|---|---|---|
| `OFF` (**default**) | No smoothing; \(V_s=V\). Boot / invalid NVM / `*RCL` of unset-filter image | — |
| `EWMA` | Host opt-in for light smoothing | `SENS1:FILT EWMA` then `SENS1:FILT:ALPH` \(0\ldots1\) (stored default **0.2**). Higher α → closer to raw, more noise. |
| `MOV` | FIR-style SNR when rate is high enough | `SENS1:FILT MOV` then `SENS1:FILT:WIND` \(1\ldots64\) (stored default **10**) |
| `MED` | Impulse / spike rejection | `SENS1:FILT MED` then same `WIND` |

Notes:

- Setting `ALPH` when not EWMA, or `WIND` when not MOV/MED → `-221` (ignored). Queries may still return stored values.
- Filter state **resets at the start of each** ENER/POW burst (no carry across queries).

### 5.4 Query

```text
MEAS1:VOLT?          ; optional: check V (AVG path) — does not use DUR/FILT
MEAS1:ENER?          ; blocks ~DUR; last E in window (J)
MEAS1:POW?           ; blocks ~DUR; OLS P = dE/dt (W), signed
SYSTEM:ERROR?        ; expect 0,"No error"
```

Interpretation:

| `POW?` | Meaning |
|---|---|
| \(>0\) | Energy rising → **charging** |
| \(<0\) | Energy falling → **discharging** |
| ≈0 | Flat / noisy / window too short for the \(\mathrm{d}V/\mathrm{d}t\) |

Do **not** treat many short near-zero `POW?` replies as a valid average of true power — lengthen `DUR` and/or strengthen filtering instead.

### 5.5 Example session (100 µF, ÷1, default OFF)

```text
*IDN?
SENS1:VOLT:SCAL 1
SENS1:CAP 1e-4
SENS1:SAMP:RATE 100
SENS1:SAMP:DUR 0.1
; FILT is OFF unless the host sets it, e.g.:
; SENS1:FILT EWMA
; SENS1:FILT:ALPH 0.2
*SAV
*OPC?

MEAS1:VOLT?
MEAS1:ENER?
MEAS1:POW?
SYSTEM:ERROR?
```

### 5.6 Optional / VOLT?-only knobs

These affect **`MEAS1:VOLT?` aperture**, and `AVER:COUN` is also the number of analogReads in each ENER/POW tick (`APER` still not applied on the burst):

```text
SENS1:AVER:COUN 16
SENS1:APER 0
```

Default `APER 0` already runs the 16 `VOLT?` reads back-to-back. For a single conversion: `AVER:COUN 1`, `APER 0`.

### 5.7 Checklist before trusting POW?

1. CAP set and `SENS1:CAP?` returns your \(C\).
2. Scale matches the divider.
3. `RATE×DUR ≤ 2000` and `DUR` long enough for the physical \(\mathrm{d}V/\mathrm{d}t\).
4. Filter is **OFF** unless you opted in; if noisy, set `SENS1:FILT EWMA` (etc.) for the environment.
5. During charge/discharge, `POW?` sign matches expectation.
6. `SYSTEM:ERROR?` is `0,"No error"` after queries.
7. Remember VOLT? and ENER? paths differ — use ENER/POW for energy power, not \(\Delta(\tfrac12 C V_{\mathtt{VOLT}}^2)/\Delta t\) from sparse VOLT? polls unless you accept the mismatch.

---

## 6. Smoke checklist

Hardware matrix: (1) no Si5351, no INA (2) no Si5351, with INA.

| # | Step | USB | BLE |
|---|---|:---:|:---:|
| 1 | `*IDN?` → Energize string | ☐ | ☐ |
| 2 | Boot: no Si5351 halt; INA OK or WARN | ☐ | — |
| 3 | `MEAS1:VOLT?` | ☐ | ☐ |
| 4 | Unset CAP → ENER/POW `-221` | ☐ | ☐ |
| 5 | Set CAP/RATE/DUR/FILT; `*SAV`; reboot restore | ☐ | ☐ |
| 6 | `MEAS1:ENER?` / `POW?` block ~DUR | ☐ | ☐ |
| 7 | Charging → POW &gt; 0; discharging → POW &lt; 0 | ☐ | ☐ |
| 8 | `MEAS3:VOLT?` with/without INA | ☐ | ☐ |
| 9 | `SOURCE1:FREQ 1e6` → unsupported | ☐ | ☐ |
| 10 | BLE name `EnergizeRX-…`; `ble` USB-only | ☐ | ☐ |

Host helpers: `test_scripts/energize_rx_smoke.py`, `energize_rx_volt_stream.py`, `energize_rx_ener_stream.py`.

---

## 7. Related paths

| Path | Role |
|---|---|
| `SCPI_Energize_RX_NICLA/` | This firmware |
| `SCPI_SI5351_NICLA/` | Parent generator (reference) |
| `spikes/adc-rate-nicla/` | ADC rate spike (O3) |
| `ble-serial-app/` | BLE↔COM helper |
| `docs/IMPLEMENTATION_PLAN.md` | Stages A–E |
| `test_scripts/` | USB smoke / volt / energy stream scripts |
