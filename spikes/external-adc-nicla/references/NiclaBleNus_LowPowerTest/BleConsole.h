#ifndef BLE_CONSOLE_H
#define BLE_CONSOLE_H

#include <Arduino.h>

#include "BleNusPeripheral.h"

// The "ble ..." operator commands, meant for the USB serial port only so that
// a remote central can never re-pair the board it is talking to.
//
// Pairing and unpairing throw away a stored peer, so both ask for a "ble yes"
// confirmation first. The confirmation never times out.
class BleConsole {
public:
    explicit BleConsole(BleNusPeripheral& peripheral) : _peripheral(peripheral) {}

    // Returns true if the line was addressed to this console, whether or not
    // the command itself was valid, so the caller can route anything else to
    // its own protocol.
    bool handleLine(const String& line);

private:
    enum class PendingConfirmation : uint8_t { None, Unpair, UnpairThenPair };

    void printHelp() const;
    void requestPairing();
    void requestUnpair();
    void confirmPending();
    void cancelPending();

    BleNusPeripheral&   _peripheral;
    PendingConfirmation _pending = PendingConfirmation::None;
};

#endif  // BLE_CONSOLE_H
