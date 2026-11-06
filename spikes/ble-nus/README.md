# BLE NUS spike (`spikes/ble-nus`)

Throwaway proofs for §5.2.8 of `SCPI_SI5351_NICLA/docs/SCPI_SI5351_NICLA.md`.
Nothing here is product firmware. Record findings in `RESULTS.md`.

Host-side BLE serial console / bridge lives at repo-root **`ble-serial-app/`**
(peer of `SCPI_SI5351_NICLA/`), not under this spike.

## Tools

```bash
CLI="$LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
FQBN=arduino:mbed_nicla:nicla_voice
```

Libraries (already expected): ArduinoBLE, HardwareBLESerial.

## Nicla — echo spike

```bash
"$CLI" compile --fqbn $FQBN spikes/ble-nus/ble_echo_spike
"$CLI" board list
"$CLI" upload -p COMxx --fqbn $FQBN spikes/ble-nus/ble_echo_spike
```

USB Serial Monitor @ 115200. Commands: `mac`, `info`, `adv on`, `adv off`, `heap`.
BLE: send a `\n`-terminated line; board replies `ECHO:<line>\n`.

Open NUS, no pairing. Product firmware adds the USB `ble …` wizard on top of
this stack.

## Nicla — pair + NUS spike

Same NUS echo as above, plus the product pairing wizard (`Ble.cpp` from
`SCPI_SI5351_NICLA` / Energize RX). Local name `NusSpike-AABBCC`.

```bash
"$CLI" compile --fqbn $FQBN spikes/ble-nus/ble_pair_spike
"$CLI" upload -p COMxx --fqbn $FQBN spikes/ble-nus/ble_pair_spike
```

USB: `ble help` / `ble pair` / `ble unpair` / `ble yes` / `ble no` / `ble pin`.
NUS echo only while an allowlisted central is connected.

## Nicla — flash sector probe

```bash
"$CLI" compile --fqbn $FQBN spikes/ble-nus/flash_sector_probe
"$CLI" upload -p COMxx --fqbn $FQBN spikes/ble-nus/flash_sector_probe
```

USB commands: `map`, `read`, `write`, `checksi`.
`write` refuses unless the BLE sector is blank (`0xFF`) or already carries magic `0x5351B001`.

Verification sequence (§5.2.8):

1. `map`, `write`, reset, `read` — pattern must survive.
2. Flash real `SCPI_SI5351_NICLA`, store a frequency, reboot, confirm Si5351 setting.
3. Reflash this probe, `read` — note whether the BLE sector survived the upload.

## PC host (see `ble-serial-app/`)

Install signed com0com if needed, then use the Qt console / `echo_probe.py` in
`ble-serial-app/` (venv, `ble-serial`, PyInstaller). End-to-end notes stay in
this folder’s `RESULTS.md`.

## Known outcomes (see RESULTS.md)

- HardwareBLESerial **compiles** on mbed_nicla 4.6.0 + ArduinoBLE 2.1.0.
- BLE NVM is **third-to-last** flash sector (§5.2.5 updated); second-to-last must not be erased.
- com0com + ble-serial COM path **verified** (`echo_probe.py` all PASS including 200-char lines).
