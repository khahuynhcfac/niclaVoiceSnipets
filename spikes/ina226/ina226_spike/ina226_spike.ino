// §9 Stage 0 — INA226_WE spike (throwaway). See spikes/ina226/README.md + RESULTS.md.
// Nicla Voice: nicla::begin(), default Wire SDA=4 SCL=3.

#include <Nicla_System.h>
#include <Wire.h>
#include <INA226_WE.h>
#include <si5351.h>

static constexpr uint8_t  INA_ADDR     = 0x40;
static constexpr float    SHUNT_OHMS   = 0.1f;
static constexpr float    I_MAX_A      = 0.8192f;
static constexpr uint32_t AUTO_MS      = 1000;

INA226_WE ina226(INA_ADDR);
Si5351    si5351(SI5351_BUS_BASE_ADDR);

static String   gLine;
static bool     gAutoPrint = true;
static uint32_t gLastAutoMs = 0;
static bool     gInaOk = false;
static bool     gSiOk  = false;
static uint16_t gAvg   = 16;
static uint16_t gConvUs = 1100;

static INA226_AVERAGES avgEnum(uint16_t n) {
    switch (n) {
        case 1:    return INA226_AVERAGE_1;
        case 4:    return INA226_AVERAGE_4;
        case 16:   return INA226_AVERAGE_16;
        case 64:   return INA226_AVERAGE_64;
        case 128:  return INA226_AVERAGE_128;
        case 256:  return INA226_AVERAGE_256;
        case 512:  return INA226_AVERAGE_512;
        case 1024: return INA226_AVERAGE_1024;
        default:   return INA226_AVERAGE_16;
    }
}

static INA226_CONV_TIME convEnum(uint16_t us) {
    switch (us) {
        case 140:  return INA226_CONV_TIME_140;
        case 204:  return INA226_CONV_TIME_204;
        case 332:  return INA226_CONV_TIME_332;
        case 588:  return INA226_CONV_TIME_588;
        case 1100: return INA226_CONV_TIME_1100;
        case 2116: return INA226_CONV_TIME_2116;
        case 4156: return INA226_CONV_TIME_4156;
        case 8244: return INA226_CONV_TIME_8244;
        default:   return INA226_CONV_TIME_1100;
    }
}

static bool isLegalAvg(uint16_t n) {
    return n == 1 || n == 4 || n == 16 || n == 64 || n == 128 ||
           n == 256 || n == 512 || n == 1024;
}

static bool isLegalConv(uint16_t us) {
    return us == 140 || us == 204 || us == 332 || us == 588 ||
           us == 1100 || us == 2116 || us == 4156 || us == 8244;
}

static void i2cScan() {
    Serial.println("I2C scan (Wire):");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "  0x%02X ACK", addr);
            Serial.println(buf);
            found++;
        }
    }
    if (found == 0) Serial.println("  (none)");
}

static void printReading() {
    if (!gInaOk) {
        Serial.println("INA: not init");
        return;
    }

    const float shunt_mV = ina226.getShuntVoltage_mV();
    const float bus_V    = ina226.getBusVoltage_V();
    const float curr_mA  = ina226.getCurrent_mA();
    const float power_mW = ina226.getBusPower();
    const byte  err      = ina226.getI2cErrorCode();

    char buf[160];
    snprintf(buf, sizeof(buf),
             "bus_V=%.4f shunt_mV=%.3f curr_mA=%.3f power_mW=%.3f curr_A=%.6f power_W=%.6f ovf=%u i2c_err=%u",
             bus_V, shunt_mV, curr_mA, power_mW,
             curr_mA / 1000.0f, power_mW / 1000.0f,
             ina226.overflow ? 1u : 0u, static_cast<unsigned>(err));
    Serial.println(buf);
}

static void printHelp() {
    Serial.println("Commands: help | scan | read | auto on|off | avg <1|4|16|...|1024>");
    Serial.println("          conv <140|204|332|588|1100|2116|4156|8244> | si5351 | info");
}

static void printInfo() {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "INA ok=%u addr=0x%02X shunt=%.3f Imax=%.4f avg=%u conv_us=%u SI5351 ok=%u",
             gInaOk ? 1u : 0u, INA_ADDR, SHUNT_OHMS, I_MAX_A, gAvg, gConvUs,
             gSiOk ? 1u : 0u);
    Serial.println(buf);
}

static bool applyAvg(uint16_t n) {
    if (!isLegalAvg(n)) return false;
    gAvg = n;
    ina226.setAverage(avgEnum(n));
    return true;
}

static bool applyConv(uint16_t us) {
    if (!isLegalConv(us)) return false;
    gConvUs = us;
    ina226.setConversionTime(convEnum(us));
    return true;
}

static void trySi5351() {
    // Etherkit init() calls Wire.begin() internally (known Mbed leak if repeated).
    // Call once for coexistence check after INA is already up.
    const bool found = si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
    gSiOk = found;
    Serial.println(found ? "Si5351: OK (init)" : "Si5351: NOT FOUND");
    if (found) {
        si5351.update_status();
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "Si5351 SYS_INIT=%u LOL_A=%u LOL_B=%u LOS=%u REVID=%u",
                 si5351.dev_status.SYS_INIT,
                 si5351.dev_status.LOL_A,
                 si5351.dev_status.LOL_B,
                 si5351.dev_status.LOS,
                 si5351.dev_status.REVID);
        Serial.println(buf);
    }
}

static void handleCommand(String cmd) {
    cmd.trim();
    cmd.toLowerCase();
    if (cmd.length() == 0) return;

    if (cmd == "help" || cmd == "?") {
        printHelp();
    } else if (cmd == "scan") {
        i2cScan();
    } else if (cmd == "read") {
        printReading();
    } else if (cmd == "info") {
        printInfo();
    } else if (cmd == "auto on") {
        gAutoPrint = true;
        Serial.println("auto=on");
    } else if (cmd == "auto off") {
        gAutoPrint = false;
        Serial.println("auto=off");
    } else if (cmd.startsWith("avg ")) {
        const int n = cmd.substring(4).toInt();
        if (!gInaOk) Serial.println("INA: not init");
        else if (applyAvg(static_cast<uint16_t>(n))) {
            Serial.print("avg=");
            Serial.println(gAvg);
        } else Serial.println("ERR: illegal avg");
    } else if (cmd.startsWith("conv ")) {
        const int us = cmd.substring(5).toInt();
        if (!gInaOk) Serial.println("INA: not init");
        else if (applyConv(static_cast<uint16_t>(us))) {
            Serial.print("conv_us=");
            Serial.println(gConvUs);
        } else Serial.println("ERR: illegal conv");
    } else if (cmd == "si5351") {
        trySi5351();
    } else {
        Serial.println("ERR: unknown (help)");
    }
}

void setup() {
    nicla::begin();
    nicla::leds.begin();
    nicla::leds.setColor(0, 0, 32);  // dim blue = spike running

    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== INA226 Stage 0 spike (Nicla Voice) ===");

    // Library examples call Wire.begin(); product Si5351 leaves it to init().
    // Spike: one Wire.begin() before INA init (document in RESULTS).
    Wire.begin();

    gInaOk = ina226.init();
    if (!gInaOk) {
        Serial.println("FAIL: INA226 init — check wiring / address");
        i2cScan();
    } else {
        ina226.setResistorRange(SHUNT_OHMS, I_MAX_A);
        applyAvg(16);
        applyConv(1100);
        ina226.setMeasureMode(INA226_CONTINUOUS);
        ina226.waitUntilConversionCompleted();
        Serial.println("INA226: OK");
        printInfo();
        printReading();
    }

    printHelp();
    Serial.println("READY");
}

void loop() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n') {
            handleCommand(gLine);
            gLine = "";
        } else if (c != '\r') {
            gLine += c;
        }
    }

    if (gAutoPrint && gInaOk && (millis() - gLastAutoMs >= AUTO_MS)) {
        gLastAutoMs = millis();
        printReading();
    }
}
