#include "Ble.h"

#include <ArduinoBLE.h>
#include <HardwareBLESerial.h>
#include <Nicla_System.h>
#include <ctype.h>
#include <string.h>

#include "FlashIAP.h"

// Pairing wizard copied from SCPI_SI5351_NICLA/Ble.cpp. Local name: NusSpike.
// §5.2.5 / §5.2.6
static constexpr uint32_t BLE_NVM_MAGIC     = 0x5351B001U;
static constexpr uint32_t BLE_LED_FAST_MS   = 200;
static constexpr uint32_t BLE_LED_SLOW_MS   = 1000;

static HardwareBLESerial& bleSerial = HardwareBLESerial::getInstance();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool iequals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        const char ca = static_cast<char>(tolower(static_cast<unsigned char>(*a)));
        const char cb = static_cast<char>(tolower(static_cast<unsigned char>(*b)));
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool isBleKeywordLine(const String& line) {
    // "ble" case-insensitive, then EOL or whitespace (§5.2.4 / §5.2.8 Stage 4).
    if (line.length() < 3) return false;
    char c0 = line.charAt(0), c1 = line.charAt(1), c2 = line.charAt(2);
    if (tolower(static_cast<unsigned char>(c0)) != 'b') return false;
    if (tolower(static_cast<unsigned char>(c1)) != 'l') return false;
    if (tolower(static_cast<unsigned char>(c2)) != 'e') return false;
    if (line.length() == 3) return true;
    const char c3 = line.charAt(3);
    return c3 == ' ' || c3 == '\t';
}

static uint32_t bleNvmAddr(mbed::FlashIAP& flash) {
    // Third-to-last sector: si5351_addr − 2×sector_size (§5.2.5).
    const uint32_t start    = flash.get_flash_start();
    const uint32_t total    = flash.get_flash_size();
    const uint32_t sectorSz = flash.get_sector_size(start + total - 1);
    return start + total - 3U * sectorSz;
}

// ---------------------------------------------------------------------------
// BleManager
// ---------------------------------------------------------------------------
BleManager::BleManager()
    : _state(State::IdleUnpaired),
      _pending(Pending::None),
      _radioOk(false),
      _advertising(false),
      _scpiLinkUp(false),
      _wasLinked(false),
      _ledLastMs(0),
      _ledPhaseOn(false),
      _ledR(0),
      _ledG(0),
      _ledB(0) {
    memset(&_nvm, 0, sizeof(_nvm));
    _localName[0] = '\0';
    nvmDefaults();
}

void BleManager::nvmDefaults() {
    memset(&_nvm, 0, sizeof(_nvm));
    _nvm.magic     = 0;
    _nvm.pinLen    = 4;
    _nvm.pin[0]    = '0';
    _nvm.pin[1]    = '0';
    _nvm.pin[2]    = '0';
    _nvm.pin[3]    = '0';
    _nvm.peerValid = 0;
}

bool BleManager::nvmLoad() {
    mbed::FlashIAP flash;
    flash.init();
    const uint32_t addr = bleNvmAddr(flash);
    BleNvmData tmp;
    flash.read(&tmp, addr, sizeof(tmp));
    flash.deinit();
    if (tmp.magic != BLE_NVM_MAGIC) return false;
    if (tmp.pinLen < 4 || tmp.pinLen > 6) return false;
    _nvm = tmp;
    return true;
}

bool BleManager::nvmSave() {
    mbed::FlashIAP flash;
    flash.init();
    const uint32_t addr     = bleNvmAddr(flash);
    const uint32_t sectorSz = flash.get_sector_size(addr);
    const uint32_t pageSz   = flash.get_page_size();

    BleNvmData next = _nvm;
    next.magic      = BLE_NVM_MAGIC;

    BleNvmData current;
    flash.read(&current, addr, sizeof(current));
    if (memcmp(&current, &next, sizeof(BleNvmData)) == 0) {
        flash.deinit();
        return false;
    }

    const uint32_t writeLen =
        ((sizeof(BleNvmData) + pageSz - 1) / pageSz) * pageSz;
    uint8_t buf[64] = {};
    memcpy(buf, &next, sizeof(BleNvmData));

    flash.erase(addr, sectorSz);
    flash.program(buf, addr, writeLen);
    flash.deinit();

    _nvm = next;
    return true;
}

void BleManager::deriveLocalName(const String& mac) {
    char hex[13];
    int n = 0;
    for (unsigned i = 0; i < mac.length() && n < 12; i++) {
        char c = mac.charAt(i);
        if (c == ':') continue;
        if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
        hex[n++] = c;
    }
    hex[n] = '\0';
    const char* last3 = (n >= 6) ? (hex + (n - 6)) : hex;
    snprintf(_localName, sizeof(_localName), "NusSpike-%s", last3);
}

void BleManager::setAdvertising(bool on) {
    if (!_radioOk) return;
    if (on == _advertising) return;
    if (on) {
        BLE.advertise();
        _advertising = true;
        Serial.println("BLE: advertising on");
    } else {
        BLE.stopAdvertise();
        _advertising = false;
        Serial.println("BLE: advertising off");
    }
}

void BleManager::disconnectCentral() {
    if (!_radioOk) return;
    BLEDevice central = BLE.central();
    if (central) {
        central.disconnect();
    }
    _scpiLinkUp = false;
}

void BleManager::enterPairing() {
    disconnectCentral();
    _state = State::Pairing;
    setAdvertising(true);
    Serial.println("BLE: pair mode — connect a central to store peer");
}

void BleManager::enterIdleUnpaired() {
    disconnectCentral();
    _state = State::IdleUnpaired;
    setAdvertising(false);
    Serial.println("BLE: unpaired — advertising stopped");
}

void BleManager::enterPairedAdv() {
    _state = State::PairedAdv;
    setAdvertising(true);
    Serial.print("BLE: paired — allowlist ");
    Serial.println(_nvm.peerAddr);
}

bool BleManager::peerMatches(const String& addr) const {
    if (!_nvm.peerValid) return false;
    return iequals(_nvm.peerAddr, addr.c_str());
}

bool BleManager::storePeer(const String& addr) {
    if (addr.length() == 0 || addr.length() > 17) return false;
    memset(_nvm.peerAddr, 0, sizeof(_nvm.peerAddr));
    addr.toCharArray(_nvm.peerAddr, sizeof(_nvm.peerAddr));
    _nvm.peerValid = 1;
    nvmSave();
    Serial.print("BLE: peer stored ");
    Serial.println(_nvm.peerAddr);
    return true;
}

bool BleManager::clearPeer() {
    if (!_nvm.peerValid && _nvm.peerAddr[0] == '\0') {
        return false;
    }
    _nvm.peerValid = 0;
    memset(_nvm.peerAddr, 0, sizeof(_nvm.peerAddr));
    nvmSave();
    Serial.println("BLE: peer cleared");
    return true;
}

void BleManager::setLedColor(uint8_t r, uint8_t g, uint8_t b) {
    if (r == _ledR && g == _ledG && b == _ledB) return;
    _ledR = r;
    _ledG = g;
    _ledB = b;
    nicla::leds.setColor(r, g, b);
}

void BleManager::applyLed(bool i2cFaulted) {
    if (i2cFaulted) {
        setLedColor(255, 0, 0);
        return;
    }

    if (_state == State::Connected && _scpiLinkUp) {
        setLedColor(0, 255, 0);
        return;
    }

    if (_state == State::IdleUnpaired || !_radioOk) {
        setLedColor(0, 0, 0);
        return;
    }

    // Pairing = fast blue blink; PairedAdv = slow blue blink.
    const uint32_t period =
        (_state == State::Pairing) ? BLE_LED_FAST_MS : BLE_LED_SLOW_MS;
    const uint32_t now = millis();
    if (now - _ledLastMs >= period) {
        _ledLastMs = now;
        _ledPhaseOn = !_ledPhaseOn;
    }
    if (_ledPhaseOn) {
        setLedColor(0, 0, 255);
    } else {
        setLedColor(0, 0, 0);
    }
}

bool BleManager::begin() {
    nvmDefaults();
    if (!nvmLoad()) {
        Serial.println("BLE: NVM empty/invalid — defaults (PIN 0000, no peer)");
        nvmDefaults();
    } else {
        Serial.println("BLE: NVM loaded");
    }

    if (!BLE.begin()) {
        Serial.println("FAIL: BLE.begin() — BLE disabled; UART SCPI still works");
        _radioOk = false;
        _state   = State::IdleUnpaired;
        return false;
    }
    _radioOk = true;

    const String mac = BLE.address();
    deriveLocalName(mac);
    BLE.setLocalName(_localName);
    BLE.setDeviceName(_localName);

    bleSerial.begin();

    Serial.print("BLE: MAC=");
    Serial.print(mac);
    Serial.print(" NAME=");
    Serial.println(_localName);

    if (_nvm.peerValid) {
        enterPairedAdv();
    } else {
        enterPairing();
    }
    return true;
}

void BleManager::loop(bool i2cFaulted) {
    if (_radioOk) {
        bleSerial.poll();

        const bool linked = BLE.connected();
        if (linked) {
            BLEDevice central = BLE.central();
            const String addr = central ? central.address() : String();

            if (_state == State::Pairing) {
                if (addr.length() > 0) {
                    storePeer(addr);
                    _state      = State::Connected;
                    _scpiLinkUp = true;
                    _advertising = false;  // SoftDevice stops adv on connect
                    Serial.print("BLE: connected (paired) central=");
                    Serial.println(addr);
                }
            } else if (_state == State::PairedAdv || _state == State::Connected) {
                if (!peerMatches(addr)) {
                    Serial.print("BLE: reject central=");
                    Serial.println(addr);
                    if (central) central.disconnect();
                    _scpiLinkUp = false;
                    // After disconnect, next polls restore PairedAdv + adv.
                    _state = State::PairedAdv;
                } else {
                    if (!_scpiLinkUp || !_wasLinked) {
                        Serial.print("BLE: connected central=");
                        Serial.println(addr);
                    }
                    _state      = State::Connected;
                    _scpiLinkUp = true;
                    _advertising = false;
                }
            } else {
                // IdleUnpaired should not advertise; drop surprise links.
                if (central) central.disconnect();
                _scpiLinkUp = false;
            }
        } else {
            if (_wasLinked) {
                Serial.println("BLE: disconnected");
            }
            _scpiLinkUp = false;
            if (_state == State::Connected) {
                if (_nvm.peerValid) {
                    enterPairedAdv();
                } else {
                    enterIdleUnpaired();
                }
            } else if (_state == State::PairedAdv && !_advertising) {
                setAdvertising(true);
            } else if (_state == State::Pairing && !_advertising) {
                setAdvertising(true);
            }
        }
        _wasLinked = linked;
    }

    applyLed(i2cFaulted);
}

void BleManager::printHelp() const {
    Serial.println("BLE commands (USB-CDC only):");
    Serial.println("  ble help");
    Serial.println("  ble pin <4-6 digits>");
    Serial.println("  ble pair");
    Serial.println("  ble unpair");
    Serial.println("  ble yes");
    Serial.println("  ble no");
}

bool BleManager::cmdPin(const String& digits) {
    const unsigned len = digits.length();
    if (len < 4 || len > 6) {
        Serial.println("BLE: PIN must be 4–6 decimal digits");
        return false;
    }
    for (unsigned i = 0; i < len; i++) {
        if (!isdigit(static_cast<unsigned char>(digits.charAt(i)))) {
            Serial.println("BLE: PIN must be 4–6 decimal digits");
            return false;
        }
    }
    memset(_nvm.pin, 0, sizeof(_nvm.pin));
    digits.toCharArray(_nvm.pin, sizeof(_nvm.pin));
    _nvm.pinLen = static_cast<uint8_t>(len);
    nvmSave();
    Serial.print("BLE: PIN stored (");
    Serial.print(_nvm.pinLen);
    Serial.println(" digits) — not used on air until passkey work");
    return true;
}

void BleManager::cmdPair() {
    if (_nvm.peerValid) {
        _pending = Pending::UnpairThenPair;
        Serial.println(
            "BLE: peer stored — confirm unpair then pair? Type 'ble yes' or 'ble no'.");
        return;
    }
    _pending = Pending::None;
    enterPairing();
}

void BleManager::cmdUnpair() {
    if (!_nvm.peerValid) {
        Serial.println("BLE: no peer paired.");
        return;
    }
    _pending = Pending::Unpair;
    Serial.println("BLE: confirm unpair? Type 'ble yes' or 'ble no'.");
}

void BleManager::cmdYes() {
    if (_pending == Pending::None) {
        Serial.println("BLE: nothing to confirm.");
        return;
    }
    const Pending p = _pending;
    _pending = Pending::None;
    clearPeer();
    if (p == Pending::UnpairThenPair) {
        enterPairing();
    } else {
        enterIdleUnpaired();
    }
}

void BleManager::cmdNo() {
    if (_pending == Pending::None) {
        Serial.println("BLE: nothing to cancel.");
        return;
    }
    _pending = Pending::None;
    Serial.println("BLE: cancelled.");
}

bool BleManager::handleSerialCommand(const String& line) {
    if (!isBleKeywordLine(line)) return false;

    String rest = (line.length() > 3) ? line.substring(3) : String("");
    rest.trim();
    rest.toLowerCase();

    if (rest.length() == 0 || rest == "help") {
        printHelp();
        return true;
    }
    if (rest.startsWith("pin ")) {
        String digits = rest.substring(4);
        digits.trim();
        cmdPin(digits);
        return true;
    }
    if (rest == "pin") {
        Serial.println("BLE: usage: ble pin <4-6 digits>");
        return true;
    }
    if (rest == "pair") {
        cmdPair();
        return true;
    }
    if (rest == "unpair") {
        cmdUnpair();
        return true;
    }
    if (rest == "yes") {
        cmdYes();
        return true;
    }
    if (rest == "no") {
        cmdNo();
        return true;
    }

    Serial.println("BLE: unknown command — ble help");
    return true;
}
