# SCPI Si5351 Waveform Generator — Nicla Voice Variant

**Status:** Clean port from ESP32 baseline; logical sensors + `*RST`/`*SAV`/`*RCL` **implemented** (§2.3–2.4, §4.2–4.3, §6.1, §6.3)  
**Board:** Arduino Nicla Voice (nRF52832, Arduino Mbed OS Nicla Boards core)  
**Source of truth:** This document governs all design decisions for the Nicla variant.

---

## 0. Working rules

1. **Essential only.** Implement only what was asked or what is required for the board to work. Do not invent extras (interactive holds, convenience flags, flow-control commands, recovery layers, “nice to have” UX) unless the user explicitly requests them.
2. **ESP32 parity first.** Prefer copying ESP32 behaviour. Nicla-only changes are limited to hardware/core differences (I2C pins, `nicla::begin()`, no WiFi/`Preferences`, Mbed `Serial` without `printf`).
3. **No speculative robustness.** Do not add Watchdog, I2C bus-reset layers, absolute OE writes, etc. unless agreed and documented here first. (`*OPC` / `*OPC?` and FlashIAP persistence are present by explicit request — see §4 and §6.)
4. **Docs stay truthful.** Update this file when behaviour changes; do not document features that are not in the code.
5. **Document first, implement later.** For BLE and any other new feature: agree and write the behaviour in this document first; implement only what is documented. If code must differ from the document, update the document and get human agreement **before** changing the code. No silent document edits — every doc change is reviewed and agreed by the human developer.
6. **No unagreed assumptions.** The AI agent may suggest technical points that should be considered, but must not treat anything as decided unless the human developer has explicitly agreed it or already specified it in this document. Open items stay marked as open until agreed.

---

## 1. Overview

This variant is a **strict port** of the ESP32-S3 Si5351 SCPI waveform generator. The Si5351 driver setters/read-back, SCPI parser, and command set match the ESP32 originals. Only Nicla-specific board adaptations are applied.

| ESP32 | Nicla adaptation |
|---|---|
| `config.h` | `config_nicla.h` (no WiFi/EEPROM macros; Nicla `*IDN?`) |
| `pinDef.h` | `pinDef_nicla.h` (SDA=4, SCL=3) |
| `Wire.begin(PIN_SDA, PIN_SCL)` | Core defaults; `Wire.begin()` left to `Si5351::init()` (see §3.3) |
| `Preferences` / NVS | FlashIAP persistence in the driver (§6); SCPI still uses `SET_ONLY` |
| WiFi TCP + UART | UART / USB-CDC + BLE NUS (replaces WiFi; §5.2) |
| `Serial.printf` | `snprintf` + `Serial.print` (Mbed UART) |
| No `*OPC` / `*OPC?` | **Nicla-only:** IEEE 488.2 `*OPC` / `*OPC?` (§4.1) |
| `*RST` disables outputs only | **Both platforms:** MCU soft-reset (§4.2) — implemented |
| — | `nicla::begin()` so VDDIO_EXT is 3.3 V |
| — | INA226 + Nicla ADC logical sensors (§2.3, §4.3); `*SAV` / `*RCL` (§4.2) — implemented |

I2C write verification, bounded recovery, FlashIAP, `*OPC` / `*OPC?`, logical
sensors, `*RST` soft-reset, and `*SAV` / `*RCL` are present by explicit request
— see §3.3, §4, and §6. For the full I2C/EMI engineering analysis and
best-practice ladder (reference only), see §3.4.

---

## 2. Hardware Setup

### 2.1 Board

| Item | Value |
|---|---|
| MCU | nRF52832 (ANNA-B112 module) |
| Core | Arduino Mbed OS Nicla Boards |
| Operating voltage | 1.8 V (internal); VDDIO_EXT set to 3.3 V via `nicla::begin()` |

### 2.2 I2C wiring

Do **not** remap I2C pins. Use Nicla defaults:

| Si5351 breakout pin | Nicla Voice header pin | Nicla MCU pin |
|---|---|---|
| SCL | `3` (I2C_SCL) | P0_23 |
| SDA | `4` (I2C_SDA) | P0_22 |

Also on the **ESLOV** connector (SCL=3, SDA=4).

> **Note:** Power supply wiring for the Si5351 breakout is not documented here — to be added once confirmed.

### 2.3 INA226 power monitor (implemented)

High-side current sense on the external I2C bus (same `Wire` as Si5351: SDA=4, SCL=3).
Owned by `SensorManager` (`Sensors.h` / `Sensors.cpp`). Stage 0 spike confirmed
address `0x40` and Si5351 coexistence (`spikes/ina226/RESULTS.md`).

| Item | Value |
|---|---|
| Library | **INA226_WE** |
| I2C address | `0x40` (`INA226_I2C_ADDR` in `config_nicla.h`) |
| Shunt | **0.1 Ω** (R100, 1%), high-side (`INA226_SHUNT_OHMS`) |
| VBUS pin | Wired to **load positive = IN−** (load voltage, not supply rail). **VBUS is a separate chip pin** from IN+/IN− — many breakouts need an explicit jumper from VBUS→IN− / load+. Shared GND alone is not enough; without VBUS tied to the rail, `MEAS3:VOLT?` stays ~0 while current still works. |
| Typical rail / load | ~**7.5 V**, ~**60–100 mA** → load ≈ 7.49 V at 100 mA |
| Shunt full-scale | ±81.92 mV → **I_FS ≈ ±819.2 mA** |
| Calibration `I_max` | **0.8192 A** (`INA226_I_MAX_A`) — matches shunt FS; `Current_LSB` = 25 µA |
| Over-range | Currents above FS may saturate; those readings need not be accurate |
| Power | Read **INA226 Power register** (library mW → SCPI W) |

Default on-chip conversion if NVM invalid: **AVERAGE_16**, **CONV_TIME_1100** (1.1 ms) — macros `INA226_DEFAULT_AVG`, `INA226_DEFAULT_CONV_US`.
Missing INA226 at boot logs a warning and does **not** halt; ADC channels remain available.

### 2.4 Nicla built-in ADC channels (implemented)

Two logical voltage sensors use the Nicla built-in ADC (channels **1** and **2** in §4.3).

| Item | Value |
|---|---|
| ch1 pin | `A0` / `P0_02` (`PIN_ADC_CH1`) |
| ch2 pin | `A1` / `P0_30` (`PIN_ADC_CH2`) |
| Resolution | **12-bit** (`analogReadResolution(12)`, full-scale 4095) |
| Reference | Fixed **1.8 V** (`ADC_VREF_V`) — inputs tolerate 3.3 V but saturate above 1.8 V |
| Reported value | Voltage **at the pin**, 0–1.8 V (no external-divider scaling) |
| Software AVG | `1…1024` samples (`ADC_AVG_MIN` / `ADC_AVG_MAX`) |
| Software APER | Inter-sample delay `0…1 s` (`ADC_APER_MAX_US`) |

One `MEASURE`/`FETCH` acquisition blocks for about `N × aperture` (default 16 × 1.1 ms ≈ 17.6 ms).

---

## 3. Software Architecture

### 3.1 Copied from ESP32

| File | Role |
|---|---|
| `IScpiTransport.h` | Abstract transport interface |
| `ScpiParser.h` / `.cpp` | SCPI line parser and command dispatcher — same as ESP32 plus Nicla-only `*OPC` / `*OPC?` (§4) |

### 3.2 Adapted from ESP32

| File | Nicla-only changes |
|---|---|
| `config_nicla.h` | Drop WiFi/EEPROM; Nicla IDN/version strings; same Si5351 constants and **8 mA** default drive as ESP32; INA226/ADC default macros (§2.3, §4.3) when implemented |
| `pinDef_nicla.h` | SDA=4, SCL=3; no `PIN_SCPI_STATUS`; ADC pins TBD |
| `Si5351Driver.h` / `.cpp` | No `Preferences`; FlashIAP persistence; setters verify by register read-back (§3.3) |
| `SCPI_SI5351_NICLA.ino` | `nicla::begin()`; UART + BLE dual `ScpiParser`; `ble …` USB router; LED resolver (§3.3, §5.2) |
| `Ble.h` / `Ble.cpp` | BLE NVM, `BleTransport` (`IScpiTransport`), `BleManager` (radio, allowlist, UART `ble` cmds, LED) — isolated from Si5351 driver |
| `Sensors.h` / `Sensors.cpp` | `SensorManager`: INA226_WE + Nicla ADC; fourth-to-last FlashIAP (§6.1); SCPI via §4.3 |

For I2C failure modes, platform limits, and the recommended recovery ladder
(not all implemented), see **§3.4**.

---

### 3.3 I2C write verification and fault handling

Every setter confirms **only the field it just wrote** by register read-back:

| Setter | Register checked |
|---|---|
| `enableOutput` | `3` — that clock's OE bit (SET = disabled) |
| `setDriveStrength` | `16`–`18` — drive bits `[1:0]` |
| `setFrequency` | `16`–`18` — MultiSynth source bits `[3:2]` |
| `setCorrection` | `16` — MultiSynth source present (aliveness only; correction only changes the PLL divider maths, so there is nothing to compare) |

A full-chip compare (`_verifyChip`) is used only by `tryRecover()` after
re-applying every cached setting.

**Why not a global compare after every SET?** Etherkit `reset()` leaves CLK
ctrl = `0x0c` (2 mA) while our cache defaults to 8 mA, and `set_freq()`
auto-enables the output on first use without going through `enableOutput`.
A global OE+drive compare therefore false-fails on a healthy bus.  After
`init()`, `begin()` always pushes the cached drive strengths so the chip
matches before any SCPI command runs.

`setFrequency` also resyncs the OE cache from register `3`, but only on the
first successful set for a clock (detected by the cached frequency still
being `0`), which is the only call where Etherkit auto-enables.

On a failed read or a mismatch the cache entry is rolled back, the setter
returns `false`, `ScpiParser` queues `-241,"Hardware error"`, and the fault
latches.

**Reads must report transfer failure, not just data.** Etherkit's
`si5351_read()` discards the `endTransmission()` status and the
`requestFrom()` count, returning `0x00` both for a genuine zero and for a
failed transfer. `0x00` is a legal value for two of the fields checked
above — reg `3` decodes as *all outputs enabled*, and the drive bits as
2 mA — so a dead bus would have passed verification whenever the cache
happened to hold those states. The driver therefore reads through its own
`_readReg()`, which returns the transfer status separately from the byte.
`setFrequency`'s OE-cache resync uses it for the same reason.

`_readReg()` drains any leftover `Wire` RX bytes first (Mbed `requestFrom`
appends without clearing — a stale byte would otherwise be returned as the
register value and false-fail verification), prefers a repeated-START
transaction, and falls back to Etherkit's STOP-then-START pattern on NACK.

**While latched, every setter returns `false` immediately without touching
the bus.** A wedged TWI blocks each transaction until the mbed timeout, so
letting `set_freq()` issue its ~10 writes per command starves `loop()` and
overflows the Serial RX buffer — the board stops answering SCPI at all.
Only `tryRecover()` may drive the bus in that state.

**Recovery** re-applies the cached settings and re-verifies. It deliberately
does *not* reset the bus or the TWI peripheral — it only re-drives the chip
over a bus assumed to be intact. `Wire.begin()` is avoided (directly, and via
`Si5351::init()` which calls it internally) because `mbed::I2C` has an empty
destructor and `i2c_free()` is an unimplemented weak symbol, so `Wire.end()`
frees only the C++ wrapper: it leaks a ~170-byte instance per call and leaves
TWI0 enabled and still owning SDA/SCL.

Retries are capped at `RECOVERY_MAX_TRIES` (3, every 2 s); after that the
fault is latched, the board prints `FAIL: Si5351 unrecoverable — press
reset.` and stops retrying so UART stays responsive.

**This is a scope limit, not a hardware limit.** An earlier revision of this
document claimed a wedged TWI could only be cleared by a board reset. That is
incorrect; both remedies exist on this core and are simply not implemented:

- **Slave stuck holding SDA low.** `mbed::I2C`'s constructor already calls
  `I2C::recover()`, the I2C-spec bus-clear procedure (nine SCL pulses plus a
  manual STOP), immediately before `i2c_init()` — confirmed by disassembling
  `I2C.o` in the prebuilt `libmbed.a`. It is ineffective today only because
  TWI0 is never disabled, so the peripheral retains pin ownership and the
  GPIO pulses never reach the wire. Disabling TWI0 first would fix that.
- **Master peripheral wedged.** `recover()` does not address this. The nRF52832
  uses the legacy TWI block (`I2C_0 = NRF_TWI0_BASE` in `PeripheralNames.h`),
  reachable from sketch code for an `ENABLE`-register reset or power-cycle.

A third rung also exists: the PMIC is on `Wire1` (P0.15/P0.16), physically
separate from the external bus on `Wire` (P0.22/P0.23), so a wedged external
bus cannot block `nicla::disableLDO()` / `enable3V3LDO()`. If the Si5351 is
fed from VDDIO_EXT this is a true power-on-reset of the slave.

None of this is implemented; it is recorded so the constraint is not
re-derived incorrectly. Per §0 rule 3, adding any of it requires agreement
and a documented decision here first.

| RGB LED | Meaning |
|---|---|
| Off | Normal operation |
| Red | Si5351 not found at boot; or verification failed (recovery in progress or exhausted) |

Missing Si5351 at boot logs `FAIL`, latches `_faulted` (red LED via §5.2.6), and does **not** halt; UART, BLE, and sensors still start.

### 3.4 I2C / EMI — failure modes and best-practice reference

This section records the engineering analysis for noisy or failing I2C on this
board. It is **reference material** for future work — not a specification of
what the firmware does today. Current behaviour is §3.3 only. Per §0 rule 3,
implementing anything listed here as "not implemented" requires agreement and
an update to §3.3 first.

#### 3.4.1 Why I2C is fragile under EMI

I2C has **no error detection** at the protocol level — no CRC, no parity, no
length field. The only integrity signal is the per-byte ACK. That produces
three distinct failure classes; good design treats them differently:

| Class | What happens | Typical cause | Detectable by |
|---|---|---|---|
| **A — silent corruption** | A glitch flips a data bit; the slave ACKs normally and a wrong value is written (e.g. a wrong MultiSynth divider → wrong output frequency) | Glitch during a data byte | **Read-back only** — transport status looks fine |
| **B — transport error** | Address or ACK slot corrupted; master sees NACK (`endTransmission() != 0`) or zero bytes from `requestFrom()` | Glitch on address/ACK | Cheap — check Wire return codes |
| **C — lockup** | Master and slave disagree on bit count; slave holds **SDA low** waiting for clocks, or the nRF52 TWI peripheral hangs in its state machine | Phantom SCL edge, interrupted transaction | Bus stuck / timeouts / failed reads |

Class A is why register read-back after every SET is the right foundation.
Class B is why every read must report **transfer failure separately from the
data byte** (see `_readReg()` in §3.3). Class C is why a single retry strategy
is not enough — slave-stuck and master-wedged need different remedies.

#### 3.4.2 Recommended layered approach (best practice)

Apply remedies from cheapest to most invasive. Each layer should be **bounded**
in time and attempts and must not block `loop()` for long enough to overflow
the Serial RX buffer.

| Layer | Remedy | Purpose | Status on Nicla |
|---|---|---|---|
| **0 — Physical** | Stronger pull-ups (2.2–4.7 kΩ vs Adafruit's ~10 kΩ), short runs, ground return adjacent to SDA/SCL, 22–100 Ω series damping at driver ends, keep I2C at 100 kHz, route Si5351 clock outputs away from SDA/SCL | Reduce glitch rate at source | Hardware — not in firmware |
| **1 — Transport status** | Check `endTransmission()` and `requestFrom()` count on every access; never treat `0x00` from a failed read as data | Fix Class B; prevent false "healthy" reads | **Implemented** — `_readReg()` (§3.3) |
| **2 — Transaction retry** | On transport failure only, retry the same register access 2–3 times with short backoff *before* latching `_faulted` | Turn most Class B (and some Class A) glitches into non-events | **Not implemented** |
| **3 — Read-back verify** | After each SET, read the field just written and compare to cache; roll back cache on mismatch | Catch Class A silent corruption | **Implemented** — per-field verify (§3.3) |
| **4 — Fail-fast while latched** | While `_faulted`, setters return immediately without touching the bus | Keep UART/SCPI responsive when TWI would block for hundreds of ms per `set_freq()` | **Implemented** (§3.3) |
| **5 — Chip re-drive** | Re-apply cached settings + full verify (`tryRecover()`) | Recover when bus is intact but chip state drifted | **Implemented**, bounded 3× every 2 s (§3.3) |
| **6 — Bus clear (9 clocks + STOP)** | Disable TWI0, run I2C-spec bus recovery, re-init | Fix Class C slave-stuck (SDA held low) | **Not implemented** — see §3.4.3 |
| **7 — Peripheral reset** | `NRF_TWI0->ENABLE = 0`, then re-init | Fix Class C master-wedged | **Not implemented** — see §3.4.3 |
| **8 — Slave power-cycle** | `nicla::disableLDO()` / `enable3V3LDO()` via PMIC on `Wire1` | True POR of Si5351; only works if breakout is fed from VDDIO_EXT | **Not implemented** — see §3.4.3 |
| **9 — System reset** | Board reset button / watchdog | Last resort when all else fails | User action after `FAIL: … press reset.` |
| **10 — Instrument semantics** | SCPI status register (Questionable Data) when cache ≠ verified chip; optional error counters | Host can distinguish "last known" from "verified" | **Not implemented** |

**Escalation order when adding recovery:** 2 → 6 → 7 → 8 → 9. Do not skip
to `Wire.begin()` without disabling TWI0 first (§3.4.3).

**Fail-safe output policy (design decision, not yet made):** When the bus is
dead, the Si5351 keeps driving its last configuration. Turning outputs off
over I2C is impossible in that state; an LDO cut (layer 8) is the only
software path to force outputs quiet. Decide explicitly whether "keep last
frequency" or "go silent" is acceptable for the application.

#### 3.4.3 Platform facts (Nicla Voice / nRF52832 / Mbed)

These were verified against the shipped core and prebuilt `libmbed.a`; do not
re-derive from assumptions.

**Two independent I2C buses**

| Bus | Pins | Used for |
|---|---|---|
| `Wire` | P0_22 (SDA), P0_23 (SCL) — Arduino 4, 3 | External Si5351 |
| `Wire1` | P0_15 (SDA), P0_16 (SCL) | On-board PMIC (BQ25120A) |

A wedged external bus on `Wire` does **not** block PMIC access on `Wire1`.

**Etherkit library limitations**

- `si5351_write()` / `si5351_write_bulk()` return `endTransmission()` status,
  but callers (`set_freq`, `output_enable`, etc.) **ignore** non-zero returns.
- `si5351_read()` discards both `endTransmission()` and the `requestFrom()`
  count; on failure it returns `0x00`, which is indistinguishable from a
  genuine zero for some registers. Do not use it for verification — use
  `_readReg()` or an equivalent wrapper.
- `Si5351::init()` calls `Wire.begin()` internally and returns `true` even on
  a dead bus (its `SYS_INIT` wait loop reads `0` when the bus is down and
  exits immediately).

**Mbed `Wire` / `mbed::I2C` behaviour**

- Default I2C clock: **100 kHz** (never changed in this project).
- `Wire.end()` deletes the C++ `mbed::I2C` object only. The destructor is
  empty; Nordic's `i2c_free()` is an unimplemented weak symbol. **TWI0 stays
  enabled** and keeps ownership of SDA/SCL.
- Each `Wire.begin()` allocates a new `mbed::I2C` (~170 bytes) and leaks the
  old one. Bounded use is acceptable; unbounded retry loops are not.
- **`mbed::I2C`'s constructor already calls `I2C::recover()`** (nine SCL
  pulses + manual STOP per I2C spec) immediately before `i2c_init()` —
  confirmed by disassembling `I2C.o` in `variants/NICLA/libs/libmbed.a`.
- That recovery is **ineffective today** if TWI0 was never disabled: the
  peripheral retains pin ownership and GPIO pulses from `recover()` do not
  reach the wire. **Correct sequence for bus clear:** `NRF_TWI0->ENABLE = 0`
  (or equivalent) → `Wire.end()` → `Wire.begin()` (which runs `recover()` +
  `i2c_init()`).
- nRF52832 uses the legacy **TWI** block, not TWIM (`I2C_0 = NRF_TWI0_BASE`
  in `PeripheralNames.h`). TWI0 base address: `0x40003000`.

**Verification false-negative traps (fixed in §3.3)**

| Field checked | Dead-bus read | Decodes as | False pass when cache holds |
|---|---|---|---|
| OE reg `3` | `0x00` | All outputs **enabled** | `_enabled[i] == true` |
| Drive bits `[1:0]` | `0x00` | 2 mA | `_drive[i] == SI5351_DRIVE_2MA` |
| MultiSynth bits `[3:2]` | `0x00` | Not MultiSynth | Never — `0x00 ≠ 0b11` |

The MultiSynth check was accidentally safe; OE and drive were not. Any future
verify logic must ensure the failure signature cannot equal a legal expected
value, or must check transport status first.

#### 3.4.4 Physical layer notes (layer 0)

Highest return per effort for EMI on this setup:

1. **Pull-ups:** Adafruit Si5351 breakout ships ~10 kΩ. Paralleling to
   2.2–4.7 kΩ cuts rise time and stiffens lines against injected charge.
   Floor ≈ `(3.3 V − 0.4 V) / 3 mA ≈ 1 kΩ`.
2. **Self-EMI:** Si5351 outputs at up to 160 MHz with 8 mA drive are a strong
   aggressor near the I2C run — route clock cables away from SDA/SCL before
   blaming external EMI.
3. **Long or hostile runs:** Rise-time accelerator (e.g. LTC4311) or
   differential/isolated repeater (e.g. PCA9615, ADuM1250) — firmware cannot
   substitute for marginal signal integrity.

#### 3.4.5 What the host sees today vs ideal

| Situation | Today | Ideal (not implemented) |
|---|---|---|
| Transient glitch on one SET | Fault latch → `-241` → up to 3× `tryRecover()` | Layer 2 retry absorbs glitch; no latch |
| Dead bus during SET | `-241`, fault latch, red LED | Same, but after retries + escalation |
| Dead bus during query (`FREQ?`) | Answers from **RAM cache** — may not match chip | Status bit: "Questionable Data" |
| Recovery exhausted | `FAIL: Si5351 unrecoverable — press reset.`; UART still works | Same; optionally suggest bus/peripheral/LDO steps tried |

Queries intentionally use cache so SCPI stays responsive during a fault.
Callers cannot tell "verified live" from "last known" without a status
mechanism (layer 10).

#### 3.4.6 Checklist before changing I2C fault handling

1. Does the change respect §0 (essential only, no speculative layers)?
2. If adding recovery: which layer (§3.4.2)? Document in §3.3.
3. If calling `Wire.begin()` in a loop: disable TWI0 first; cap attempts.
4. If adding verify: can `0x00` alias onto success? Use `_readReg()` pattern.
5. If changing fail-safe: document output behaviour when bus is dead.
6. Re-run `arduino-cli compile --fqbn arduino:mbed_nicla:nicla_voice`.

---

## 4. SCPI Command Set

Same Si5351 command set as the ESP32 variant, plus Nicla-only `*OPC` / `*OPC?`.
Channel style matches the existing parser: **header suffix `{N}`**
(`SOURCE{N}:…`, `OUTPUT{N}:…`), not `(@n)` lists. Omit `{N}` → channel **1**.

**Host-driver authors:** see [`SCPI_DRIVER_GUIDE.md`](SCPI_DRIVER_GUIDE.md) for
framing, transports, error queue, sensor legality, and a codegen-oriented
command matrix (derived from this section + `ScpiParser.cpp`).

| Command | Description | Status |
|---|---|---|
| `*IDN?` | Identification string | Implemented |
| `*RST` | **MCU soft-reset** (both Nicla and ESP32) — see §4.2 | Implemented |
| `*CLS` | Clear error queue | Implemented |
| `*OPC` | Operation Complete (command form) — silent no-op; see §4.1 | Implemented (Nicla) |
| `*OPC?` | Operation Complete query — replies `1` when prior work is done | Implemented (Nicla) |
| `*SAV` [`<n>`] | Persist single working set — see §4.2 | Implemented |
| `*RCL` [`<n>`] | Restore single working set — see §4.2 | Implemented |
| `SOURCE{N}:FREQUENCY:FIXED {f}` | Set frequency | Implemented |
| `SOURCE{N}:FREQUENCY:FIXED?` | Query frequency | Implemented |
| `OUTPUT{N}:STATE {ON\|OFF\|1\|0}` | Enable/disable output | Implemented |
| `OUTPUT{N}:STATE?` | Query output enable | Implemented |
| `SOURCE{N}:FUNCTION:SHAPE?` | Always `SQU` | Implemented |
| `SYSTEM:ERROR?` | Dequeue one error | Implemented |
| `SYSTEM:VERSION?` | Firmware version | Implemented |
| `MEASURE{N}:…` / `FETCH{N}:…` / `SENSE{N}:…` | Logical sensors — see §4.3 | Implemented |

`{N}` for `SOURCE` / `OUTPUT` is 1-based: `SOURCE1` → CLK0, … `SOURCE3` → CLK2.

SET commands produce **no reply** (standard SCPI). Prefer `*OPC?` after a SET
when the host needs a completion handshake; otherwise settle briefly before
querying.

### 4.1 `*OPC` / `*OPC?` (Nicla only)

IEEE 488.2 Operation Complete. All SET handlers finish their I2C work before
returning to the parser, so there is never a pending operation when these
commands run:

| Form | Behaviour |
|---|---|
| `*OPC?` | Always replies `1` |
| `*OPC` | No reply (silent). There is no Event Status Register in this firmware, so the command form does not set ESR bit 0 |

Hardware failures during a SET still queue `-241,"Hardware error"`; they do
not change the `*OPC?` reply. Use `SYSTem:ERRor?` to read faults.

The ESP32 parser does **not** implement these commands.

Default drive strength is **8 mA** (`SI5351_DEFAULT_DRIVE`), matching ESP32 `config.h`.

### 4.2 `*RST`, `*SAV`, `*RCL` (implemented)

#### `*RST` — MCU soft-reset (Nicla **and** ESP32)

`*RST` performs a **firmware soft-reset** of the MCU (reboot so `setup()`
runs again). It does **not** mean “disable Si5351 outputs only.”

| Platform | Mechanism |
|---|---|
| Nicla (Mbed) | `NVIC_SystemReset()` after transport flush |
| ESP32 | `ESP.restart()` after transport flush |

After reset, FlashIAP / NVS load runs as on power-up.

#### `*SAV` / `*RCL` — single working set

| Command | Behaviour |
|---|---|
| `*SAV` [`<n>`] | Persist **all** storeable runtime state: `Si5351Driver::storeAll()` + `SensorManager::storeAll()`. **Single working set** — if `<n>` is present it is **accepted and ignored**. |
| `*RCL` [`<n>`] | Reload that working set from NVM into runtime and re-apply to hardware (`recallAll()` on both). `<n>` ignored if present. |

Individual `SENSE{N}:…` SETs update **runtime only** (`SET_ONLY`). Persistence of sensor params is via `*SAV` (or boot load from NVM).

### 4.3 Logical sensors (implemented)

**Status:** Implemented in `SensorManager` + `ScpiParser`. IEEE command *names*;
channel spelling matches existing `SOURCE{N}` style (long and short forms:
`MEASURE`/`MEAS`, `FETCH`/`FETC`, `SENSE`/`SENS`). Not dual `(@n)` dialect.

#### 4.3.1 Model

Each **field** is one **logical sensor** (one quantity, one unit).  
Five channels:

| `{N}` | Logical sensor | Quantity | Unit | Backend |
|---|---|---|---|---|
| 1 | Nicla built-in ADC channel 1 | Voltage | V | `A0` / P0_02, 12-bit, 1.8 V ref |
| 2 | Nicla built-in ADC channel 2 | Voltage | V | `A1` / P0_30, 12-bit, 1.8 V ref |
| 3 | INA226 load voltage (IN− / VBUS) | Voltage | V | INA226 bus-voltage register |
| 4 | INA226 shunt current | Current | A | INA226 current register |
| 5 | INA226 power | Power | W | **INA226 power register** |

**Same command families** for all channels; the channel decides whether a
family is legal. Illegal combination → queue **-221,"Settings conflict"**
(no silent ignore).

| Channel | `VOLTage` | `CURRent` | `POWer` | `AVERage` / `APERture` |
|---|---|---|---|---|
| 1 | yes | no → -221 | no → -221 | yes (software) |
| 2 | yes | no → -221 | no → -221 | yes (software) |
| 3 | yes | no → -221 | no → -221 | yes (INA on-chip; **shared** with 4 & 5) |
| 4 | no → -221 | yes | no → -221 | yes (INA on-chip; **shared** with 3 & 5) |
| 5 | no → -221 | no → -221 | yes | yes (INA on-chip; **shared** with 3 & 4) |

Omit `{N}` → **1**. Channel outside 1–5 → **-108,"Parameter not allowed"**.

Measurement replies are NR3 (scientific, six fraction digits). On Nicla,
`snprintf("%.6E")` is unavailable in newlib-nano, so the parser builds the
same form via `%f` + exponent. Hardware / I2C read failure →
**-241,"Hardware error"** and the query still replies `0` (blocking hosts).
Illegal AVG or aperture → **-222,"Data out of range"**; malformed number →
**-102,"Syntax error"**.

**INA reads must not trust `INA226_WE::readRegister()`.** That helper returns
`0x0000` for both a real zero and a failed transfer, and often leaves
`getI2cErrorCode()==0` when `requestFrom` yields no bytes on this Mbed core —
so an unplugged INA226 looked like a successful `MEAS3:VOLT? → 0`.
`SensorManager` therefore reads INA registers through its own `_readInaReg()`
(same transfer-status pattern as `Si5351Driver::_readReg()` in §3.3) and
latches `_faulted` on failure before the parser queues `-241`. Hosts must
still call `SYSTEM:ERROR?` after a `0` measurement reply to distinguish a
true zero from a hardware fault.

Aperture on the wire is in **seconds**. INA channels accept only the eight
legal conversion times (140, 204, 332, 588, 1100, 2116, 4156, 8244 µs) after
`round(sec×1e6)`; ADC channels accept `0…ADC_APER_MAX_US` (1 s).

#### 4.3.2 Measure / fetch

```text
MEASURE{N}:VOLTAGE:DC?
MEASURE{N}:CURRENT:DC?
MEASURE{N}:POWER:DC?
FETCH{N}:VOLTAGE:DC?
FETCH{N}:CURRENT:DC?
FETCH{N}:POWER:DC?
```

Short forms allowed by the usual SCPI abbreviation rules
(e.g. `MEAS3:VOLT?`, `FETC4:CURR?`, `MEAS5:POW?`).

| Phase | `MEASURE` vs `FETCH` |
|---|---|
| **v1 (now)** | **Identical:** continuous conversion (INA) / configured ADC path; SCPI read on demand only; both return the latest appropriate value |
| **Future** | Second BLE GATT stream + background logger + FIFO (§5.2.9): then `MEASURE` = fresh acquire (new I2C/ADC work), `FETCH` = latest RAM sample — direction only; not v1 |

#### 4.3.3 Sense configuration (runtime + NVM)

```text
SENSE{N}:AVERAGE:COUNT {<NR1>|MINIMUM|MAXIMUM}
SENSE{N}:AVERAGE:COUNT? [MINIMUM|MAXIMUM]
SENSE{N}:APERTURE {<NRf>|MINIMUM|MAXIMUM}
SENSE{N}:APERTURE? [MINIMUM|MAXIMUM]
```

| Backend | `AVERAGE:COUNT` | `APERTURE` |
|---|---|---|
| INA226 (ch 3/4/5) | On-chip AVG `{1,4,16,64,128,256,512,1024}` | On-chip conversion time as **seconds** (eight legal CT values, e.g. `0.0011`) |
| Nicla ADC (ch 1/2) | Software: average **N** successive ADC samples | Software: **delay between** those samples, in **seconds** |

**Shared INA knobs:** setting AVG or APER on **any** of channels 3, 4, or 5
updates the **same** INA226 registers; queries on 3/4/5 all return that shared
value. Channels 1 and 2 each have **independent** software AVG/APER.

**Defaults** if sensor NVM invalid (macros in `config_nicla.h`):

| Macro | Default |
|---|---|
| `INA226_DEFAULT_AVG` | `16` |
| `INA226_DEFAULT_CONV_US` | `1100` (= 1.1 ms → aperture `0.0011`) |
| ADC AVG / APER defaults | Same numeric defaults (16 and 0.0011 s) unless later overridden by ADC-specific macros |

Boot: load sensor NVM if valid; else apply macros; apply to INA226 / ADC paths.

#### 4.3.4 I2C fault policy (INA226)

Same policy as Si5351 (§3.3): verify transfers where applicable; on failure
queue **-241,"Hardware error"**; latch fault behaviour consistent with the
Si5351 driver (fail-fast while latched; bounded recovery only as already
agreed for Si5351 — no new speculative bus-reset layers).

#### 4.3.5 Future direction (not v1)

High-rate streaming: keep NUS for SCPI; add a **second GATT** service with
packed binary frames + optional FIFO/background logger (§5.2.9). At that
time redefine `MEASURE` ≠ `FETCH` as in §4.3.2.

---

## 5. Transport

### 5.1 Current: USB-CDC Serial

115200 baud. `SerialTransport` in the `.ino` wraps `Serial` as `IScpiTransport`.

### 5.2 BLE (implemented)

**Status:** Behaviour below is agreed and implemented in `Ble.h` / `Ble.cpp`.
Changes to behaviour require updating this section first (§0 rules 5–6).

**Role (ESP32 parity):** Same as WiFi on the ESP32-S3 variant: coexist with
USB-CDC; wireless path carries SCPI. Nicla has no WiFi → BLE replaces WiFi.

**Existing hooks (already in tree):**
- `IScpiTransport` — BLE SCPI transport implements this (like `WifiTcpTransport`).
- USB-CDC SCPI remains via `SerialTransport` + `processExternalLine()`.

#### 5.2.1 Stack and payload

| Item | Decision |
|---|---|
| Nicla library | HardwareBLESerial (Nordic UART Service over ArduinoBLE) |
| Host | ble-serial (Python bleak + com0com) |
| Payload | UART-like SCPI text TX/RX (not a continuous stream) |
| Local name | `Si5351-AABBCC` — last 3 bytes of the board BLE MAC, hex uppercase |
| Centrals | One at a time; extra connection attempts rejected |
| `ble …` channel | USB-CDC only; case-insensitive keyword `ble`; never accepted over BLE SCPI |

#### 5.2.2 Security (current vs later)

| Phase | Decision |
|---|---|
| **Now** | Open BLE (no radio passkey / stack bonding). Short `ble pair` window is the accepted risk. |
| **`ble pin` now** | Stores PIN in BLE FlashIAP only; does **not** gate the radio. |
| **PIN format** | 4–6 decimal digits; no leading-zero requirement beyond that length. Once set, used for all future passkey pairing. Default if unset in flash: `0000`. |
| **Peer identity now (F1)** | App allowlist: store paired central address in BLE FlashIAP; accept only that peer when paired. |
| **Later (documented intent)** | True BLE passkey + bonding (F2); PIN then used on air; stack bond keys; revisit allowlist vs IRK. |
| **Private-address risk** | Windows may use a resolvable/private address that changes → allowlist MAC may fail reconnect. **Ignore until seen in testing**; then revisit (likely with F2). |

#### 5.2.3 Roles, advertising, connect policy

- Nicla is always **peripheral**. The Windows central reconnects; Nicla does not act as central.
- **Unpaired + not in pair mode:** do not advertise.
- **Pair mode** (`ble pair`, or boot with no stored peer): advertise; accept the **first** connecting central; store its address; leave pair mode; enter paired behaviour.
- **Paired:** always advertise; accept **only** the stored peer address; reject others.
- Host may use **Windows Settings Pair** and/or **ble-serial** direct connect during pair mode; either is fine if the allowlisted peer can reconnect afterward.
- After Nicla `ble unpair`, Windows may still list the device; harmless; user may Remove device in Settings if desired.

#### 5.2.4 UART `ble` command set (USB-CDC only)

Mirror ESP32 `wifi …` routing: lines starting with `ble` (case-insensitive) are handled by the BLE manager, not `ScpiParser`.

| Command | Behaviour |
|---|---|
| `ble help` | Print available `ble` commands |
| `ble pin <digits>` | Set PIN (4–6 digits); save to BLE FlashIAP; does not affect radio until passkey work |
| `ble pair` | If a peer is stored → ask confirm (will unpair current). If no peer → enter pair mode immediately |
| `ble unpair` | If a peer is stored → ask confirm. If no peer → short error, ignore |
| `ble yes` | Confirm pending unpair-then-pair or unpair action |
| `ble no` | Cancel pending confirm |

Confirm has **no timeout**. Confirm is required **only** when the action would unpair a stored peer.

#### 5.2.5 Persistence (separate from Si5351)

Own FlashIAP sector and magic — never merged with Si5351 `NvmData` /
`0x5351A001`, so a BLE erase cannot touch Si5351 settings.

**Placement.** Si5351 owns the **last** flash sector
(`get_flash_start() + get_flash_size() − sector_size`). BLE owns the
**third-to-last** sector, `si5351_addr − 2×sector_size` (4096 B on nRF52832),
computed the same runtime way — no hard-coded address.

Stage 0 hardware check found the **second-to-last** sector (`0x7E000` on this
board) already holds non-`0xFF` data that is not our magic, so it must not be
erased. Third-to-last (`0x7D000`) was blank, accepted `0x5351B001`, and survived
reset and sketch reflash. Do not move BLE NVM back to second-to-last without a
new probe.

**Magic.** `0x5351B001` ("B" = BLE, mirroring `0x5351A001`). An erased sector
(`0xFF…`) reads invalid → defaults. A future layout change bumps the low byte
(`0x5351B002`); there is no separate version field.

```cpp
struct __attribute__((packed)) BleNvmData {
    uint32_t magic;        //  4 B — 0x5351B001 when valid
    uint8_t  pinLen;       //  1 B — 4..6
    char     pin[6];       //  6 B — ASCII digits, unused tail = 0
    uint8_t  peerValid;    //  1 B — 0 = no paired peer, 1 = peerAddr valid
    char     peerAddr[18]; // 18 B — "aa:bb:cc:dd:ee:ff" + NUL, as ArduinoBLE prints
};                         // 30 B total
```

The peer address is stored as **text** because ArduinoBLE reports
`central.address()` as a string: the allowlist check is then a direct
case-insensitive compare with no byte-order ambiguity. No address-type field is
stored; if the private-address risk in §5.2.2 appears, that is part of the
revisit.

**Defaults.** Invalid magic → PIN `0000` (`pinLen = 4`), `peerValid = 0`
→ boot into pair mode.

**Write sequence** (same as Si5351 §6): read → `memcmp` → skip if unchanged →
erase sector → program padded to the page size (4 B). Written on `ble pin`
success, pair success (store peer), and unpair (clear peer).

**Verified on hardware (Stage 0):** third-to-last is usable; second-to-last is
not. If a future board revision differs, re-run `spikes/ble-nus/flash_sector_probe`
and update this section before changing the implementation.

**Boot:**
- Peer present → paired advertise + allowlist (central may reconnect).
- Peer absent → enter pair mode (advertise for a new peer).

#### 5.2.6 LED and logs

Serial logs for BLE state changes. RGB LED:

| State | LED |
|---|---|
| Idle unpaired (not advertising) | Off |
| Pair mode | Blue, fast blink (toggle every **200 ms**) |
| Paired, advertising, not connected | Blue, slow blink (toggle every **1000 ms**) |
| BLE connected | Green, solid |
| I2C fault | Red (existing) — **always overrides** BLE colours |

One LED resolver in `loop()`: if `si5351.isFaulted()` → red; else BLE colour
above. Blink timing matches the Stage 0 echo spike for slow blink; fast blink
is 5× that rate for pair mode.

#### 5.2.7 Acceptance test

1. USB-CDC: set PIN (optional), `ble pair` (confirm if needed).
2. Pair/connect from Windows and/or ble-serial; Nicla stores peer and leaves pair mode.
3. ble-serial COM: `*IDN?` succeeds over BLE.
4. USB-CDC SCPI still works concurrently.
5. Reboot with peer stored → advertising; allowlisted central reconnects; non-peer rejected.
6. `ble unpair` (with confirm) → short error if already unpaired; after unpair, no peer advertise until `ble pair` or boot-with-no-peer pair mode.

#### 5.2.8 Implementation plan

Agreed plan of work. Each stage ends with a gate; anything that contradicts
§5.2 comes back as a documented change **before** coding continues (§0 rule 5).
`arduino-cli compile --fqbn arduino:mbed_nicla:nicla_voice` must stay clean at
every stage.

**Stage 0 — toolchain spike (throwaway code, nothing lands in the sketch folder)**

*0A — Nicla side*
1. Install HardwareBLESerial + ArduinoBLE; confirm HardwareBLESerial 1.0.0 (2020) still compiles against the current ArduinoBLE on this core.
2. Throwaway echo sketch: `BLE.begin()`, local name, NUS, advertise, echo each received line. Print the board BLE MAC over USB to learn the real `Si5351-AABBCC` suffix.
3. Record RAM/flash usage from the compile output — early warning for ArduinoBLE memory pressure.
4. Flash-sector probe: write a pattern to the **third-to-last** sector (§5.2.5), reboot, confirm it survived and the Si5351 sector is untouched. Do not erase the second-to-last sector (occupied on this board).

*0B — PC side*
5. Install com0com. The classic build is unsigned on current Windows; record which build/procedure actually works.
6. Create a com0com pair; note which end ble-serial attaches to and which end the host opens.
7. `pip install ble-serial` in a venv; `ble-scan` must show the board with its NUS UUID.
8. Start ble-serial against the board; confirm the virtual COM port enumerates.

*0C — end-to-end and known unknowns*
9. Echo lines over the virtual COM, including a line longer than the 20-byte default ATT MTU, to confirm framing survives chunking.
10. Confirm `\n` framing behaves as `ScpiParser` expects (no stripped or doubled newlines through ble-serial).
11. **Address stability:** note board and PC addresses across a PC reboot and a board reboot — this is the private-address risk in §5.2.2.
12. Try both host paths from §5.2.3 (Windows Settings Pair; direct ble-serial connect) and whether each survives disconnect/reconnect.
13. Rough round-trip latency for a short line, to judge SCPI SET pacing over BLE against the UART path.
14. Confirm USB-CDC still works while the BLE link is up.

*Gate:* HardwareBLESerial builds, the sector is usable, and the echo round-trips
over the virtual COM. Outputs feed the later stages: working com0com build,
exact ble-serial invocation, MTU/framing behaviour, address stability, RAM
headroom.

**Stage 1 — BLE persistence.** `BleNvm.h/.cpp` following the FlashIAP pattern
already in `Si5351Driver.cpp` (runtime sector lookup, read-before-erase).
`load()` falls back to defaults on bad magic; `save()` skips when unchanged.
*Test:* set PIN, reboot, PIN persists, Si5351 settings unaffected.

**Stage 2 — BLE transport.** `BleTransport` implements `IScpiTransport` over
HardwareBLESerial, as `WifiTcpTransport` did on ESP32. A second `ScpiParser`
instance is polled in `loop()` beside the UART one. Check HardwareBLESerial's
buffer size against `ScpiParser::BUF_SIZE` so long SCPI lines are not truncated.

**Stage 3 — BLE manager.** `BleManager.h/.cpp` owns NVM, radio, allowlist, LED
and logs. States per §5.2.3:

| State | Entry | Advertising | Accepts |
|---|---|---|---|
| `IDLE_UNPAIRED` | after `ble unpair` | no | — |
| `PAIRING` | `ble pair`, or boot with no peer | yes | first central → store address → `PAIRED_ADV` |
| `PAIRED_ADV` | boot with peer, or disconnect | yes | stored address only; others disconnected immediately |
| `CONNECTED` | allowlisted central connected | no | one central only |

Without bonding, rejection is `central.disconnect()` immediately after a
non-matching connect.

**Stage 4 — UART `ble` router.** In `loop()`, a line starting with `ble`
(case-insensitive, followed by end-of-line or space) goes to
`BleManager::handleSerialCommand()` instead of `scpiUart`; mirrors ESP32
`WiFiManager::handleSerialCommand`. BLE-side lines never reach this router, so
`ble …` stays USB-CDC only (§5.2.4). Pending-confirm state lives in the manager.

**Stage 5 — LED and logs.** Non-blocking blink in `loop()`. Because the I2C
fault colour has priority (§5.2.6), the two direct `nicla::leds.setColor()`
calls in the `.ino` move into one resolver that picks red when
`si5351.isFaulted()`, else the BLE colour. Blink periods: fast **200 ms**,
slow **1000 ms** (documented in §5.2.6).

**Stage 6 — docs and acceptance.** Update §3.2, §7 and the `.ino` header
comment (it still says "BLE deferred"); run §5.2.7 end to end.

**Known risk:** RAM headroom for ArduinoBLE plus the SoftDevice on the
nRF52832 alongside the existing code. If it does not fit, that is a design
conversation here, not a silent workaround.

#### 5.2.9 Future expansion — sensor streaming (direction only; not implemented)

Agreed direction for a later high-rate sensor stream on the same BLE link,
without fighting NUS/SCPI overhead:

- Keep the existing **Nordic UART Service (NUS)** for SCPI text (control /
  query). Do **not** stream samples as one-sample-per-notification text over
  NUS.
- Add a **second GATT service** dedicated to sensor data (same connection,
  same pairing/allowlist story).
- Use a **custom 128-bit UUID** service (project-defined), not a Bluetooth SIG
  profile such as Heart Rate or Environmental Sensing — those target
  interoperable single readings, not max sample-rate packed streams.
- Typical shape: Notify characteristic `StreamData` carrying **packed binary
  frames** (many samples per notification); optional Write characteristic
  `StreamControl` (start/stop, rate, which sensors). Prefer **Notifications**
  (not Indications).
- When implementing, raise **ATT MTU** (and enable **DLE** where the stack
  allows) on **both** Nicla and the PC/central so each frame can approach
  ~244 B payload (ATT MTU 247). Align characteristic max length and host
  write/notify chunk size with that MTU.
- **L2CAP Connection-Oriented Channels (CoC)** are **not** required for the
  first expansion. Reconsider only if GATT notify throughput is still
  insufficient and both the Nicla stack and Windows host support CoC cleanly.

---



## 6. Persistence

**Backend: Mbed OS `FlashIAP` (`#include "FlashIAP.h"`)** — built into the Arduino Mbed OS Nicla Boards core.

### 6.1 Flash sector map

| Owner | Sector | Magic (when used) |
|---|---|---|
| Si5351 | **Last** sector | `0x5351A001` |
| *(do not use)* | **Second-to-last** | Occupied on this board — see §5.2.5 |
| BLE | **Third-to-last** | BLE NVM magic (§5.2.5) |
| Sensors (INA226 + ADC AVG/APER) | **Fourth-to-last** | `0x5351C001` — never merge with Si5351 or BLE |

All addresses computed at runtime from `FlashIAP::get_flash_start()` /
`get_flash_size()` / `get_sector_size()` — no hard-coded absolute addresses.

### 6.2 Si5351 NVM

All Si5351 settings are packed into one 38-byte `NvmData` struct stored at the **last flash sector**.

```cpp
struct __attribute__((packed)) NvmData {
    uint32_t magic;                        //  4 B — 0x5351A001 when valid
    int32_t  corrPpb;                      //  4 B
    uint64_t freqHz [SI5351_NUM_CLOCKS];   // 24 B
    uint8_t  enabled[SI5351_NUM_CLOCKS];   //  3 B  (0 or 1)
    uint8_t  drive  [SI5351_NUM_CLOCKS];   //  3 B
};                                         // 38 B total
```

`magic == 0x5351A001` → valid. An erased sector (all `0xFF`) → invalid → defaults used, no restore.

**Write sequence (`_nvmFlush`):**
1. Read current sector content.
2. Compare with new data (`memcmp`). If identical → return immediately (no wear).
3. Erase the sector.
4. Program the new data (padded to the page-program granularity, 4 B on nRF52832).

| `StoreMode` | Effect |
|---|---|
| `SET_ONLY` | Applies to chip in RAM only |
| `SET_N_STORE` | Applies to chip + writes flash (read-before-erase) |

Settings are restored in `Si5351Driver::begin()` on every boot.

### 6.3 Sensor NVM (implemented)

Separate sector (§6.1), owned by `SensorManager`. Layout:

```cpp
struct __attribute__((packed)) SensorNvmData {
    uint32_t magic;         // 0x5351C001 when valid
    uint16_t inaAvg;
    uint16_t inaConvUs;
    uint16_t adcAvg[2];
    uint32_t adcAperUs[2];
};                          // 20 B
```

Invalid / missing magic → use `config_nicla.h` macros (§4.3.3).  
`*SAV` flushes this sector (and Si5351) from runtime; boot / `*RCL` reload.

---

## 7. File Structure

```
SCPI_SI5351_NICLA/
├── SCPI_SI5351_NICLA.ino
├── config_nicla.h
├── pinDef_nicla.h
├── IScpiTransport.h
├── Ble.h
├── Ble.cpp
├── Si5351Driver.h
├── Si5351Driver.cpp
├── Sensors.h
├── Sensors.cpp
├── ScpiParser.h
├── ScpiParser.cpp
└── docs/
    ├── SCPI_SI5351_NICLA.md      # design source of truth
    └── SCPI_DRIVER_GUIDE.md      # host-driver SCPI communication guide
```

---

## 8. Building and Flashing

1. Open `SCPI_SI5351_NICLA/SCPI_SI5351_NICLA.ino` in Arduino IDE.
2. Board: **Arduino Mbed OS Nicla Boards → Arduino Nicla Voice**.
3. Libraries: **Etherkit Si5351**, **ArduinoBLE**, **HardwareBLESerial**, **INA226_WE**.
4. Upload, then Serial Monitor at **115200 baud**.
5. Expect: `Si5351 OK — SYS_INIT=...` (or `FAIL: Si5351 not found` + red LED if absent), `INA226 OK` (or WARN if absent), BLE name/state, then `UART+BLE SCPI ready.`
6. USB-CDC: `ble help` for pairing commands; SCPI also over BLE NUS once paired
   (PC: `ble-serial-app/` — ble-serial + com0com; Nicla spike notes in `spikes/ble-nus/`).

### Testing

```bash
python test_scripts/scpi_selftest.py -p COMxx
python test_scripts/sensor_selftest.py -p COMxx
python test_scripts/uart_freq_sweep.py -p COMxx -s 100 -e 10000 -t 100 -i 50
python test_scripts/uart_freq_sweep.py -p COMxx -s 100 -e 10000 -t 100 -i 50 --opc
```

The sweep script **always waits** for a `SYSTem:ERRor?` reply after each SET
before sending the next command. Do not remove that pacing: flooding SETs
faster than `set_freq` + verify can finish overflows the Nicla USB-CDC buffer,
can wedge I2C, and leaves the Si5351 stuck on the last frequency that landed
(often looking like “frozen” output after a reboot mid-sweep).

`--opc` additionally sends `*OPC?` after each SET and waits for `1`. Use it
against **Nicla** firmware only (ESP32 does not implement `*OPC?`).

Example:
```
*IDN?
  → Arduino,Si5351-NiclaVoice,001,1.0.0

SOURce1:FREQuency:FIXed 10000000
*OPC?
  → 1
OUTPut1:STATe ON
SOURce1:FREQuency:FIXed?
  → 10000000.0
```

Sensor examples (after §4.3 is implemented):
```
MEAS3:VOLT?
MEAS4:CURR?
MEAS5:POW?
SENS1:AVER:COUN 16
SENS3:APER 0.0011
*SAV
```
---

## 9. Implementation plan — sensors, `*RST`, `*SAV` / `*RCL`

Agreed plan of work for §2.3–2.4, §4.2–4.3, and §6.1 / §6.3. Each stage ends
with a gate; anything that contradicts those sections comes back as a
documented change **before** coding continues (§0 rule 5).
`arduino-cli compile --fqbn arduino:mbed_nicla:nicla_voice` must stay clean at
every Nicla stage. ESP32 `*RST` changes use the ESP32 FQBN / IDE board as usual.

Throwaway spike code lives under `spikes/ina226/` (Stage 0). Product code
is in `SCPI_SI5351_NICLA/` (`Sensors.*`, parser, `*SAV`/`*RCL`, `*RST`).
Record Stage 0 findings in `spikes/ina226/RESULTS.md`.

**Implementation status (2026-07-27):** Stages 0–7 code is in tree and
compiles for `arduino:mbed_nicla:nicla_voice`. Stage 0 software/lib gate
passed; metrology gate still open until the 7.5 V load is connected.
On-target upload/self-test may require a physical Nicla reset if SWD
reports `cannot read IDR`.


**Out of scope for this plan:** GATT sensor stream / FIFO / `MEASURE`≠`FETCH`
(§4.3.5, §5.2.9); speculative I2C bus-reset layers (§0 rule 3).

---

**Stage 0 — INA226 hardware / library spike (throwaway)**

*0A — Bring-up*
1. Install **INA226_WE**; confirm it compiles for `arduino:mbed_nicla:nicla_voice`.
2. Throwaway sketch: `nicla::begin()`, default `Wire` (SDA=4, SCL=3), init INA226
   at `0x40` (or scan and record the real address), calibrate with
   `INA226_SHUNT_OHMS` / `INA226_I_MAX_A` from §2.3, set `AVERAGE_16` +
   `CONV_TIME_1100`, continuous mode.
3. USB Serial @ 115200: print bus voltage, current, and **chip power register**
   on a slow timer or a `read` command. Confirm ~7.5 V / ~60–100 mA looks
   plausible vs a meter when the load is present.
4. Change average count and conversion time from Serial; confirm the chip
   accepts the discrete sets and readings still make sense.

*0B — Coexistence / platform risks*
5. On the same sketch (or a second throwaway), also init the Etherkit Si5351 on
   the same bus. Record safe **init order** and whether extra `Wire.begin()`
   calls cause the known Mbed leak / wedged-bus issues (§3.3 / §3.4). Prefer
   leaving `Wire.begin()` to library `init` as in product Si5351 code.
6. Record RAM/flash from compile output (baseline vs spike) for headroom notes
   beside BLE.

*0C — Record*
7. Fill `spikes/ina226/RESULTS.md`: address, wiring notes, V/I/P vs meter,
   AVG/CT behaviour, Si5351 coexistence, any lib quirks.

*Gate:* INA226_WE builds on Nicla; device talks at the documented (or corrected)
address; V / I / P reads are credible; coexistence notes are written. **No
product INA driver until this gate passes.** If the address or wiring differs
from §2.3, update §2.3 **before** Stage 3.

---

**Stage 1 — `*RST` soft-reset (Nicla and ESP32)**

1. Nicla `ScpiParser::cmdRst()`: MCU soft-reset (Mbed / NVIC), not “disable
   outputs only.”
2. ESP32 `ScpiParser::cmdRst()`: `ESP.restart()` (or equivalent).
3. Update this document §4.2 and `docs/IMPLEMENTATION.md` §3.1.1 status to
   **implemented** when both parsers match.

*Test:* send `*RST` over USB-CDC; board reboots; `setup()` runs; Si5351 NVM
(and later sensor NVM) reloads as on power-up.

*Gate:* both platforms soft-reset; no silent “outputs off only” left in either
`cmdRst()`.

---

**Stage 2 — Config macros + sensor FlashIAP sector**

1. Add §2.3 / §4.3.3 macros to `config_nicla.h`
   (`INA226_I2C_ADDR`, `INA226_SHUNT_OHMS`, `INA226_I_MAX_A`,
   `INA226_DEFAULT_AVG`, `INA226_DEFAULT_CONV_US`, ADC default AVG/APER).
2. Sensor NVM helper (name TBD): **fourth-to-last** sector (§6.1), own magic,
   read-before-erase, `memcmp` skip-if-unchanged — same pattern as Si5351 / BLE.
3. Struct holds: shared INA AVG+APER; ADC1 AVG+APER; ADC2 AVG+APER.
4. If sector placement is uncertain, a one-shot probe (reuse Stage 0 flash-probe
   ideas) before erasing anything; never touch last / second-to-last /
   third-to-last.

*Test:* empty sector → defaults; write pattern; reboot → restored; Si5351 and
BLE sectors untouched.

*Gate:* fourth-to-last proven safe; load/flush API ready for the INA driver.

---

**Stage 3 — INA226 product driver (logical channels 3/4/5)**

1. Thin wrapper over INA226_WE in `SCPI_SI5351_NICLA/` (filenames TBD at
   coding time): `begin()`, read bus V / current / **power register**,
   set/get shared AVG and aperture (seconds ↔ discrete CT), NVM apply/flush,
   I2C fault policy per §4.3.4 (same spirit as Si5351; no new bus-reset layers).
2. Init order follows Stage 0 RESULTS.
3. No SCPI yet required, but driver must be callable from the `.ino` for a
   smoke print if useful.

*Test:* after `begin()`, readings match spike credibility; AVG/APER set/get
round-trip; bad I2C surfaces as failure for the parser to map to `-241`.

*Gate:* driver stable on hardware with Si5351 + BLE still in the tree.

---

**Stage 4 — SCPI `MEASURE` / `FETCH` / `SENSE` for channels 3/4/5**

1. Extend `ScpiParser` with header-suffix parsing like `SOURCE{N}` (omit `{N}`
   → 1).
2. Implement §4.3.2–4.3.3 for INA channels only: `MEASURE` ≡ `FETCH` in v1;
   legal matrix → else `-221,"Settings conflict"`.
3. Wire the driver into **both** UART and BLE `ScpiParser` instances.

*Test:* `MEAS3:VOLT?`, `MEAS4:CURR?`, `MEAS5:POW?`; `MEAS3:CURR?` → `-221` via
`SYSTEM:ERROR?`; `SENS3:AVER:COUN` / `SENS5:APER` alias shared regs; BLE NUS
same commands.

*Gate:* INA SCPI surface matches §4.3 on UART and BLE.

---

**Stage 5 — `*SAV` / `*RCL` (single working set)**

1. `*SAV` [`<n>`]: ignore `<n>`; flush Si5351 store path + sensor NVM from
   runtime (§4.2).
2. `*RCL` [`<n>`]: ignore `<n>`; reload both; re-apply to hardware.
3. Individual `SENSE` SETs remain runtime-only until `*SAV`.

*Test:* change INA AVG, `*SAV`, reboot (or `*RCL`) → value restored; Si5351
frequency still correct after `*SAV`.

*Gate:* §4.2 behaviour for save/recall works with Si5351 + INA.

*(ESP32 `*SAV`/`*RCL` for Si5351-only is optional parity — do not implement
unless explicitly agreed; Nicla sensors do not exist on ESP32.)*

---

**Stage 6 — Nicla ADC channels 1/2 (blocked on pins)**

1. **Doc first:** lock ADC GPIO / analog pins in §2.4 and `pinDef_nicla.h`
   (still open). No ADC product code before that update.
2. Software average of N samples with inter-sample delay = aperture (seconds);
   independent AVG/APER per channel in sensor NVM.
3. SCPI legal matrix for ch 1–2; `MEAS1:CURR?` → `-221`.

*Test:* `MEAS1:VOLT?` / `MEAS2:VOLT?`; `SENS1:AVER:COUN` / `SENS2:APER`;
`*SAV` persists ADC knobs.

*Gate:* §4.3 complete for all five logical sensors.

---

**Stage 7 — `.ino` integration, LED, docs, acceptance**

1. Construct sensor driver(s) in `setup()` with Stage 0 init order; brief boot
   log for INA OK/fail (essential only).
2. LED: keep §5.2.6 resolver; if both Si5351 and INA can fault, document
   precedence in §4.3.4 / §5.2.6 before coding a tie-break (default suggestion:
   any I2C/sensor fault → red).
3. Flip all “agreed — not yet implemented” banners in §2.3–2.4, §4.2–4.3, §6.1,
   §6.3, §7, §8 to match code.
4. Acceptance checklist: `*RST` reboot; INA MEAS/SENS; `-221` cases; `*SAV` +
   reboot; ADC MEAS/SENS once Stage 6 done; BLE SCPI still healthy; compile
   size note.

*Gate:* document and firmware match; §8 examples work on hardware.

---

**Known risks**

- Extra `Wire.begin()` on Mbed (§3.3) — spike must settle init order.
- Shared bus with Si5351: no high-rate INA polling in `loop()` for v1.
- Fourth-to-last FlashIAP must not collide with Si5351 / occupied /
  BLE sectors (§6.1).
- RAM with ArduinoBLE + SoftDevice + INA226_WE — watch compile sizes each stage.
- ADC Stage 6 is **blocked** until pins are agreed in the doc.
