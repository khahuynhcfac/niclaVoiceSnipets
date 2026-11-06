# NiclaBleNus_LowPowerTest — beacon current measurement

Working copy of `NiclaBleNus`. Leave `NiclaBleNus/` as the untouched template.

This sketch does **not** keep a BLE connection. Every 5 s it sends **one
non-connectable advertising event** (three PDUs on channels 37/38/39), then
puts the nRF52832 into **System ON idle** until the RTC wakes it. RAM is
retained. USB serial is left off so the trace is not dominated by UART.

## Requirements

- Board: `arduino:mbed_nicla:nicla_voice`
- Libraries: ArduinoBLE
- Host: Python 3.10+ with `bleak` (see `tools/requirements.txt`)

```bash
CLI="$LOCALAPPDATA/arduino-cli/arduino-cli.exe"
"$CLI" compile --fqbn arduino:mbed_nicla:nicla_voice NiclaBleNus_LowPowerTest
"$CLI" upload -p COMxx --fqbn arduino:mbed_nicla:nicla_voice NiclaBleNus_LowPowerTest
```

## What the board does

On boot:

- The CPU stays **awake for 10 s** (blue LED) so SWD can attach. Press
  RESET once, then upload during that window. After this sketch is on the
  board you should not need the bootloader double-tap. Wait until the LED
  goes off before taking a current trace.
- RGB LED driver is then powered down (`nicla::leds.end()`)
- LS/LDO is disabled (`nicla::disableLDO()`)
- Syntiant NDP120 is held in reset (`PORSTB` / `p18`), so the microphone,
  BMI270 and BMM150 stay off
- `NRF_UART0` / `NRF_UARTE0` are disabled; `Serial` is never started
- BLE starts once, non-connectable, local name `NiclaBeacon`

Then, forever:

1. Write a 4-byte little-endian burst index as manufacturer data (company
   `0xFFFF`)
2. `BLE.advertise()` → `BLE.poll(20)` → `BLE.stopAdvertise()`
3. Sleep 5 s (`ThisThread::sleep_for`)

ArduinoBLE has no “emit N PDUs then halt” command on nRF52832. The 20 ms
wait is longer than one 3-channel event (~1.5–3 ms) and shorter than the
100 ms minimum `ADV_NONCONN_IND` interval, so a second event should not
run. The stack is **not** torn down with `BLE.end()` each cycle, so the
current spike is the burst rather than a full HCI reset.

Measure at **VBAT / VIN**, not through USB. The SAMD11 USB bridge has its
own rail. `nicla::begin()` still kicks the PMIC over I2C about every 10 s;
that can show as a small extra blip, separate from the 5 s beacon.

## Listen on the PC

```bash
cd NiclaBleNus_LowPowerTest/tools
python -m venv .venv
# Windows Git Bash:
source .venv/Scripts/activate
pip install -r requirements.txt
python listen_adv.py
```

Optional filters:

```bash
python listen_adv.py --name NiclaBeacon
python listen_adv.py --address AA:BB:CC:DD:EE:FF
```

Each line is one scanner callback: timestamp, address, RSSI, name, burst
index. The adapter may report one callback per event, or miss a burst if it
is listening on the wrong advertising channel at that instant.

## Files

| File | Responsibility |
|---|---|
| `NiclaBleNus_LowPowerTest.ino` | Shutdown, one-shot advert, 5 s sleep |
| `BleNusConfig.h` | Beacon name, company ID, sleep and wait times |
| `tools/listen_adv.py` | Host-side advertisement logger |
| `BleNusPeripheral.*` and other NUS files | Unused at runtime (Arduino still compiles sketch `.cpp` files) |
