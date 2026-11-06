// Throwaway: Nicla Voice battery monitor via the onboard BQ25120A (I2C 0x6A, Wire1).
// Reads only. Does not enable or disable charging except as the core does
// inside getBatteryVoltagePercentage() (brief pause while it samples).

#include <Nicla_System.h>

static constexpr uint32_t AUTO_MS = 1000;

static String   gLine;
static bool     gAuto = true;
static uint32_t gLastMs = 0;

static const char* chargeLevelName(BatteryChargeLevel level) {
    switch (level) {
        case BatteryChargeLevel::Empty:       return "Empty";
        case BatteryChargeLevel::AlmostEmpty: return "AlmostEmpty";
        case BatteryChargeLevel::HalfFull:    return "HalfFull";
        case BatteryChargeLevel::AlmostFull:  return "AlmostFull";
        case BatteryChargeLevel::Full:        return "Full";
        default:                              return "Unknown";
    }
}

static const char* statusName(OperatingStatus status) {
    switch (status) {
        case OperatingStatus::Ready:            return "Ready";
        case OperatingStatus::Charging:         return "Charging";
        case OperatingStatus::ChargingComplete: return "ChargingComplete";
        case OperatingStatus::Error:            return "Error";
        default:                                return "Unknown";
    }
}

static const char* tempName(BatteryTemperature temp) {
    switch (temp) {
        case BatteryTemperature::Normal:  return "Normal";
        case BatteryTemperature::Extreme: return "Extreme";
        case BatteryTemperature::Cool:    return "Cool";
        case BatteryTemperature::Warm:    return "Warm";
        default:                          return "Unknown";
    }
}

static void printReading() {
    const bool onBat = nicla::runsOnBattery();
    const OperatingStatus op = nicla::getOperatingStatus();
    const float vreg = nicla::getRegulatedBatteryVoltage();
    const int8_t pct = nicla::getBatteryVoltagePercentage();
    const float vbat = nicla::getCurrentBatteryVoltage();
    const BatteryChargeLevel level = nicla::getBatteryChargeLevel();
    const BatteryTemperature temp = nicla::getBatteryTemperature();
    const uint8_t faults = nicla::getBatteryFaults();

    const uint8_t rawStatus = nicla::_pmic.readByte(BQ25120A_ADDRESS, BQ25120A_STATUS);
    const uint8_t rawFaults = nicla::_pmic.readByte(BQ25120A_ADDRESS, BQ25120A_FAULTS);
    const uint8_t rawMon    = nicla::_pmic.readByte(BQ25120A_ADDRESS, BQ25120A_BATT_MON);
    const uint8_t rawCtrl   = nicla::_pmic.readByte(BQ25120A_ADDRESS, BQ25120A_BATTERY_CTRL);

    char buf[240];
    snprintf(buf, sizeof(buf),
             "on_bat=%u status=%s Vreg=%.3f pct=%d Vbat=%.3f level=%s temp=%s faults=0x%X",
             onBat ? 1u : 0u,
             statusName(op),
             static_cast<double>(vreg),
             static_cast<int>(pct),
             static_cast<double>(vbat),
             chargeLevelName(level),
             tempName(temp),
             static_cast<unsigned>(faults));
    Serial.println(buf);

    snprintf(buf, sizeof(buf),
             "raw status=0x%02X faults=0x%02X batt_mon=0x%02X batt_ctrl=0x%02X"
             " (VIN_OV=%u VIN_UV=%u BAT_UVLO=%u BAT_OCP=%u)",
             rawStatus, rawFaults, rawMon, rawCtrl,
             (faults >> 3) & 1u,
             (faults >> 2) & 1u,
             (faults >> 1) & 1u,
             faults & 1u);
    Serial.println(buf);
}

static void printHelp() {
    Serial.println("Commands: help | read | auto on | auto off | ntc on | ntc off");
}

static void handleLine(String line) {
    line.trim();
    line.toLowerCase();
    if (line.length() == 0) return;
    if (line == "help") {
        printHelp();
    } else if (line == "read") {
        printReading();
    } else if (line == "auto on") {
        gAuto = true;
        Serial.println("auto on");
    } else if (line == "auto off") {
        gAuto = false;
        Serial.println("auto off");
    } else if (line == "ntc on") {
        nicla::setBatteryNTCEnabled(true);
        Serial.println("ntc on");
    } else if (line == "ntc off") {
        nicla::setBatteryNTCEnabled(false);
        Serial.println("ntc off");
    } else {
        Serial.println("ERR unknown — try help");
    }
}

void setup() {
    nicla::begin();
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== battery-nicla (BQ25120A) ===");
    printHelp();
}

void loop() {
    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n') {
            handleLine(gLine);
            gLine = "";
        } else if (c != '\r') {
            gLine += c;
        }
    }

    if (gAuto && (millis() - gLastMs) >= AUTO_MS) {
        gLastMs = millis();
        printReading();
    }
}
