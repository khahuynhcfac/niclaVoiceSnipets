//  NiclaBleNus_AdvScanner.ino — dedicated Nicla Voice beacon listener.
//
//  Parks the nRF52832 RADIO on one advertising channel (default 37) and
//  never hops. ArduinoBLE cannot do this: it scans 37/38/39 and the hop
//  gap is long enough to miss a 2 ms three-PDU burst.
//
//  Flash on the listener Nicla only. DUT stays on NiclaBleNus_LowPowerTest.
//  USB serial 115200. A matching PDU prints power in microwatts (µW), one line per second.
//  Packets with a different application code or a bad checksum are dropped.

// Must match ADV_APP_CODE in NiclaBleNus_LowPowerTest.ino.
#define ADV_APP_CODE 0xC3

#include <Nicla_System.h>
#include <mbed.h>
#include <nrf.h>
#include <string.h>

namespace ListenerConfig {
constexpr uint8_t  LISTEN_CHANNEL       = 37;  // 37, 38 or 39. 37 is sent first.
constexpr unsigned long USB_SERIAL_BAUD = 115200;
}  // namespace ListenerConfig

static constexpr uint32_t kAdvAccessAddress = 0x8E89BED6UL;
static constexpr size_t   kMaxAdvPayload    = 37;
static constexpr size_t   kRxBytes          = 2 + kMaxAdvPayload;

static uint8_t  rxBuf[kRxBytes] __attribute__((aligned(4)));

static void startHfclk();
static bool radioConfigure(uint8_t channel);
static void radioStartRx();
static void parseBeacon(const uint8_t* pdu, uint8_t pduLen);
static uint8_t payloadChecksum(uint8_t b0, uint8_t b1, uint8_t b2);

void setup() {
    nicla::begin();  // starts PMIC watchdog ping thread; without this the BQ25120A resets at ~48 s

    Serial.begin(ListenerConfig::USB_SERIAL_BAUD);
    unsigned long serialWait = millis();
    while (!Serial && (millis() - serialWait) < 3000) {
    }

    Serial.println();
    Serial.println("=== Nicla beacon listener (single channel) ===");
    Serial.print("Locked on advertising channel ");
    Serial.println(ListenerConfig::LISTEN_CHANNEL);
    Serial.print("Match: app 0x");
    Serial.println(ADV_APP_CODE, HEX);

    sleep_manager_lock_deep_sleep();  // keeps HFXO running; required for RADIO
    startHfclk();
    Serial.print("HFCLK=");
    Serial.println((NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_SRC_Msk)
                   ? "Xtal"
                   : "RC (RADIO will not receive BLE)");
    if (!radioConfigure(ListenerConfig::LISTEN_CHANNEL)) {
        Serial.println("Bad LISTEN_CHANNEL (use 37, 38 or 39)");
        while (true) {}
    }
    radioStartRx();

    Serial.println("Listening.");
}

void loop() {
    if (NRF_RADIO->EVENTS_END) {
        NRF_RADIO->EVENTS_END = 0;
        const bool crcOk = (NRF_RADIO->CRCSTATUS != 0);
        uint8_t copy[kRxBytes];
        memcpy(copy, rxBuf, sizeof(copy));
        radioStartRx();

        if (crcOk) {
            const uint8_t pduLen = copy[1];
            if (pduLen >= 6 && pduLen <= kMaxAdvPayload) {
                parseBeacon(copy, pduLen);
            }
        }
    }
}

// ---------------------------------------------------------------------------

static void startHfclk() {
    // HFINT (RC) is often already running. BLE 1M needs the 32 MHz Xtal (HFXO).
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while ((NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_STATE_Msk) == 0 ||
           (NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_SRC_Msk) == 0) {
    }
}

static bool radioConfigure(uint8_t channel) {
    uint8_t freq = 0;
    uint8_t whiteIv = 0;
    switch (channel) {
        case 37: freq = 2;  whiteIv = 37; break;
        case 38: freq = 26; whiteIv = 38; break;
        case 39: freq = 80; whiteIv = 39; break;
        default: return false;
    }

    NVIC_DisableIRQ(RADIO_IRQn);
    NRF_RADIO->INTENCLR = 0xFFFFFFFFUL;
    NRF_RADIO->POWER = 0;
    delay(1);
    NRF_RADIO->POWER = 1;

    NRF_RADIO->MODE        = RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos;
    NRF_RADIO->FREQUENCY   = freq;
    NRF_RADIO->DATAWHITEIV = whiteIv;

    NRF_RADIO->PCNF0 = (1UL << RADIO_PCNF0_S0LEN_Pos) |
                       (8UL << RADIO_PCNF0_LFLEN_Pos)  |
                       (0UL << RADIO_PCNF0_S1LEN_Pos);
    NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) |
                       (RADIO_PCNF1_ENDIAN_Little   << RADIO_PCNF1_ENDIAN_Pos)   |
                       (3UL << RADIO_PCNF1_BALEN_Pos)                            |
                       (static_cast<uint32_t>(kMaxAdvPayload) << RADIO_PCNF1_MAXLEN_Pos);

    NRF_RADIO->PREFIX0    = (kAdvAccessAddress >> 24) & 0xFFUL;
    NRF_RADIO->BASE0      = (kAdvAccessAddress << 8)  & 0xFFFFFF00UL;
    NRF_RADIO->RXADDRESSES = RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos;

    NRF_RADIO->CRCCNF  = (RADIO_CRCCNF_LEN_Three      << RADIO_CRCCNF_LEN_Pos)     |
                         (RADIO_CRCCNF_SKIPADDR_Skip   << RADIO_CRCCNF_SKIPADDR_Pos);
    NRF_RADIO->CRCINIT = 0x555555UL;
    NRF_RADIO->CRCPOLY = 0x00065BUL;

    // READY_START: begin receiving as soon as the radio is ready.
    // END_DISABLE: disable automatically after each packet so loop() can safely copy rxBuf.
    NRF_RADIO->SHORTS    = RADIO_SHORTS_READY_START_Msk |
                           RADIO_SHORTS_END_DISABLE_Msk;
    NRF_RADIO->PACKETPTR = reinterpret_cast<uint32_t>(rxBuf);
    return true;
}

static void radioStartRx() {
    NRF_RADIO->EVENTS_END      = 0;
    NRF_RADIO->EVENTS_READY    = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    // Under normal operation the radio is already Disabled here (END_DISABLE short).
    // The timeout guards against a peripheral lock-up on a very long run.
    if ((NRF_RADIO->STATE & RADIO_STATE_STATE_Msk) != RADIO_STATE_STATE_Disabled) {
        NRF_RADIO->TASKS_DISABLE = 1;
        const unsigned long t0 = millis();
        while (NRF_RADIO->EVENTS_DISABLED == 0) {
            if (millis() - t0 > 10) {  // 10 ms is orders of magnitude longer than a disable transition
                NRF_RADIO->POWER = 0;
                delay(1);
                NRF_RADIO->POWER = 1;
                radioConfigure(ListenerConfig::LISTEN_CHANNEL);
                break;
            }
        }
        NRF_RADIO->EVENTS_DISABLED = 0;
    }
    NRF_RADIO->TASKS_RXEN = 1;
}

static void parseBeacon(const uint8_t* pdu, uint8_t pduLen) {
    // AdvA (6 bytes) + 4-byte application payload = pduLen 10.
    if (pduLen != 10) {
        return;
    }
    const uint8_t* data = pdu + 8;
    if (data[0] != ADV_APP_CODE) {
        return;
    }
    if (data[3] != payloadChecksum(data[0], data[1], data[2])) {
        return;
    }
    const int16_t power_uW = (int16_t)((uint16_t)data[1] | ((uint16_t)data[2] << 8));
    Serial.println(power_uW);
}

static uint8_t payloadChecksum(uint8_t b0, uint8_t b1, uint8_t b2) {
    return static_cast<uint8_t>(b0 + b1 + b2);
}
