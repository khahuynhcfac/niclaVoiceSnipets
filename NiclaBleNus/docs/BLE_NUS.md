# BLE NUS — design and implementation reference

This document covers the BLE Nordic UART Service stack in `NiclaBleNus/`.
It is the source of truth for decisions about connectivity, pairing policy,
flash layout, LED feedback, and the `ble …` console. Change behaviour here
first, then code.

---

## 1. Stack overview

| Item | Choice |
|---|---|
| Board | Arduino Nicla Voice (nRF52832, Mbed OS) |
| BLE role | Peripheral only — the board never initiates connections |
| GATT service | **Nordic UART Service (NUS)** via `HardwareBLESerial` |
| Underlying BLE library | ArduinoBLE 2.1.0 |
| Host bridge | `ble-serial` (Python bleak) + com0com virtual COM pair |
| Payload framing | Newline-terminated (`\n`) text lines |
| Device name | `<DEVICE_NAME_PREFIX>-AABBCC`, where `AABBCC` is the last three bytes of the board BLE MAC in hex uppercase |
| Simultaneous centrals | One at a time; extra connections are rejected immediately |
| `ble …` commands | USB serial only; a central can never re-pair the board it is connected to |

The NUS presents two GATT characteristics — RX (Write) and TX (Notify) — that
together behave like a UART. `HardwareBLESerial` wraps them into an Arduino
`Stream`. The ATT MTU is 20 bytes by default, so the library chunks long lines
automatically; lines up to 256 bytes have been verified round-trip.

---

## 2. Connection lifecycle

### 2.1 States

```
        ┌──────────────────────────────────────────────────────────────────┐
        │                        BleLinkState                              │
        │                                                                  │
        │   Idle ──ble pair──► Pairing ──first central connects──► Connected
        │     ▲                   │                                    │   │
        │     │                  │  (peer stored to flash)             │   │
        │     │                  ▼                                     │   │
        │     └──ble unpair── Advertising ◄──────── disconnected ──────┘   │
        │                        │                                         │
        │                        └──────────── wrong peer rejected ────────┘
        └──────────────────────────────────────────────────────────────────┘
```

| State | Radio | Accepts |
|---|---|---|
| `Idle` | quiet, not advertising | nobody |
| `Pairing` | advertising | the first central that connects; stores its address, moves to `Connected` |
| `Advertising` | advertising | the stored peer address only; others disconnected immediately |
| `Connected` | not advertising (SoftDevice stops automatically) | the connected peer |

### 2.2 Boot behaviour

| Flash record | Boot state |
|---|---|
| No valid record (first boot, or after `ble unpair`) | `Pairing` |
| Valid peer address stored | `Advertising` (peer may reconnect) |

### 2.3 Central rejection

Rejection is performed at the application layer, not at the radio:
`central.disconnect()` is called immediately when the connecting address does
not match the stored peer. The radio-level allowlist is not used because it
would require bonding, which is not implemented.

---

## 3. Peer allowlist and flash persistence

### 3.1 What is stored

```cpp
// PairedPeerStorage.h
struct __attribute__((packed)) Record {
    uint32_t magic;
    uint8_t  peerValid;
    char     peerAddress[18];  // "aa:bb:cc:dd:ee:ff" + NUL
};
```

The address is stored as the text string `ArduinoBLE` reports from
`central.address()`. The allowlist check is then a case-insensitive string
compare — no byte-order ambiguity, no address-type field needed.

### 3.2 Flash sector placement

| Owner | Sector | Address on this board |
|---|---|---|
| **NiclaBleNus** peer record | **Third-to-last** | `0x0007D000` |
| *(do not erase)* | Second-to-last | `0x0007E000` — occupied by foreign data |
| *(free for app)* | Last | `0x0007F000` |

The address is computed at runtime, not hard-coded:

```cpp
// PairedPeerStorage.cpp
uint32_t recordAddress(mbed::FlashIAP& flash) {
    const uint32_t flashStart = flash.get_flash_start();
    const uint32_t flashSize  = flash.get_flash_size();
    const uint32_t sectorSize = flash.get_sector_size(flashStart + flashSize - 1);
    return flashStart + flashSize - 3 * sectorSize;
}
```

**Why third-to-last?**
Hardware probing (`spikes/ble-nus/flash_sector_probe`) found the
second-to-last sector already holds non-`0xFF` data. Erasing it corrupts the
board. The third-to-last was blank, survived a write/reset/verify cycle, and
is unaffected by sketch reflash.

### 3.3 Magic value

`PEER_STORAGE_MAGIC` (defined in `BleNusConfig.h`, default `0x4E555301`)
identifies a valid record. A sector full of `0xFF` reads as invalid — the
board boots into pairing mode. Give each project a distinct magic so that
flashing one project over another never causes the old peer record to be
misread as valid.

### 3.4 Write sequence

1. Read existing sector contents.
2. Compare with the new record (`memcmp`).
3. Skip write if unchanged.
4. Erase the sector, then program the record padded to the page size (4 bytes).

This minimises flash wear. Writes happen on: first successful pairing (peer
address stored), `ble unpair` confirmation (record cleared).

---

## 4. Operator console (`ble …` commands)

Commands are accepted on the **USB serial port only**. The check is an
`equalsIgnoreCase("ble")` prefix match; anything that does not match is passed
to the application's `handleUsbLine()`.

| Command | Behaviour |
|---|---|
| `ble help` | Print the command list and the current peer address (or "no peer paired") |
| `ble pair` | If no peer stored: enter pairing immediately. If a peer is stored: ask for confirmation first |
| `ble unpair` | If a peer is stored: ask for confirmation. If no peer: print a short message and return |
| `ble yes` | Confirm the pending pair or unpair |
| `ble no` | Cancel the pending pair or unpair |

Confirmation is required only when an action would **forget an existing peer**.
The confirmation prompt does not time out.

### Why these commands are USB-only

A connected central sending `ble unpair` over NUS would be able to disconnect
itself and force the board into pairing mode, where anyone could claim it.
`BleConsole` checks the line before `handleBleLine()` is ever called — BLE
lines are never routed through `BleConsole`.

---

## 5. Device naming

```cpp
// BleNusPeripheral.cpp
void buildDeviceName(const String& macAddress, char* name, size_t size) {
    // ...extracts the last three bytes of the MAC as uppercase hex...
    snprintf(name, size, "%s-%s", BleNusConfig::DEVICE_NAME_PREFIX, lastThreeBytes);
}
```

With the default prefix `NiclaNus` and a board MAC of `7c:7e:d0:52:1f:06`,
the advertised name is `NiclaNus-521F06`. This name appears in BLE scans and
in `ble-serial --scan`.

---

## 6. LED status

Managed by `BleStatusLed`, which is entirely separate from the BLE stack.
Delete `BleStatusLed.h/.cpp` and the `statusLed` calls in the `.ino` if your
project uses the LED for something else.

| State | LED colour | Pattern |
|---|---|---|
| `Idle` | off | — |
| `Pairing` | blue | fast blink, toggle every 200 ms |
| `Advertising` | blue | slow blink, toggle every 1000 ms |
| `Connected` | green | solid |

The blink periods are configurable in `BleNusConfig.h`:
`PAIRING_BLINK_PERIOD_MS` and `ADVERTISING_BLINK_PERIOD_MS`.

---

## 7. USB serial logs

Every state transition logs itself. Nothing in the application needs to call
anything — the logs come from inside `BleNusPeripheral`:

```
BLE: peer record loaded from flash
BLE: address 7c:7e:d0:52:1f:06
BLE: device name NiclaNus-521F06
BLE: advertising for peer 8c:3b:4a:54:b8:47
BLE: advertising started
BLE: connected to 8c:3b:4a:54:b8:47
BLE: disconnected
BLE: rejecting central f4:5c:89:12:34:ab
```

---

## 8. Data path API

All of these are members of `BleNusPeripheral`. Every call is **inert unless
the state is `Connected`**, so the application never needs to guard with
`isConnected()` before calling them.

```cpp
bool isConnected();                         // true iff state == Connected
int  available();                           // bytes waiting in RX buffer
int  read();                                // read one byte, or -1
bool readLine(char* buffer, size_t size);   // one \n-terminated line, no \n in buffer
void print(const char* text);               // send text to central
void flush();                               // flush TX buffer
```

`readLine` uses `HardwareBLESerial::availableLines()` and `readLine()` —
complete lines are only returned once the `\n` terminator has been received,
so partial lines are never delivered to the application.

---

## 9. Security posture and known limitations

| Aspect | Current behaviour |
|---|---|
| Passkey / bonding | None — the link is open at the radio level |
| Peer identity | Application-layer allowlist: stored MAC compared after connect |
| Pairing window | Open only while in `Pairing` state; the board returns to `Advertising` after the first connection |
| Private address risk | Windows may rotate its BLE address, causing reconnect to fail. Not observed in testing. If seen, revisit with radio-level bonding |

The pairing window is the main exposure: anyone in radio range can claim an
unpaired board. Keep the `ble pair` window short and run it in a controlled
environment.

---

## 10. Host-side setup (PC / ble-serial)

Verified configuration from hardware testing (`spikes/ble-nus/RESULTS.md`):

| Item | Value |
|---|---|
| Python | 3.11.9 |
| ble-serial | 3.0.0 |
| bleak | 3.0.2 |
| pyserial | ≥ 3.5 |
| com0com | Installed at `C:\Program Files (x86)\com0com\` |
| Verified RTT (min / avg / max) | 119.5 / 252.0 / 599.4 ms (short lines, 10 iterations) |
| Max line length verified | 200 characters |

**Install:**

```bash
cd ble-serial-app
py -m venv .venv-ble
source .venv-ble/Scripts/activate
pip install -r requirements.txt
```

**Connect:**

```bash
# Scan to find the board address:
ble-scan

# Start the bridge (replace address and COM port):
ble-serial -d 7C:7E:D0:52:1F:06 -a public -p COM11

# Run the echo probe against the other end of the com0com pair:
python ble-serial-app/echo_probe.py -p COM12 -v
```

---

## 11. Configuration reference (`BleNusConfig.h`)

| Constant | Default | What to change it to |
|---|---|---|
| `DEVICE_NAME_PREFIX` | `"NiclaNus"` | Your project's short name, max ~16 chars |
| `PEER_STORAGE_MAGIC` | `0x4E555301` | Any new 32-bit value, unique per project |
| `PAIRING_BLINK_PERIOD_MS` | `200` | LED fast-blink half-period in ms |
| `ADVERTISING_BLINK_PERIOD_MS` | `1000` | LED slow-blink half-period in ms |
| `USB_SERIAL_BAUD` | `115200` | USB CDC baud rate |
| `MAX_BLE_LINE_LENGTH` | `256` | Receive buffer size in the `.ino` loop |

---

## 12. Acceptance checklist

Run once before declaring the BLE layer working in a new project:

| # | Test | Pass when |
|---|---|---|
| 1 | Compile | `arduino-cli compile` exits 0, no warnings |
| 2 | USB console routing | `ble help` prints the command list; unrecognised lines reach `handleUsbLine` |
| 3 | BLE echo | `echo_probe.py -p COMxx -v` prints `DONE: all passed` |
| 4 | Peer persists across reboot | Power-cycle the board; `ble-serial` reconnects without `ble pair` |
| 5 | Wrong peer rejected | Connect from a second device; USB serial prints `BLE: rejecting central ...`; NUS link does not come up |
| 6 | Unpair cycle | `ble unpair` → `ble yes` → board goes idle; `ble pair` → next central connects and is remembered |

---

## 13. Future: adding a sensor stream on the same link

If the project later needs high-rate data alongside the NUS text channel,
the agreed direction is:

- Keep NUS for text (control and query).
- Add a **second GATT service** with a custom 128-bit UUID on the same connection.
- Use **Notify** characteristics carrying **packed binary frames** (many samples
  per notification), not one-sample-per-notification text over NUS.
- Raise the ATT MTU (and enable DLE where the stack allows) on both sides so
  each notification approaches ~244 bytes payload.
- L2CAP connection-oriented channels are not needed at first; reconsider only if
  GATT notify throughput is still insufficient.

The pairing and allowlist logic in `BleNusPeripheral` carries over unchanged —
the second service uses the same connection and the same peer.
