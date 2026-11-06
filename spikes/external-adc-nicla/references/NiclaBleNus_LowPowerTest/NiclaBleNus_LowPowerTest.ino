//  NiclaBleNus_LowPowerTest.ino — one non-connectable advertising event
//  every 1 s, then System ON idle (RTC wake, RAM retained).
//
//  AdvData is 4 bytes: application code, int16 little-endian value, checksum.
//  The value starts at 0 and increments by 1 on each burst. No NUS, no
//  connection, no USB serial. Unused Nicla Voice hardware stays off. The BLE
//  stack stays initialised so the current spike is the burst, not HCI reset.

// Must match ADV_APP_CODE in NiclaBleNus_AdvScanner.ino.
#define ADV_APP_CODE 0xC3

#include <ArduinoBLE.h>
#include <Nicla_System.h>
#include <mbed.h>
#include <nrf.h>
#include <rtos.h>
#include "utility/HCI.h"

#include "BleNusConfig.h"

static void shutdownUnusedNiclaVoicePeripherals();
static void disableUsbUart();
static void stayAwakeForSwd();
static void sendOneAdvertisingEvent();
static void sleepUntilNextBurst();
static void loadAdvertisingPayload(int16_t value);
static uint8_t payloadChecksum(uint8_t b0, uint8_t b1, uint8_t b2);

void setup() {
    disableUsbUart();

    nicla::begin();
    stayAwakeForSwd();
    shutdownUnusedNiclaVoicePeripherals();
    disableUsbUart();

    if (!BLE.begin()) {
        while (true) {
            rtos::ThisThread::sleep_for(1000);
        }
    }

    BLE.setConnectable(false);
    BLEAdvertisingData emptyScan;
    BLE.setScanResponseData(emptyScan);
    // ADV_NONCONN_IND minimum interval is 100 ms (160 * 0.625 ms). The radio
    // is stopped well before the second event, so this only bounds the gap.
    BLE.setAdvertisingInterval(160);

    // Program advertising parameters once. ArduinoBLE injects Flags here;
    // loadAdvertisingPayload() replaces that before each burst.
    BLE.advertise();
    BLE.stopAdvertise();
}

void loop() {
    sendOneAdvertisingEvent();
    sleepUntilNextBurst();
}

// ---------------------------------------------------------------------------
// Unused onboard hardware — Arduino Nicla Voice user manual + NDP library.
// ---------------------------------------------------------------------------

// Arduino NDP library (libraries/NDP/src/NDP.h): NDPClass::PORSTB = p18.
// NDP.begin() pulses this pin low then high. Holding it low keeps the
// Syntiant NDP120 in reset. The NDP is the SPI host for BMI270 / BMM150 and
// the PDM clock source for the IM69D130, so those stay off with it.
static constexpr PinName kNdpPorstbPin = p18;

static void stayAwakeForSwd() {
    nicla::leds.begin();
    nicla::leds.setColor(blue);
    delay(BleNusConfig::BEACON_BOOT_UNLOCK_MS);
}

static void shutdownUnusedNiclaVoicePeripherals() {
    nicla::leds.begin();
    nicla::leds.setColor(off);
    nicla::leds.end();

    nicla::disableLDO();

    pinMode(kNdpPorstbPin, OUTPUT);
    digitalWrite(kNdpPorstbPin, LOW);
}

static void disableUsbUart() {
    NRF_UART0->ENABLE  = 0;
    NRF_UARTE0->ENABLE = 0;
}

// Low 8 bits of the sum of the first three payload bytes.
static uint8_t payloadChecksum(uint8_t b0, uint8_t b1, uint8_t b2) {
    return static_cast<uint8_t>(b0 + b1 + b2);
}

static void loadAdvertisingPayload(int16_t value) {
    uint8_t adv[4];
    adv[0] = ADV_APP_CODE;
    adv[1] = static_cast<uint8_t>(value);
    adv[2] = static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8);
    adv[3] = payloadChecksum(adv[0], adv[1], adv[2]);
    HCI.leSetAdvertisingData(4, adv);

    uint8_t unused = 0;
    HCI.leSetScanResponseData(0, &unused);
}

static void sendOneAdvertisingEvent() {
    static int16_t value = 0;
    loadAdvertisingPayload(value);

    HCI.leSetAdvertiseEnable(0x01);
    BLE.poll(BleNusConfig::BEACON_EVENT_WAIT_MS);
    HCI.leSetAdvertiseEnable(0x00);

    value = (value == 32767) ? static_cast<int16_t>(-32768) : static_cast<int16_t>(value + 1);
}

static void sleepUntilNextBurst() {
    rtos::ThisThread::sleep_for(BleNusConfig::BEACON_SLEEP_MS);
}
