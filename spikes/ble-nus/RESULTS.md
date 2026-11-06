# BLE NUS spike RESULTS

Filled 2026-07-27 during the spike; updated after §5.2.5 third-to-last approval and com0com install.
(Host app path later moved to repo-root `ble-serial-app/`; this folder was `spikes/stage0`.)

## Environment

| Item | Value |
|---|---|
| Date | 2026-07-27 |
| arduino-cli | 1.5.1 (bundled with Arduino IDE) |
| FQBN | `arduino:mbed_nicla:nicla_voice` |
| Core | mbed_nicla 4.6.0 |
| ArduinoBLE | 2.1.0 (already installed) |
| HardwareBLESerial | 1.0.0 (already installed) |
| Python | 3.11.9 |
| Nicla USB COM | COM10 |
| Board BLE MAC | `7c:7e:d0:52:1f:06` |
| Derived local name | `Si5351-521F06` (last 3 bytes) |

## 0A-1 Libraries / baseline compile

| Item | Value |
|---|---|
| ArduinoBLE install | already present (2.1.0) |
| HardwareBLESerial install | already present (1.0.0) |
| HardwareBLESerial compile OK? | **YES** — builds against ArduinoBLE 2.1.0 |
| Compile error (if any) | none |
| Baseline flash (SCPI_SI5351_NICLA) | 80184 B (15%) |
| Baseline RAM (SCPI_SI5351_NICLA) | 13256 B (20%) |

## 0A-2 / 0A-3 Echo spike

| Item | Value |
|---|---|
| Spike flash | 271928 B (51%) |
| Spike RAM | 35576 B (55%) |
| Delta flash vs baseline | +191744 B |
| Delta RAM vs baseline | +22320 B |
| Runtime heap (`heap` while connected) | `current=0 max=0 …` — mbed heap stats disabled on this core build. Use static RAM figures. |
| `BLE_SERIAL_RECEIVE_BUFFER_SIZE` | 256 |
| `ScpiParser::BUF_SIZE` | 256 |
| Buffer comparison | equal |
| `BLE_ATTRIBUTE_MAX_VALUE_LENGTH` | 20 |
| Advertise / connect / echo OK? | **YES** |

## 0A-4 Flash sector probe (after §5.2.5 update)

| Item | Value |
|---|---|
| flash start | `0x00000000` |
| flash size | 524288 |
| sector size | 4096 |
| page size | 4 |
| Si5351 sector (last) | `0x0007F000` |
| Avoid (2nd-last) | `0x0007E000` — still holds `A4 D6 BE 29…`; **do not erase** |
| BLE sector (3rd-last, §5.2.5) | `0x0007D000` |
| `write` to BLE 3rd-last | **PASS** (counter=2) |
| Survived DTR reset? | **YES** |
| Si5351 intact (`checksi`)? | Yes (erased / unused on this board during spike) |
| Survived reflash earlier? | **YES** (prior writethird test) |

§5.2.5 in the design doc now specifies **third-to-last**. Contradiction resolved.

## 0B PC side

| Item | Value |
|---|---|
| com0com | Installed at `C:\Program Files (x86)\com0com\` — **no environment variable required** |
| `ble-com-setup` | OK (default install path). Creates `COM9 <-> BLE` pair |
| Extra pair observed | `COM11` / `COM12` (also OK) |
| Port pair used for probe | ble-serial `-p COM11`, host `echo_probe.py -p COM12` |
| Official pair also available | `BLE` (ble-serial) ↔ `COM9` (host) |
| Python venv | `ble-serial-app/.venv-ble` (trusted-host flags needed on this machine; path was `spikes/stage0/pc/` at spike time) |
| Packages | ble-serial 3.0.0, bleak 3.0.2, pyserial 3.5 |
| `ble-scan` | **YES** — `7C:7E:D0:52:1F:06` / `Si5351-521F06` |
| NUS UUID | **YES** |
| `ble-serial` (COM) | `ble-serial -d 7C:7E:D0:52:1F:06 -a public -p COM11` |
| Link held? | **YES** |

## 0C End to end (com0com COM12)

| Item | Value |
|---|---|
| Short-line echo | **PASS** |
| Long lines (40 / 100 / 200) | **PASS** all three |
| CRLF vs LF | **PASS** |
| Burst (5 lines) | **PASS** |
| RTT min / avg / max (ms) | **119.5 / 252.0 / 599.4** (10 iterations) |

Earlier TCP-only run had a flaky 200-char timeout; COM path with the same spike passed 200 chars. Prefer host pacing anyway for product SCPI.

## 0C Manual observations

| Item | Value |
|---|---|
| Board address | Stable `7C:7E:D0:52:1F:06` |
| Central address (one connect) | `8c:3b:4a:54:b8:47` |
| Private-address risk seen? | Not in these sessions |
| Windows Settings Pair | Not exercised |
| Direct ble-serial | **Works** |
| USB-CDC concurrent with BLE? | **YES** |

## Gate

| Criterion | Pass? |
|---|---|
| HardwareBLESerial compiles | **PASS** |
| Board advertises and echoes | **PASS** |
| BLE sector survives reboot; Si5351 intact | **PASS** (3rd-last) |
| Echo round-trips over virtual COM | **PASS** (com0com COM11↔COM12) |

## Contradictions vs §5.2

None open. Third-to-last placement is documented and re-verified.
