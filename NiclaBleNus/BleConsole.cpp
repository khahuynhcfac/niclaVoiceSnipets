#include "BleConsole.h"

namespace {

constexpr unsigned KEYWORD_LENGTH = 3;  // "ble"

bool startsWithBleKeyword(const String& line) {
    if (line.length() < KEYWORD_LENGTH) return false;
    if (!line.substring(0, KEYWORD_LENGTH).equalsIgnoreCase("ble")) return false;
    if (line.length() == KEYWORD_LENGTH) return true;

    const char afterKeyword = line.charAt(KEYWORD_LENGTH);
    return afterKeyword == ' ' || afterKeyword == '\t';
}

}  // namespace

bool BleConsole::handleLine(const String& line) {
    if (!startsWithBleKeyword(line)) return false;

    String command = line.substring(KEYWORD_LENGTH);
    command.trim();
    command.toLowerCase();

    if (command.length() == 0 || command == "help") {
        printHelp();
    } else if (command == "pair") {
        requestPairing();
    } else if (command == "unpair") {
        requestUnpair();
    } else if (command == "yes") {
        confirmPending();
    } else if (command == "no") {
        cancelPending();
    } else {
        Serial.println("BLE: unknown command, try 'ble help'");
    }
    return true;
}

void BleConsole::printHelp() const {
    Serial.println("BLE commands (USB serial only):");
    Serial.println("  ble help    show this list");
    Serial.println("  ble pair    forget any peer and adopt the next central");
    Serial.println("  ble unpair  forget the peer and stop advertising");
    Serial.println("  ble yes     confirm a pending pair or unpair");
    Serial.println("  ble no      cancel a pending pair or unpair");

    if (_peripheral.hasPairedPeer()) {
        Serial.print("BLE: paired with ");
        Serial.println(_peripheral.pairedPeerAddress());
    } else {
        Serial.println("BLE: no peer paired");
    }
}

void BleConsole::requestPairing() {
    if (!_peripheral.hasPairedPeer()) {
        _pending = PendingConfirmation::None;
        _peripheral.startPairing();
        return;
    }
    _pending = PendingConfirmation::UnpairThenPair;
    Serial.print("BLE: pairing will forget ");
    Serial.print(_peripheral.pairedPeerAddress());
    Serial.println(". Confirm with 'ble yes' or 'ble no'.");
}

void BleConsole::requestUnpair() {
    if (!_peripheral.hasPairedPeer()) {
        Serial.println("BLE: no peer paired");
        return;
    }
    _pending = PendingConfirmation::Unpair;
    Serial.print("BLE: forget ");
    Serial.print(_peripheral.pairedPeerAddress());
    Serial.println("? Confirm with 'ble yes' or 'ble no'.");
}

void BleConsole::confirmPending() {
    const PendingConfirmation confirmed = _pending;
    _pending = PendingConfirmation::None;

    switch (confirmed) {
        case PendingConfirmation::UnpairThenPair:
            _peripheral.startPairing();
            break;
        case PendingConfirmation::Unpair:
            _peripheral.forgetPeer();
            break;
        case PendingConfirmation::None:
            Serial.println("BLE: nothing to confirm");
            break;
    }
}

void BleConsole::cancelPending() {
    if (_pending == PendingConfirmation::None) {
        Serial.println("BLE: nothing to cancel");
        return;
    }
    _pending = PendingConfirmation::None;
    Serial.println("BLE: cancelled");
}
