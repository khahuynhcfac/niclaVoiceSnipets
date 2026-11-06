#ifndef BLE_NUS_CONFIG_H
#define BLE_NUS_CONFIG_H

#include <Arduino.h>

// Everything a new project is expected to change lives here.
namespace BleNusConfig {

// Beacon local name used by this low-power advertising test (no connection).
constexpr char BEACON_NAME[] = "NiclaBeacon";

// 0xFFFF is the experimental / unassigned Bluetooth company identifier.
constexpr uint16_t BEACON_COMPANY_ID = 0xFFFF;

// Time from radio-off to the next advertising burst. The nRF52832 RTC wakes
// from System ON idle; System OFF cannot wake on a timer.
constexpr uint32_t BEACON_SLEEP_MS = 1000;

// Wait after BLE.advertise() before stopAdvertise(). Must be longer than one
// 3-channel event and shorter than the 100 ms ADV_NONCONN_IND interval.
constexpr uint32_t BEACON_EVENT_WAIT_MS = 20;

// CPU stays fully awake after reset so SWD can connect without a bootloader
// double-tap. delay() on purpose: do not sleep here. LED is on during this
// window, then powered down with the rest of the unused peripherals.
constexpr uint32_t BEACON_BOOT_UNLOCK_MS = 10000;

// The advertised name becomes "<prefix>-AABBCC", where AABBCC is the last
// three bytes of the board's BLE MAC. Unused while this sketch is a beacon.
constexpr char DEVICE_NAME_PREFIX[] = "NiclaNus";

// Identifies this project's peer record in flash. Give each project its own
// value so that reflashing one board with another project never reads a
// foreign record as valid.
constexpr uint32_t PEER_STORAGE_MAGIC = 0x4E555301U;

constexpr uint32_t PAIRING_BLINK_PERIOD_MS     = 200;
constexpr uint32_t ADVERTISING_BLINK_PERIOD_MS = 1000;

constexpr unsigned long USB_SERIAL_BAUD = 115200;

constexpr size_t MAX_BLE_LINE_LENGTH = 256;

}  // namespace BleNusConfig

#endif  // BLE_NUS_CONFIG_H
