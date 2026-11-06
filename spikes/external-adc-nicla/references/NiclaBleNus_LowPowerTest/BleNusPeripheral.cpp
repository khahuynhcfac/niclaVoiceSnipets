#include "BleNusPeripheral.h"

#include <ArduinoBLE.h>
#include <HardwareBLESerial.h>
#include <ctype.h>
#include <stdio.h>

#include "BleNusConfig.h"

static HardwareBLESerial& nusSerial = HardwareBLESerial::getInstance();

namespace {

void logEvent(const char* message) {
    Serial.print("BLE: ");
    Serial.println(message);
}

void logEvent(const char* message, const String& detail) {
    Serial.print("BLE: ");
    Serial.print(message);
    Serial.print(' ');
    Serial.println(detail);
}

void buildDeviceName(const String& macAddress, char* name, size_t size) {
    char hexDigits[13] = {};
    size_t digitCount  = 0;
    for (unsigned i = 0; i < macAddress.length() && digitCount < sizeof(hexDigits) - 1; i++) {
        const char c = macAddress.charAt(i);
        if (c == ':') continue;
        hexDigits[digitCount++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
    const char* lastThreeBytes = (digitCount >= 6) ? hexDigits + digitCount - 6 : hexDigits;
    snprintf(name, size, "%s-%s", BleNusConfig::DEVICE_NAME_PREFIX, lastThreeBytes);
}

}  // namespace

bool BleNusPeripheral::begin() {
    logEvent(_peerStorage.load() ? "peer record loaded from flash"
                                 : "no peer record in flash");

    if (!BLE.begin()) {
        logEvent("radio unavailable, BLE disabled");
        _radioReady = false;
        _state      = BleLinkState::Idle;
        return false;
    }
    _radioReady = true;

    const String macAddress = BLE.address();
    buildDeviceName(macAddress, _deviceName, sizeof(_deviceName));
    BLE.setLocalName(_deviceName);
    BLE.setDeviceName(_deviceName);
    nusSerial.begin();

    logEvent("address", macAddress);
    logEvent("device name", _deviceName);

    if (_peerStorage.hasPeer()) {
        enterAdvertising();
    } else {
        enterPairing();
    }
    return true;
}

void BleNusPeripheral::update() {
    if (!_radioReady) return;

    nusSerial.poll();

    const bool centralPresent = BLE.connected();
    if (centralPresent) {
        handleCentralPresent();
    } else {
        handleCentralAbsent();
    }
    _centralWasPresent = centralPresent;
}

void BleNusPeripheral::handleCentralPresent() {
    BLEDevice central     = BLE.central();
    const String address  = central ? central.address() : String();

    if (_state == BleLinkState::Pairing) {
        // The address can take a few polls to become readable; wait for it.
        if (address.length() == 0) return;
        _peerStorage.rememberPeer(address);
        logEvent("paired with", address);
        enterConnected();
        return;
    }

    if (_state == BleLinkState::Idle || !_peerStorage.matches(address)) {
        logEvent("rejecting central", address);
        if (central) central.disconnect();
        _state = _peerStorage.hasPeer() ? BleLinkState::Advertising : BleLinkState::Idle;
        return;
    }

    if (_state != BleLinkState::Connected) logEvent("connected to", address);
    enterConnected();
}

void BleNusPeripheral::handleCentralAbsent() {
    if (_centralWasPresent) logEvent("disconnected");

    if (_state == BleLinkState::Connected) {
        if (_peerStorage.hasPeer()) {
            enterAdvertising();
        } else {
            enterIdle();
        }
    } else if (_state != BleLinkState::Idle) {
        // Re-arm after a connection attempt we rejected took advertising down.
        setAdvertising(true);
    }
}

void BleNusPeripheral::startPairing() {
    _peerStorage.forgetPeer();
    enterPairing();
}

void BleNusPeripheral::forgetPeer() {
    _peerStorage.forgetPeer();
    enterIdle();
}

void BleNusPeripheral::enterPairing() {
    disconnectCentral();
    _state = BleLinkState::Pairing;
    setAdvertising(true);
    logEvent("pairing, connect a central to claim this board");
}

void BleNusPeripheral::enterAdvertising() {
    _state = BleLinkState::Advertising;
    setAdvertising(true);
    logEvent("advertising for peer", _peerStorage.peerAddress());
}

void BleNusPeripheral::enterIdle() {
    disconnectCentral();
    _state = BleLinkState::Idle;
    setAdvertising(false);
    logEvent("idle, no peer remembered");
}

void BleNusPeripheral::enterConnected() {
    _state       = BleLinkState::Connected;
    _advertising = false;  // the SoftDevice stops advertising once connected
}

void BleNusPeripheral::setAdvertising(bool enabled) {
    if (!_radioReady || enabled == _advertising) return;
    if (enabled) {
        BLE.advertise();
    } else {
        BLE.stopAdvertise();
    }
    _advertising = enabled;
    logEvent(enabled ? "advertising started" : "advertising stopped");
}

void BleNusPeripheral::disconnectCentral() {
    if (!_radioReady) return;
    BLEDevice central = BLE.central();
    if (central) central.disconnect();
}

int BleNusPeripheral::available() {
    return isConnected() ? static_cast<int>(nusSerial.available()) : 0;
}

int BleNusPeripheral::read() {
    return isConnected() ? nusSerial.read() : -1;
}

bool BleNusPeripheral::readLine(char* buffer, size_t size) {
    if (!isConnected() || nusSerial.availableLines() == 0) return false;
    nusSerial.readLine(buffer, size);
    return true;
}

void BleNusPeripheral::print(const char* text) {
    if (isConnected() && text) nusSerial.print(text);
}

void BleNusPeripheral::flush() {
    if (isConnected()) nusSerial.flush();
}
