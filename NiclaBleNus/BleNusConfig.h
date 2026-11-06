#ifndef BLE_NUS_CONFIG_H
#define BLE_NUS_CONFIG_H

#include <Arduino.h>

// Everything a new project is expected to change lives here.
namespace BleNusConfig {

// The advertised name becomes "<prefix>-AABBCC", where AABBCC is the last
// three bytes of the board's BLE MAC.
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
