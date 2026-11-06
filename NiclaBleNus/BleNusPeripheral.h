#ifndef BLE_NUS_PERIPHERAL_H
#define BLE_NUS_PERIPHERAL_H

#include <Arduino.h>

#include "PairedPeerStorage.h"

// Where the board sits in the pairing and connection lifecycle.
enum class BleLinkState : uint8_t {
    Idle,         // no peer remembered and not pairing, so the radio stays quiet
    Pairing,      // advertising; the first central to connect becomes the peer
    Advertising,  // peer remembered; advertising, but only that peer is accepted
    Connected     // the remembered peer is connected and data may flow
};

// A BLE peripheral exposing a Nordic UART Service, restricted to the single
// central remembered in flash. The board is never a central.
class BleNusPeripheral {
public:
    // Starts the radio and the UART service, then advertises for the
    // remembered peer, or begins pairing when none is stored. Returns false if
    // the radio could not be started, leaving every data-path call inert.
    bool begin();

    // Services the radio and the connection policy. Call once every loop().
    void update();

    BleLinkState state() const { return _state; }
    bool isConnected() const { return _state == BleLinkState::Connected; }
    const char* deviceName() const { return _deviceName; }

    bool hasPairedPeer() const { return _peerStorage.hasPeer(); }
    const char* pairedPeerAddress() const { return _peerStorage.peerAddress(); }

    // Forgets any peer, then advertises and adopts the next central to connect.
    void startPairing();

    // Forgets the peer and stops advertising, leaving the radio quiet until
    // startPairing() or the next reboot.
    void forgetPeer();

    // Data path. Every call below is inert unless isConnected().
    int available();
    int read();

    // Copies one newline-terminated line, without its terminator, into buffer.
    // Returns false when no complete line is waiting.
    bool readLine(char* buffer, size_t size);

    void print(const char* text);
    void flush();

private:
    void handleCentralPresent();
    void handleCentralAbsent();

    void enterPairing();
    void enterAdvertising();
    void enterIdle();
    void enterConnected();

    void setAdvertising(bool enabled);
    void disconnectCentral();

    PairedPeerStorage _peerStorage;
    BleLinkState      _state                = BleLinkState::Idle;
    bool              _radioReady           = false;
    bool              _advertising          = false;
    bool              _centralWasPresent    = false;
    char              _deviceName[24]       = {};
};

#endif  // BLE_NUS_PERIPHERAL_H
