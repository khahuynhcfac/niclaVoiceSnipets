//  NiclaBleNus.ino — skeleton for an Arduino Nicla Voice project that talks
//  over BLE using the Nordic UART Service.
//
//  Only this file is meant to change when you start a new project: add your
//  own setup in startApplication(), your own work in serviceApplication(), and
//  handle incoming text in handleBleLine() / handleUsbLine(). Project-wide
//  settings such as the advertised name live in BleNusConfig.h.

#include <Nicla_System.h>

#include "BleConsole.h"
#include "BleNusConfig.h"
#include "BleNusPeripheral.h"
#include "BleStatusLed.h"
#include "SerialLineReader.h"

BleNusPeripheral blePeripheral;
BleConsole       bleConsole(blePeripheral);
BleStatusLed     statusLed;
SerialLineReader usbLineReader;

static void startApplication();
static void serviceApplication();
static void handleUsbLine(const String& line);
static void handleBleLine(const char* line);

void setup() {
    nicla::begin();  // raises VDDIO_EXT to 3.3 V for external peripherals
    statusLed.begin();

    Serial.begin(BleNusConfig::USB_SERIAL_BAUD);
    delay(1000);
    Serial.println();
    Serial.println("=== Nicla BLE NUS skeleton ===");

    blePeripheral.begin();
    startApplication();

    Serial.println("Ready. Type 'ble help' on USB serial.");
}

void loop() {
    String usbLine;
    while (usbLineReader.readLineFrom(Serial, usbLine)) {
        handleUsbLine(usbLine);
    }

    blePeripheral.update();

    char bleLine[BleNusConfig::MAX_BLE_LINE_LENGTH];
    while (blePeripheral.readLine(bleLine, sizeof(bleLine))) {
        handleBleLine(bleLine);
    }

    serviceApplication();
    statusLed.update(blePeripheral.state());
}

// ---------------------------------------------------------------------------
// Application hooks — replace the bodies below with your own project.
// ---------------------------------------------------------------------------

static void startApplication() {
    // Bring up your sensors, drivers and I2C devices here.
}

static void serviceApplication() {
    // Run your non-blocking periodic work here.
}

static void handleUsbLine(const String& line) {
    if (bleConsole.handleLine(line)) return;

    Serial.print("USB echo: ");
    Serial.println(line);
}

static void handleBleLine(const char* line) {
    Serial.print("BLE echo: ");
    Serial.println(line);

    blePeripheral.print(line);
    blePeripheral.print("\n");
}
