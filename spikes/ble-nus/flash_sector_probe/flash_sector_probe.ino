// Stage 0A-4 — flash sector probe (throwaway, no BLE).
// Verifies third-to-last flash sector for BLE NVM (§5.2.5).

#include <Nicla_System.h>
#include "FlashIAP.h"
#include <cstring>
#include <cstdio>

static constexpr uint32_t SI5351_MAGIC = 0x5351A001U;
static constexpr uint32_t BLE_MAGIC    = 0x5351B001U;

static mbed::FlashIAP gFlash;
static bool           gFlashOk = false;
static String         gUsbLine;

static uint32_t gStart    = 0;
static uint32_t gTotal    = 0;
static uint32_t gSectorSz = 0;
static uint32_t gPageSz   = 0;
static uint32_t gSi5351Addr = 0;
static uint32_t gBleAddr    = 0;   // third-to-last (§5.2.5)
static uint32_t gAvoidAddr  = 0;   // second-to-last — do not erase

struct __attribute__((packed)) ProbePattern {
    uint32_t magic;
    uint32_t counter;
    char     text[16];
};

static bool ensureFlash() {
    if (gFlashOk) return true;
    if (gFlash.init() != 0) {
        Serial.println("FAIL: FlashIAP::init");
        return false;
    }
    gStart    = gFlash.get_flash_start();
    gTotal    = gFlash.get_flash_size();
    gSectorSz = gFlash.get_sector_size(gStart + gTotal - 1);
    gPageSz   = gFlash.get_page_size();
    gSi5351Addr = gStart + gTotal - gSectorSz;
    gAvoidAddr  = gSi5351Addr - gSectorSz;           // second-to-last (occupied)
    gBleAddr    = gSi5351Addr - (2u * gSectorSz);    // third-to-last (§5.2.5)
    gFlashOk = true;
    return true;
}

static void cmdMap() {
    if (!ensureFlash()) return;
    char buf[192];
    snprintf(buf, sizeof(buf),
             "start=0x%08lX total=%lu sector=%lu page=%lu",
             static_cast<unsigned long>(gStart),
             static_cast<unsigned long>(gTotal),
             static_cast<unsigned long>(gSectorSz),
             static_cast<unsigned long>(gPageSz));
    Serial.println(buf);
    snprintf(buf, sizeof(buf),
             "si5351=0x%08lX (last) avoid=0x%08lX (2nd-last) ble=0x%08lX (3rd-last)",
             static_cast<unsigned long>(gSi5351Addr),
             static_cast<unsigned long>(gAvoidAddr),
             static_cast<unsigned long>(gBleAddr));
    Serial.println(buf);
}

static void dumpSector(uint32_t addr, const char* label, size_t nbytes) {
    if (!ensureFlash()) return;
    Serial.print("--- ");
    Serial.print(label);
    Serial.print(" @ 0x");
    Serial.println(addr, HEX);

    uint8_t buf[64];
    size_t remaining = nbytes;
    uint32_t off = 0;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        gFlash.read(buf, addr + off, chunk);
        char line[3 * 64 + 16];
        size_t n = 0;
        n += snprintf(line + n, sizeof(line) - n, "%04lX:",
                      static_cast<unsigned long>(off));
        for (size_t i = 0; i < chunk && n + 4 < sizeof(line); i++) {
            n += snprintf(line + n, sizeof(line) - n, " %02X", buf[i]);
        }
        Serial.println(line);
        off += chunk;
        remaining -= chunk;
    }
}

static void cmdRead() {
    dumpSector(gSi5351Addr, "Si5351 sector", 64);
    dumpSector(gAvoidAddr, "2nd-last (do not erase)", 64);
    dumpSector(gBleAddr, "BLE 3rd-last (§5.2.5)", 64);
}

static bool sectorAllFf(uint32_t addr) {
    const uint32_t chunk = 64;
    uint8_t buf[64];
    for (uint32_t off = 0; off < gSectorSz; off += chunk) {
        uint32_t n = (off + chunk > gSectorSz) ? (gSectorSz - off) : chunk;
        gFlash.read(buf, addr + off, n);
        for (uint32_t i = 0; i < n; i++) {
            if (buf[i] != 0xFFu) return false;
        }
    }
    return true;
}

static bool sectorSafeToWrite(uint32_t addr) {
    ProbePattern cur;
    memset(&cur, 0, sizeof(cur));
    gFlash.read(&cur, addr, sizeof(cur));
    if (cur.magic == BLE_MAGIC) return true;
    return sectorAllFf(addr);
}

static void cmdWrite() {
    if (!ensureFlash()) return;
    if (!sectorSafeToWrite(gBleAddr)) {
        Serial.println("REFUSE: BLE 3rd-last is neither blank nor our magic 0x5351B001");
        dumpSector(gBleAddr, "BLE 3rd-last (unsafe)", 64);
        return;
    }

    ProbePattern prev;
    memset(&prev, 0, sizeof(prev));
    gFlash.read(&prev, gBleAddr, sizeof(prev));

    ProbePattern next;
    next.magic   = BLE_MAGIC;
    next.counter = (prev.magic == BLE_MAGIC) ? (prev.counter + 1) : 1;
    memset(next.text, 0, sizeof(next.text));
    strncpy(next.text, "stage0-probe", sizeof(next.text) - 1);

    uint32_t writeLen = ((sizeof(next) + gPageSz - 1) / gPageSz) * gPageSz;
    if (writeLen > 64) writeLen = 64;
    uint8_t prog[64] = {};
    memcpy(prog, &next, sizeof(next));

    int er = gFlash.erase(gBleAddr, gSectorSz);
    int pr = gFlash.program(prog, gBleAddr, writeLen);
    char buf[112];
    snprintf(buf, sizeof(buf),
             "write ble@0x%08lX magic=0x%08lX counter=%lu erase=%d program=%d",
             static_cast<unsigned long>(gBleAddr),
             static_cast<unsigned long>(next.magic),
             static_cast<unsigned long>(next.counter), er, pr);
    Serial.println(buf);
}

static void cmdCheckSi() {
    if (!ensureFlash()) return;
    uint32_t magic = 0;
    gFlash.read(&magic, gSi5351Addr, sizeof(magic));
    char buf[80];
    snprintf(buf, sizeof(buf), "Si5351 magic=0x%08lX %s",
             static_cast<unsigned long>(magic),
             (magic == SI5351_MAGIC) ? "VALID" :
             (magic == 0xFFFFFFFFu) ? "ERASED" : "OTHER");
    Serial.println(buf);
}

static void handleUsb(const String& cmd) {
    if (cmd == "map") {
        cmdMap();
    } else if (cmd == "read") {
        cmdRead();
    } else if (cmd == "write") {
        cmdWrite();
    } else if (cmd == "checksi") {
        cmdCheckSi();
    } else {
        Serial.print("Unknown: ");
        Serial.println(cmd);
        Serial.println("Commands: map | read | write | checksi");
    }
}

void setup() {
    nicla::begin();
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== Stage 0 flash_sector_probe (3rd-last BLE) ===");
    if (!ensureFlash()) {
        while (true) delay(1000);
    }
    cmdMap();
    cmdCheckSi();
    Serial.println("Commands: map | read | write | checksi");
}

void loop() {
    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n') {
            gUsbLine.trim();
            if (gUsbLine.length() > 0) {
                handleUsb(gUsbLine);
            }
            gUsbLine = "";
        } else if (c != '\r') {
            gUsbLine += c;
        }
    }
}
