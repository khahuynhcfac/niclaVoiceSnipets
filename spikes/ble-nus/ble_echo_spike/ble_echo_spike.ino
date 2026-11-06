// Stage 0A — BLE echo spike (throwaway).
// Manual BLE bring-up + HardwareBLESerial NUS. See spikes/ble-nus/README.md.

#include <Nicla_System.h>
#include <ArduinoBLE.h>
#include <HardwareBLESerial.h>
#include <mbed.h>
#include <mbed_stats.h>

HardwareBLESerial& bleSerial = HardwareBLESerial::getInstance();

static char     gLocalName[24];
static String   gMac;
static bool     gAdvertising = false;
static bool     gWasConnected = false;
static String   gUsbLine;
static uint32_t gLedLastMs = 0;
static bool     gLedOn = false;

static constexpr uint32_t LED_SLOW_MS = 1000;

static void deriveLocalName(const String& mac, char* out, size_t outLen) {
    // mac like "aa:bb:cc:dd:ee:ff" → "Si5351-DDEEFF" (last 3 bytes)
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
    snprintf(out, outLen, "Si5351-%s", last3);
}

static void printInfo() {
    Serial.print("MAC=");
    Serial.println(gMac);
    Serial.print("NAME=");
    Serial.println(gLocalName);
    Serial.print("ADV=");
    Serial.println(gAdvertising ? "on" : "off");
    Serial.print("CONN=");
    Serial.println(bleSerial ? "yes" : "no");
    Serial.println("ArduinoBLE + HardwareBLESerial 1.0.0");
    Serial.println("RX_BUF=256 ATTR_MAX=20");
}

static void printHeap() {
    mbed_stats_heap_t heap;
    mbed_stats_heap_get(&heap);
    char buf[128];
    snprintf(buf, sizeof(buf),
             "HEAP current=%lu max=%lu reserved=%lu allocs=%lu fails=%lu",
             static_cast<unsigned long>(heap.current_size),
             static_cast<unsigned long>(heap.max_size),
             static_cast<unsigned long>(heap.reserved_size),
             static_cast<unsigned long>(heap.alloc_cnt),
             static_cast<unsigned long>(heap.alloc_fail_cnt));
    Serial.println(buf);
}

static void setAdvertising(bool on) {
    if (on) {
        BLE.advertise();
        gAdvertising = true;
        Serial.println("ADV on");
    } else {
        BLE.stopAdvertise();
        gAdvertising = false;
        Serial.println("ADV off");
    }
}

static void handleUsbCommand(const String& cmd) {
    if (cmd == "mac") {
        Serial.println(gMac);
    } else if (cmd == "info") {
        printInfo();
    } else if (cmd == "adv on") {
        setAdvertising(true);
    } else if (cmd == "adv off") {
        setAdvertising(false);
    } else if (cmd == "heap") {
        printHeap();
    } else {
        Serial.print("Unknown USB cmd: ");
        Serial.println(cmd);
        Serial.println("Commands: mac | info | adv on | adv off | heap");
    }
}

static void updateLed() {
    const bool connected = static_cast<bool>(bleSerial);
    if (connected) {
        nicla::leds.setColor(0, 255, 0);  // green solid
        gLedOn = true;
        return;
    }
    if (!gAdvertising) {
        nicla::leds.setColor(0, 0, 0);
        gLedOn = false;
        return;
    }
    // blue slow blink while advertising, not connected
    const uint32_t now = millis();
    if (now - gLedLastMs >= LED_SLOW_MS) {
        gLedLastMs = now;
        gLedOn = !gLedOn;
        if (gLedOn) {
            nicla::leds.setColor(0, 0, 255);
        } else {
            nicla::leds.setColor(0, 0, 0);
        }
    }
}

void setup() {
    nicla::begin();
    nicla::leds.begin();
    nicla::leds.setColor(0, 0, 0);

    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== Stage 0 ble_echo_spike ===");

    if (!BLE.begin()) {
        Serial.println("FAIL: BLE.begin()");
        while (true) {
            delay(1000);
        }
    }

    gMac = BLE.address();
    deriveLocalName(gMac, gLocalName, sizeof(gLocalName));
    BLE.setLocalName(gLocalName);
    BLE.setDeviceName(gLocalName);

    bleSerial.begin();
    BLE.advertise();
    gAdvertising = true;

    printInfo();
    printHeap();
    Serial.println("USB commands: mac | info | adv on | adv off | heap");
    Serial.println("BLE: send line ending with \\n → ECHO:<line>");
}

void loop() {
    bleSerial.poll();

    const bool connected = static_cast<bool>(bleSerial);
    if (connected && !gWasConnected) {
        BLEDevice central = BLE.central();
        Serial.print("CONNECT central=");
        Serial.print(central.address());
        Serial.print(" t=");
        Serial.println(millis());
    } else if (!connected && gWasConnected) {
        Serial.print("DISCONNECT t=");
        Serial.println(millis());
    }
    gWasConnected = connected;

    // BLE line echo
    while (bleSerial.availableLines() > 0) {
        char line[256];
        bleSerial.readLine(line, sizeof(line));
        Serial.print("BLE RX: ");
        Serial.println(line);

        char reply[280];
        snprintf(reply, sizeof(reply), "ECHO:%s", line);
        bleSerial.println(reply);
        Serial.print("BLE TX: ");
        Serial.println(reply);
    }

    // USB commands
    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n') {
            gUsbLine.trim();
            if (gUsbLine.length() > 0) {
                handleUsbCommand(gUsbLine);
            }
            gUsbLine = "";
        } else if (c != '\r') {
            gUsbLine += c;
        }
    }

    updateLed();
}
