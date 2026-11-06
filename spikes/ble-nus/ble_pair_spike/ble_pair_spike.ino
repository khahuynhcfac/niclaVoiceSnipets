// Throwaway: HardwareBLESerial NUS + product pairing wizard.
// See spikes/ble-nus/README.md.

#include <Nicla_System.h>
#include <HardwareBLESerial.h>

#include "Ble.h"

BleManager bleManager;
static HardwareBLESerial& bleSerial = HardwareBLESerial::getInstance();
static String gUsbLine;

void setup() {
    nicla::begin();
    nicla::leds.begin();
    nicla::leds.setColor(0, 0, 0);

    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== ble_pair_spike (NUS + pairing wizard) ===");

    bleManager.begin();
    Serial.println("USB: ble help");
    Serial.println("NUS: send line ending with \\n → ECHO:<line>");
}

void loop() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n') {
            gUsbLine.trim();
            if (gUsbLine.length() > 0) {
                if (!bleManager.handleSerialCommand(gUsbLine)) {
                    Serial.println("Unknown USB cmd — ble help");
                }
            }
            gUsbLine = "";
        } else if (c != '\r') {
            gUsbLine += c;
        }
    }

    bleManager.loop(false);

    if (!bleManager.scpiLinkUp()) return;
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
}
