# SCPI Communication Guide — SCPI_SI5351_NICLA

**Audience:** host-driver authors (human or coding agent).  
**Device:** Arduino Nicla Voice + Adafruit Si5351 (+ optional INA226), firmware under `SCPI_SI5351_NICLA/`.  
**Normative design doc:** [`SCPI_SI5351_NICLA.md`](SCPI_SI5351_NICLA.md) (especially §4–§5).  
**Implementation truth:** `ScpiParser.cpp`, `Sensors.cpp`, `Si5351Driver.cpp`, `config_nicla.h`, `Ble.cpp`.

If this guide and the design doc disagree on *intent*, update the design doc first (§0). If this guide and the **parser** disagree on *wire behaviour*, trust the parser and file a doc fix.

---

## 1. Device identity

| Item | Value |
|---|---|
| `*IDN?` | `Arduino,Si5351-NiclaVoice,001,1.0.0` |
| `SYSTEM:VERSION?` | `V1.0.0` |
| MCU | Nicla Voice (nRF52832, Mbed Arduino core) |
| Generator | Si5351A, 3 clocks → SCPI channels **1…3** (`SOURCE1` = CLK0) |
| Sensors | 5 logical channels → SCPI **1…5** (ADC + INA226) |
| Default drive | 8 mA (`SI5351_DEFAULT_DRIVE`) |

This is **not** the ESP32 WiFi variant. Nicla-only: `*OPC` / `*OPC?`, BLE NUS, sensors, FlashIAP `*SAV`/`*RCL`.

---

## 2. Transports

Two independent SCPI endpoints share one parser state machine (separate `ScpiParser` instances, **shared** Si5351 + sensor hardware/NVM).

### 2.1 USB-CDC Serial

| Item | Value |
|---|---|
| Baud | **115200** (USB-CDC; baud is nominal) |
| Framing | Text lines; terminate commands with **`\n`**. `\r` is ignored. |
| Line buffer | **256** bytes (`ScpiParser::BUF_SIZE`) |
| Boot | Opening the COM port often toggles DTR → MCU reset. Wait **≥2.5 s**, then sync with `*IDN?`. |
| Non-SCPI | Lines starting with `ble` (case-insensitive) are **USB-only** BLE manager commands — **not** SCPI and **not** accepted over BLE. |

### 2.2 BLE Nordic UART (NUS)

| Item | Value |
|---|---|
| Stack | HardwareBLESerial over ArduinoBLE |
| Payload | Same SCPI text as UART (line-oriented), **not** a binary stream |
| Local name | `Si5351-AABBCC` (last 3 bytes of BLE MAC, hex uppercase) |
| Concurrency | One central at a time |
| Host path | e.g. `ble-serial` + com0com, or TCP expose — see repo `ble-serial-app/` |

Pairing / allowlist is configured over USB (`ble pair`, etc.). Once connected, send SCPI the same way as on USB.

**MTU / pacing:** Prefer short lines. Long bursts without waiting for replies or `*OPC?` can overflow host↔device buffers (especially USB-CDC). BLE ATT payloads are small (~20 B default); `ble-serial` reassembles, but do not flood.

---

## 3. Framing and session model (critical for drivers)

### 3.1 Command line

1. Host sends **one SCPI command per line**, UTF-8/ASCII, ending in `\n`.
2. Path is uppercased before match; mixed-case `SOURce1:FREQuency:FIXed` works.
3. Query: first `?` splits path vs optional query parameter (`MIN` / `MAX`).
4. SET: first space splits path vs parameter.

Examples:

```text
*IDN?\n
SOURCE1:FREQUENCY:FIXED 10000000\n
SOURCE1:FREQUENCY:FIXED? MIN\n
SYSTEM:ERROR?\n
```

### 3.2 Replies

| Command type | Reply |
|---|---|
| **Query** (`…?`) | Exactly **one** line: payload + `\n`. |
| **SET** (no `?`) | **No reply.** Never block waiting for one. |

After a SET, either:

- send `*OPC?` and wait for `1`, or
- wait a settle time, then query / `SYSTEM:ERROR?`.

**Nicla recommendation:** `*OPC?` after Si5351 SETs (I2C `set_freq` can take tens–hundreds of ms).

### 3.3 Errors are queued, not in-band

Failures push into an **8-deep** SCPI error queue. They do **not** replace the query reply except where noted (many failed queries still reply `0` so hosts do not hang).

```text
SYSTEM:ERROR?
→ -241,"Hardware error"
SYSTEM:ERROR?
→ 0,"No error"
```

Also accepted: `SYSTEM:ERROR:NEXT?`.

`*CLS` clears the queue (SET, no reply).

### 3.4 Boot / informational noise

USB may emit banners (`=== Si5351…`, `Si5351 OK…`, `INA226 OK`, `UART+BLE SCPI ready.`). Drivers must **drain** or skip non-SCPI lines before treating RX as query replies. Prefer quiet-period drain over blind `reset_input_buffer()` on Windows CDC.

### 3.5 Suggested host I/O API

```text
write_line(cmd)           # append \n, flush; no read
query(cmd) -> str         # write_line; read one non-empty line; skip banners
set_and_opc(cmd)          # write_line(cmd); assert query("*OPC?") == "1"
check_error() -> (code, msg)  # parse SYSTEM:ERROR?
```

Never interleave two queries without consuming the first reply.

---

## 4. IEEE 488.2 common commands

| Command | Type | Reply / effect |
|---|---|---|
| `*IDN?` | Query | `Arduino,Si5351-NiclaVoice,001,1.0.0` |
| `*RST` | SET | MCU soft-reset (`NVIC_SystemReset`). **No reply.** Link drops; reopen and re-sync. **Not** “outputs off only.” |
| `*CLS` | SET | Clear error queue; no reply |
| `*OPC` | SET | Silent no-op (no ESR in this firmware) |
| `*OPC?` | Query | Always `1` (SETs finish I2C before returning to parser) |
| `*SAV` [`<n>`] | SET | Persist Si5351 **and** sensor working set. `<n>` accepted and **ignored**. |
| `*RCL` [`<n>`] | SET | Recall + re-apply that working set. `<n>` ignored. |

`*SAV` / `*RCL` failure → queue `-241,"Hardware error"`.

---

## 5. SYSTEM

| Command | Type | Reply |
|---|---|---|
| `SYSTEM:ERROR?` | Query | `<code>,"<message>"` or `0,"No error"` |
| `SYSTEM:ERROR:NEXT?` | Query | Same |
| `SYSTEM:VERSION?` | Query | `V1.0.0` |

**Header note:** Path must start with `SYSTEM:` after uppercasing. Short form `SYST:` is **not** implemented (unlike some bench instruments). `SYSTem:ERRor?` works because of case folding.

---

## 6. SOURCE{N} / OUTPUT{N} — Si5351

`{N}` ∈ **1…3**. Omit `{N}` → **1**.  
`SOURCE1` → CLK0, `SOURCE2` → CLK1, `SOURCE3` → CLK2.  
Invalid `{N}` → `-108,"Parameter not allowed"` (and no useful query data).

Short headers `SOUR` / `OUTP` are **not** matched — use `SOURCE` / `OUTPUT` (mixed-case OK).

### 6.1 Frequency

| Command | Notes |
|---|---|
| `SOURCE{N}:FREQUENCY:FIXED <f>` | SET Hz. Alias: `…:FREQUENCY <f>` |
| `SOURCE{N}:FREQUENCY:FIXED?` | Query current Hz as `%.1f` (e.g. `10000000.0`) |
| `SOURCE{N}:FREQUENCY:FIXED? MIN` | `8000.0` |
| `SOURCE{N}:FREQUENCY:FIXED? MAX` | `160000000.0` |

**Limits:** 8 000 Hz … 160 000 000 Hz (`SI5351_FREQ_MIN_HZ` / `MAX`). Out of range or I2C failure → `-241` (via driver), not a separate range code from the parser after successful parse.

**Frequency parameter syntax** (`parseFreqHz`):

- Numeric via `strtod` (scientific OK).
- Optional unit suffix after optional spaces: `G`/`GHZ`, `M`/`MHZ`/`MEG`, `K`/`KHZ` (case-insensitive after uppercasing of path only — **param units are uppercased** in `upParam` for OUTPUT ON/OFF; for frequency the parser uppercases the param string used in `parseFreqHz`… actually `cmdFreqFixed` uses `param` from `upParam` — yes uppercased).
- Bad parse → `-102,"Syntax error"`.

Examples:

```text
SOURCE1:FREQUENCY:FIXED 10MHZ
SOURCE2:FREQUENCY:FIXED 1e6
SOURCE1:FREQUENCY:FIXED?
```

### 6.2 Function shape

| Command | Behaviour |
|---|---|
| `SOURCE{N}:FUNCTION:SHAPE?` | Always `SQU` |
| `SOURCE{N}:FUNCTION:SHAPE SQU` | Accepted no-op |
| Other shapes | `-108,"Parameter not allowed"` |

Alias: `SOURCE{N}:FUNCTION` / `FUNCTION?`.

### 6.3 Output enable

| Command | Behaviour |
|---|---|
| `OUTPUT{N}:STATE ON\|1` | Enable |
| `OUTPUT{N}:STATE OFF\|0` (or any non-ON/1) | Disable |
| `OUTPUT{N}:STATE?` | `1` or `0` |
| `OUTPUT{N}` / `OUTPUT{N}:` | Same as `:STATE` |

I2C failure → `-241`.

### 6.4 Stubs (do not use in drivers)

These match headers but are **unsupported** → `-113,"Undefined header"` (queries often also reply `0`):

- `SOURCE{N}:VOLTAGE:…:AMPLITUDE` / `OFFSET` (several long aliases)
- `OUTPUT{N}:IMPEDANCE`
- `SOURCE{N}:PHASE` **SET** → `-113`; **PHASE?** returns `0.0` (read-only stub)
- `COUNTER:…`

---

## 7. Logical sensors — MEASURE / FETCH / SENSE

### 7.1 Channel map

| `{N}` | Meaning | Unit | Legal quantity |
|---|---|---|---|
| 1 | Nicla ADC **A0** pin voltage (0…1.8 V FS) | V | VOLTage only |
| 2 | Nicla ADC **A1** pin voltage | V | VOLTage only |
| 3 | INA226 bus / load voltage (IN−) | V | VOLTage only |
| 4 | INA226 current | A | CURRent only |
| 5 | INA226 power (**chip power register**, not V×I on host) | W | POWer only |

Omit `{N}` → **1**. Outside 1…5 → `-108` (+ query reply `0` for MEAS/FETC/SENS queries).

Illegal quantity for a legal channel → **`-221,"Settings conflict"`** and query replies **`0`**.

### 7.2 Headers and short forms

| Family | Long | Short |
|---|---|---|
| Measure | `MEASURE` | `MEAS` |
| Fetch | `FETCH` | `FETC` |
| Sense | `SENSE` | `SENS` |

**v1:** `MEASURE` ≡ `FETCH` (on-demand read of continuous INA / software-averaged ADC).

Quantity path (optional `SCALAR`/`SCAL`, optional trailing `:DC`):

| Long | Short |
|---|---|
| `VOLTAGE` | `VOLT` |
| `CURRENT` | `CURR` |
| `POWER` | `POW` |

Canonical examples:

```text
MEAS1:VOLT?
MEAS3:VOLTAGE:DC?
FETC4:CURR?
MEAS5:POW?
MEASURE3:SCALAR:VOLTAGE:DC?
```

### 7.3 Measurement reply format (NR3)

Successful measure/fetch replies look like:

```text
3.750000E-03
-5.000000E-05
0.000000E+00
```

Parse as floating point. On hardware failure: queue `-241`, reply still `0`
(integer-looking). On illegal quantity: `-221`, reply `0`.

**Host rule:** a measurement reply of `0` is ambiguous. Always follow with
`SYSTEM:ERROR?` (drain until `0,"No error"`). Firmware detects dead-INA I2C
via a status-aware register read (not the stock `INA226_WE` helper, which
can return a silent `0` on transfer failure).

### 7.4 SENSE — averaging and aperture

```text
SENSE{N}:AVERAGE:COUNT {<NR1>|MIN|MAX|MINIMUM|MAXIMUM}
SENSE{N}:AVERAGE:COUNT? [MIN|MAX|…]
SENSE{N}:APERTURE {<seconds>|MIN|MAX|…}
SENSE{N}:APERTURE? [MIN|MAX|…]
```

Short path forms: `AVER:COUN`, `AVER:COUNT`, `AVERAGE:COUN`, `APER`.

| Channels | AVERAGE:COUNT | APERTURE (seconds on wire) |
|---|---|---|
| 1–2 (ADC) | Software N ∈ **1…1024** | Inter-sample delay **0…1.0** s (`ADC_APER_MAX_US`) |
| 3–4–5 (INA) | On-chip **{1,4,16,64,128,256,512,1024}** only | On-chip CT after `round(sec×1e6)` ∈ **{140,204,332,588,1100,2116,4156,8244}** µs |

**Shared INA knobs:** AVG/APER written on ch 3, 4, or 5 update the **same** INA226 registers; queries on 3/4/5 return that shared value. Ch 1 and 2 are independent.

**Defaults** (no valid sensor NVM): AVG **16**, aperture **0.0011** s (1100 µs).

**Persistence:** Each SENSE SET is **runtime only**. Call `*SAV` to persist; boot loads sensor NVM if magic valid.

Query replies:

- AVG count → decimal integer (`16`)
- Aperture → NR3 seconds (`1.100000E-03`)

Error codes:

| Condition | Code |
|---|---|
| Bad number | `-102,"Syntax error"` |
| Illegal AVG/CT / out of ADC range | `-222,"Data out of range"` |
| I2C / apply failure | `-241,"Hardware error"` |

---

## 8. Error code cheat sheet

| Code | Message (typical) | When |
|---|---|---|
| `0` | `No error` | Empty queue |
| `-102` | `Syntax error` | Bad frequency / AVG / aperture parse |
| `-108` | `Parameter not allowed` | Bad channel; bad FUNCTION shape |
| `-113` | `Undefined header` | Unknown / stub command |
| `-221` | `Settings conflict` | Illegal sensor quantity for channel |
| `-222` | `Data out of range` | Illegal sensor AVG/aperture |
| `-241` | `Hardware error` | I2C / NVM / apply failure |

Always drain with `SYSTEM:ERROR?` in a loop until `0,"No error"` after sequences that might fault.

---

## 9. Recommended driver behaviour

### 9.1 Connect / identify

1. Open port @ 115200 (or attach BLE NUS bridge).
2. Settle (USB reset), drain banners.
3. `*IDN?` → must contain `Si5351-NiclaVoice` (or accept exact string above).
4. Optional: `SYSTEM:VERSION?`.

### 9.2 Set frequency + enable

```text
SOURCE1:FREQUENCY:FIXED 10000000
*OPC?                    → 1
SYSTEM:ERROR?            → 0,"No error"
OUTPUT1:STATE ON
*OPC?
SOURCE1:FREQUENCY:FIXED? → 10000000.0
OUTPUT1:STATE?           → 1
```

### 9.3 Read sensors

```text
MEAS1:VOLT?
MEAS2:VOLT?
MEAS3:VOLT?
MEAS4:CURR?
MEAS5:POW?
```

Optional: after any `-221`/`-241` path, read `SYSTEM:ERROR?`.

### 9.4 Configure then persist sensors

```text
SENS3:AVER:COUN 64
SENS3:APER 0.0011
SENS5:AVER:COUN?         → 64   (shared with ch3)
*SAV
*OPC?
```

### 9.5 Soft reset

```text
*RST
# reconnect, drain, *IDN?
```

Expect NVM-restored Si5351 + sensor settings after reboot if previously `*SAV`’d.

### 9.6 What not to implement

- WiFi / TCP (ESP32-only).
- Amplitude / offset / impedance / real phase control.
- `(@1,2)` channel lists.
- Assuming SET replies.
- Assuming `MEASURE` ≠ `FETCH` (future GATT stream only).
- Sending `ble …` over the BLE SCPI pipe.

---

## 10. Constants (from `config_nicla.h`)

```text
SI5351_FREQ_MIN_HZ      = 8000
SI5351_FREQ_MAX_HZ      = 160000000
SCPI_UART_BAUD          = 115200
SCPI_IDN_STRING         = Arduino,Si5351-NiclaVoice,001,1.0.0
SCPI_VERSION_STRING     = V1.0.0
INA226_DEFAULT_AVG      = 16
INA226_DEFAULT_CONV_US  = 1100
ADC_VREF_V              = 1.8
ADC_AVG_MIN/MAX         = 1 … 1024
ADC_APER_MAX_US         = 1000000
SENSOR_NUM_CHANNELS     = 5
SI5351_NUM_CLOCKS       = 3
```

---

## 11. Reference implementation sketches in-repo

| Asset | Role |
|---|---|
| `test_scripts/scpi_selftest.py` | Si5351 SCPI + `*OPC?` pacing pattern |
| `test_scripts/sensor_selftest.py` | Sensor legality / AVG / `*SAV`+`*RST` |
| `ble-serial-app/scpi_catalog.py` | Host-side command catalogue (mirrors this surface) |
| `ScpiParser.cpp` | Authoritative dispatcher |

---

## 12. Minimal capability matrix (for codegen)

```text
idn                 -> *IDN?
version             -> SYSTEM:VERSION?
error_next          -> SYSTEM:ERROR?
cls                 -> *CLS
opc                 -> *OPC?
rst                 -> *RST                 # reconnect after
sav / rcl           -> *SAV / *RCL

freq_set(ch, hz)    -> SOURCE{ch}:FREQUENCY:FIXED {hz}
freq_get(ch)        -> SOURCE{ch}:FREQUENCY:FIXED?
freq_min/max(ch)    -> …? MIN / …? MAX
output_set(ch, on)  -> OUTPUT{ch}:STATE ON|OFF
output_get(ch)      -> OUTPUT{ch}:STATE?
shape_get(ch)       -> SOURCE{ch}:FUNCTION:SHAPE?

meas_volt(ch)       -> MEAS{ch}:VOLT?       # ch in {1,2,3}
meas_curr(ch)       -> MEAS{ch}:CURR?       # ch == 4
meas_pow(ch)        -> MEAS{ch}:POW?        # ch == 5
sense_avg_set/get   -> SENS{ch}:AVER:COUN …
sense_aper_set/get  -> SENS{ch}:APER …      # seconds
```

Validate channel/quantity in the driver **before** send to avoid routine `-221`, but still handle the wire error queue.

---

*End of guide. Keep aligned with `SCPI_SI5351_NICLA.md` §4–§5 and `ScpiParser.cpp`.*
