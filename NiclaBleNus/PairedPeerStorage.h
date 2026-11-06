#ifndef PAIRED_PEER_STORAGE_H
#define PAIRED_PEER_STORAGE_H

#include <Arduino.h>

// Remembers, across power cycles, the one BLE central this board will talk to.
//
// The address is kept as the text ArduinoBLE reports ("aa:bb:cc:dd:ee:ff"), so
// the allowlist check is a plain case-insensitive compare with no byte-order
// ambiguity.
class PairedPeerStorage {
public:
    // Reads the record from flash. Returns false when nothing valid is stored,
    // in which case there is no peer and the caller should offer pairing.
    bool load();

    bool hasPeer() const { return _record.peerValid != 0; }
    const char* peerAddress() const { return _record.peerAddress; }
    bool matches(const String& address) const;

    bool rememberPeer(const String& address);
    bool forgetPeer();

private:
    struct __attribute__((packed)) Record {
        uint32_t magic;
        uint8_t  peerValid;
        char     peerAddress[18];  // "aa:bb:cc:dd:ee:ff" + NUL
    };

    bool writeRecord();

    Record _record = {};
};

#endif  // PAIRED_PEER_STORAGE_H
