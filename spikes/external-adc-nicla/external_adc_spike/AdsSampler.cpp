#include "AdsSampler.h"

#include <Adafruit_ADS1X15.h>
#include "AdsVoltage.h"
#include <Nicla_System.h>
#include <mbed.h>
#include <nrfx_twi.h>

static constexpr uint8_t PIN_ADS_ADDR = 0; // LPIO3, P0_10. LOW = 0x48, HIGH = 0x49
static constexpr uint8_t ADS_ADDR     = 0x48;
static constexpr uint32_t ADS_SDA_PIN = 22; // P0_22, Arduino pin 4

#define ADS_FIFO_SIZE 128

static constexpr int ADS_HW_SPS[] = {8, 16, 32, 64, 128, 250, 475, 860};
static constexpr int ADS_HW_SPS_COUNT = (int)(sizeof ADS_HW_SPS / sizeof ADS_HW_SPS[0]);

// A poll faster than half the fastest hardware rate leaves no conversion that
// can finish before the next poll.
static constexpr int ADS_POLL_SPS_MAX = ADS_HW_SPS[ADS_HW_SPS_COUNT - 1] / 2;

static_assert(ADS_POLL_SPS >= 1 && ADS_POLL_SPS <= ADS_POLL_SPS_MAX,
    "ADS_POLL_SPS must be from 1 to half the fastest ADS1115 rate");

// Hardware rate nearest to 2 * ADS_POLL_SPS. An equal distance takes the faster rate.
static constexpr uint16_t adsConvRateBits(int pollSps) {
    const int targetSps = pollSps * 2;
    const int last = ADS_HW_SPS_COUNT - 1;
    int index = 0;

    if (targetSps <= ADS_HW_SPS[0]) {
        index = 0;
    } else if (targetSps >= ADS_HW_SPS[last]) {
        index = last;
    } else {
        int i = 1;
        while (ADS_HW_SPS[i] < targetSps) {
            ++i;
        }
        const int slowerSps = ADS_HW_SPS[i - 1];
        const int fasterSps = ADS_HW_SPS[i];
        if (targetSps - slowerSps < fasterSps - targetSps) {
            index = i - 1;
        } else {
            index = i;
        }
    }

    return (uint16_t)(index << 5); // DR[2:0] sits in config bits 7:5
}

static constexpr uint16_t ADS_CONV_RATE = adsConvRateBits(ADS_POLL_SPS);
static constexpr uint32_t ADS_POLL_PERIOD_US = 1000000UL / ADS_POLL_SPS;

static Adafruit_ADS1115 ads;
static mbed::Ticker gSampleTicker;
static nrfx_twi_t gAdsTwi;
static uint8_t gAdsStart[3];

static volatile int16_t gSampleRing[ADS_FIFO_SIZE];
static volatile uint16_t gSampleHead = 0;
static volatile uint16_t gSampleTail = 0;

static void pushSampleFromTick(int16_t sample) {
    const uint16_t next = (uint16_t)((gSampleHead + 1) % ADS_FIFO_SIZE);
    if (next == gSampleTail) {
        return;
    }
    gSampleRing[gSampleHead] = sample;
    gSampleHead = next;
}

static bool twiOk(nrfx_err_t err) {
    return err == NRFX_SUCCESS;
}

static void onSampleTick() {
    uint8_t raw[2];
    const uint8_t reg = ADS1X15_REG_POINTER_CONVERT;
    const bool readOk = twiOk(nrfx_twi_tx(&gAdsTwi, ADS_ADDR, &reg, 1, false))
                     && twiOk(nrfx_twi_rx(&gAdsTwi, ADS_ADDR, raw, sizeof raw));
    const bool startOk = twiOk(nrfx_twi_tx(&gAdsTwi, ADS_ADDR, gAdsStart, sizeof gAdsStart, false));
    if (!readOk || !startOk) {
        return;
    }
    const int16_t sample = (int16_t)((uint16_t)raw[0] << 8 | raw[1]);
    pushSampleFromTick(sample);
}

static bool bindExternalTwi() {
    if ((NRF_TWI1->PSELSDA & 0x1Fu) == ADS_SDA_PIN && NRF_TWI1->ENABLE != 0) {
        gAdsTwi.p_twi = NRF_TWI1;
        gAdsTwi.drv_inst_idx = NRFX_TWI1_INST_IDX;
        return true;
    }
    if ((NRF_TWI0->PSELSDA & 0x1Fu) == ADS_SDA_PIN && NRF_TWI0->ENABLE != 0) {
        gAdsTwi.p_twi = NRF_TWI0;
        gAdsTwi.drv_inst_idx = NRFX_TWI0_INST_IDX;
        return true;
    }
    return false;
}

static void buildStartCommand() {
    const uint16_t config =
        ADS1X15_REG_CONFIG_OS_SINGLE |
        ADS1X15_REG_CONFIG_MUX_SINGLE_0 |
        ADS_GAIN |
        ADS1X15_REG_CONFIG_MODE_SINGLE |
        ADS_CONV_RATE |
        ADS1X15_REG_CONFIG_CQUE_NONE;
    gAdsStart[0] = ADS1X15_REG_POINTER_CONFIG;
    gAdsStart[1] = (uint8_t)(config >> 8);
    gAdsStart[2] = (uint8_t)config;
}

bool adsSamplerBegin() {
    nicla::begin();
    nicla::enable3V3LDO();

    pinMode(PIN_ADS_ADDR, OUTPUT);
    digitalWrite(PIN_ADS_ADDR, LOW);

    // ads.begin() starts Wire on SDA pin 4, SCL pin 3 and probes ADS_ADDR.
    const bool found = ads.begin(ADS_ADDR);
    ads.setGain(ADS_GAIN);
    ads.setDataRate(ADS_CONV_RATE);
    const bool twiBound = found && bindExternalTwi();
    buildStartCommand();
    return twiBound;
}

void adsSamplerStart() {
    // handler() queues the next tick from the previous timestamp before
    // onSampleTick runs, so the I2C time is not added to the period.
    // Priority 0 is the highest NVIC priority.
    nrfx_twi_tx(&gAdsTwi, ADS_ADDR, gAdsStart, sizeof gAdsStart, false);
    gSampleTicker.attach(onSampleTick, std::chrono::microseconds(ADS_POLL_PERIOD_US));
    NVIC_SetPriority(TIMER1_IRQn, 0);
}

bool adsSamplerNewest(int16_t* sample) {
    const uint16_t head = gSampleHead;
    const uint16_t tail = gSampleTail;
    if (tail == head) {
        return false;
    }
    const uint16_t newest = (uint16_t)((head + ADS_FIFO_SIZE - 1) % ADS_FIFO_SIZE);
    *sample = gSampleRing[newest];
    return true;
}

bool adsSamplerNext(int16_t* out) {
    if (gSampleTail == gSampleHead) {
        return false;
    }
    *out = gSampleRing[gSampleTail];
    gSampleTail = (uint16_t)((gSampleTail + 1) % ADS_FIFO_SIZE);
    return true;
}

void adsSamplerPrintMicrovolts() {
    while (gSampleTail != gSampleHead) {
        const int16_t sample = gSampleRing[gSampleTail];
        gSampleTail = (uint16_t)((gSampleTail + 1) % ADS_FIFO_SIZE);
        Serial.println(countsToUv(sample));
    }
}
