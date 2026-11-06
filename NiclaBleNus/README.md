# NiclaBleNus — BLE + Nordic UART skeleton for Arduino Nicla Voice

A working starting point for any Nicla Voice project that needs a wireless
text link. The radio, the Nordic UART Service, the single-peer pairing policy
and its flash persistence are already done. Copy the folder, rename it, and add
your own features.

Distilled from the BLE stack in `SCPI_SI5351_NICLA/Ble.cpp`, with the SCPI
parser, the Si5351 driver and the sensors left behind.

## Requirements

- Board: `arduino:mbed_nicla:nicla_voice`
- Libraries: ArduinoBLE, HardwareBLESerial

```bash
CLI="$LOCALAPPDATA/arduino-cli/arduino-cli.exe"
"$CLI" compile --fqbn arduino:mbed_nicla:nicla_voice NiclaBleNus
"$CLI" upload -p COMxx --fqbn arduino:mbed_nicla:nicla_voice NiclaBleNus
```

## Behaviour out of the box

The board advertises as `NiclaNus-AABBCC`, where `AABBCC` is the last three
bytes of its BLE MAC. On first boot it has no peer, so it enters pairing and
adopts the first central that connects, writing that address to flash. From
then on it advertises for that peer alone and rejects anyone else, across
reboots. Any line you send over the UART service is echoed back.

The RGB LED reports the link state:

| State | LED |
|---|---|
| Idle, no peer remembered | Off |
| Pairing | Blue, fast blink |
| Advertising for the peer | Blue, slow blink |
| Peer connected | Green, solid |

## Operator commands

Type these on the USB serial port at 115200 baud. They are deliberately not
reachable over BLE, so a connected central cannot re-pair the board underneath
itself. `ble pair` and `ble unpair` discard a stored peer, so both ask for
confirmation; the prompt does not time out.

| Command | Effect |
|---|---|
| `ble help` | List the commands and show the current peer |
| `ble pair` | Forget any peer and adopt the next central to connect |
| `ble unpair` | Forget the peer and stop advertising |
| `ble yes` | Confirm a pending pair or unpair |
| `ble no` | Cancel a pending pair or unpair |

## Files

| File | Responsibility |
|---|---|
| `NiclaBleNus.ino` | Wiring plus the application hooks you fill in |
| `BleNusConfig.h` | The settings a new project changes |
| `BleNusPeripheral.*` | Radio, UART service, pairing and connection policy |
| `PairedPeerStorage.*` | The peer address in its own flash sector |
| `BleConsole.*` | The `ble ...` operator commands |
| `BleStatusLed.*` | Link state on the RGB LED |
| `SerialLineReader.*` | Newline-terminated lines from a byte stream |

## Starting a new project

1. Copy the folder and rename it together with the `.ino`, since the Arduino
   IDE requires the two to match.
2. In `BleNusConfig.h`, set `DEVICE_NAME_PREFIX` to your project's name and
   give `PEER_STORAGE_MAGIC` a fresh value. A distinct magic means that
   flashing this project over a different one reads the old peer record as
   invalid instead of misinterpreting its layout.
3. Fill in `startApplication()` and `serviceApplication()` in the `.ino`, and
   replace the echo in `handleBleLine()` with your own protocol.

`serviceApplication()` runs inside `loop()`, so keep it non-blocking: the
radio only progresses while `BleNusPeripheral::update()` is being called.

`BleStatusLed` is presentation only. A project that wants the LED for
something else can delete both files and drop the `statusLed` lines from the
`.ino`.

## Flash layout

`PairedPeerStorage` claims the **third-to-last** flash sector, located at
runtime rather than hard-coded. That choice is not arbitrary: probing this
board (`spikes/ble-nus/flash_sector_probe`) found the second-to-last sector
already holding foreign non-`0xFF` data that must not be erased. The last
sector is left free for your own settings, which is where
`SCPI_SI5351_NICLA` keeps its Si5351 configuration.

If a future board revision behaves differently, re-run that probe before
moving the record.

## Known limitation

The link is open: there is no BLE passkey and no stack-level bonding. The peer
allowlist is enforced by this application after connection, not by the radio,
and the pairing window accepts whoever connects first. Windows may also present
a resolvable private address that changes over time, which would stop an
allowlisted peer from reconnecting; that has not been observed on this board,
and the fix belongs with proper passkey pairing.
