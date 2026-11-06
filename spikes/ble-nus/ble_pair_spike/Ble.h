#ifndef BLE_H
#define BLE_H

// Pairing wizard copied from SCPI_SI5351_NICLA/Ble.h for this throwaway spike.
// No SCPI transport.

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Flash layout (third-to-last sector; magic 0x5351B001) — §5.2.5
// ---------------------------------------------------------------------------
struct __attribute__((packed)) BleNvmData {
    uint32_t magic;        // 0x5351B001 when valid
    uint8_t  pinLen;       // 4..6
    char     pin[6];       // ASCII digits; unused tail = 0
    uint8_t  peerValid;    // 0 = none, 1 = peerAddr valid
    char     peerAddr[18]; // "aa:bb:cc:dd:ee:ff" + NUL
};                         // 30 B

// ---------------------------------------------------------------------------
// BleManager — NVM, radio, allowlist, UART "ble …", LED contribution
// ---------------------------------------------------------------------------
class BleManager {
public:
    BleManager();

    // Init ArduinoBLE + HardwareBLESerial; load NVM; enter PAIRING or PAIRED_ADV.
    bool begin();

    // Poll SoftDevice, enforce allowlist, refresh advertising / LED phase.
    void loop(bool i2cFaulted);

    // USB-CDC only. Returns true if line was a "ble …" command.
    bool handleSerialCommand(const String& line);

    // True while an allowlisted central is connected.
    bool scpiLinkUp() const { return _scpiLinkUp; }

private:
    enum class State : uint8_t {
        IdleUnpaired,  // no peer, not advertising
        Pairing,       // accept first central → store → Connected
        PairedAdv,     // peer stored, advertising, allowlist
        Connected      // allowlisted central connected
    };

    enum class Pending : uint8_t {
        None,
        UnpairThenPair,
        Unpair
    };

    BleNvmData         _nvm;
    State              _state;
    Pending            _pending;
    bool               _radioOk;
    bool               _advertising;
    bool               _scpiLinkUp;
    bool               _wasLinked;
    char               _localName[24];

    uint32_t           _ledLastMs;
    bool               _ledPhaseOn;
    uint8_t            _ledR, _ledG, _ledB;  // last applied colour

    bool nvmLoad();
    bool nvmSave();
    void nvmDefaults();

    void deriveLocalName(const String& mac);
    void setAdvertising(bool on);
    void disconnectCentral();
    void enterPairing();
    void enterIdleUnpaired();
    void enterPairedAdv();

    bool peerMatches(const String& addr) const;
    bool storePeer(const String& addr);
    bool clearPeer();

    void applyLed(bool i2cFaulted);
    void setLedColor(uint8_t r, uint8_t g, uint8_t b);

    void printHelp() const;
    bool cmdPin(const String& digits);
    void cmdPair();
    void cmdUnpair();
    void cmdYes();
    void cmdNo();
};

#endif // BLE_H
