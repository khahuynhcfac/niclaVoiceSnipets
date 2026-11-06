#include "PairedPeerStorage.h"

#include <string.h>

#include "BleNusConfig.h"
#include "FlashIAP.h"

namespace {

// The record lives in the third-to-last flash sector. On the Nicla Voice the
// second-to-last sector already holds foreign non-0xFF data and must not be
// erased, and the last sector is left free for application settings.
uint32_t recordAddress(mbed::FlashIAP& flash) {
    const uint32_t flashStart = flash.get_flash_start();
    const uint32_t flashSize  = flash.get_flash_size();
    const uint32_t sectorSize = flash.get_sector_size(flashStart + flashSize - 1);
    return flashStart + flashSize - 3 * sectorSize;
}

}  // namespace

bool PairedPeerStorage::load() {
    mbed::FlashIAP flash;
    flash.init();
    Record stored;
    flash.read(&stored, recordAddress(flash), sizeof(stored));
    flash.deinit();

    if (stored.magic != BleNusConfig::PEER_STORAGE_MAGIC) {
        _record = Record{};
        return false;
    }
    _record = stored;
    return true;
}

bool PairedPeerStorage::matches(const String& address) const {
    return hasPeer() && address.equalsIgnoreCase(_record.peerAddress);
}

bool PairedPeerStorage::rememberPeer(const String& address) {
    if (address.length() == 0 || address.length() >= sizeof(_record.peerAddress)) {
        return false;
    }
    memset(_record.peerAddress, 0, sizeof(_record.peerAddress));
    address.toCharArray(_record.peerAddress, sizeof(_record.peerAddress));
    _record.peerValid = 1;
    return writeRecord();
}

bool PairedPeerStorage::forgetPeer() {
    _record.peerValid = 0;
    memset(_record.peerAddress, 0, sizeof(_record.peerAddress));
    return writeRecord();
}

bool PairedPeerStorage::writeRecord() {
    Record next = _record;
    next.magic  = BleNusConfig::PEER_STORAGE_MAGIC;

    mbed::FlashIAP flash;
    flash.init();
    const uint32_t address = recordAddress(flash);

    Record onFlash;
    flash.read(&onFlash, address, sizeof(onFlash));
    if (memcmp(&onFlash, &next, sizeof(Record)) == 0) {
        flash.deinit();
        _record = next;
        return true;
    }

    // Flash programs whole pages, so pad the record out to a page boundary.
    const uint32_t pageSize    = flash.get_page_size();
    const uint32_t writeLength = ((sizeof(Record) + pageSize - 1) / pageSize) * pageSize;
    uint8_t paddedRecord[64]   = {};
    static_assert(sizeof(Record) <= sizeof(paddedRecord),
                  "Record outgrew the padding buffer");

    memcpy(paddedRecord, &next, sizeof(Record));
    flash.erase(address, flash.get_sector_size(address));
    const int programResult = flash.program(paddedRecord, address, writeLength);
    flash.deinit();

    _record = next;
    return programResult == 0;
}
