// Stage A — 12-bit analogRead rate spike (throwaway).
// See spikes/adc-rate-nicla/README.md + RESULTS.md.

#include <Nicla_System.h>

static constexpr uint32_t RUN_US = 2000000UL;  // 2 s
static constexpr float    EWMA_A = 0.2f;

static String gLine;

static void printHelp() {
    Serial.println("Commands: help | raw | ewma");
}

static void runRaw() {
    analogReadResolution(12);
    const uint32_t t0 = micros();
    uint32_t n = 0;
    uint32_t prev = t0;
    uint32_t dMin = 0xFFFFFFFFUL;
    uint32_t dMax = 0;
    while ((micros() - t0) < RUN_US) {
        (void)analogRead(A0);
        const uint32_t now = micros();
        const uint32_t dt = now - prev;
        prev = now;
        if (dt < dMin) dMin = dt;
        if (dt > dMax) dMax = dt;
        n++;
    }
    const uint32_t elapsed = micros() - t0;
    const float sps = (elapsed > 0)
                          ? (static_cast<float>(n) * 1e6f /
                             static_cast<float>(elapsed))
                          : 0.0f;
    char buf[96];
    snprintf(buf, sizeof(buf),
             "RAW n=%lu elapsed_us=%lu sps=%.1f dt_us min=%lu max=%lu",
             static_cast<unsigned long>(n),
             static_cast<unsigned long>(elapsed),
             static_cast<double>(sps),
             static_cast<unsigned long>(dMin),
             static_cast<unsigned long>(dMax));
    Serial.println(buf);
}

static void runEwma() {
    analogReadResolution(12);
    float vs = 0.0f;
    bool seeded = false;
    const uint32_t t0 = micros();
    uint32_t n = 0;
    while ((micros() - t0) < RUN_US) {
        const float v = static_cast<float>(analogRead(A0));
        if (!seeded) {
            vs = v;
            seeded = true;
        } else {
            vs = EWMA_A * v + (1.0f - EWMA_A) * vs;
        }
        n++;
    }
    (void)vs;
    const uint32_t elapsed = micros() - t0;
    const float sps = (elapsed > 0)
                          ? (static_cast<float>(n) * 1e6f /
                             static_cast<float>(elapsed))
                          : 0.0f;
    char buf[80];
    snprintf(buf, sizeof(buf),
             "EWMA n=%lu elapsed_us=%lu sps=%.1f",
             static_cast<unsigned long>(n),
             static_cast<unsigned long>(elapsed),
             static_cast<double>(sps));
    Serial.println(buf);
}

static void handleLine(String line) {
    line.trim();
    line.toLowerCase();
    if (line.length() == 0) return;
    if (line == "help") {
        printHelp();
    } else if (line == "raw") {
        runRaw();
    } else if (line == "ewma") {
        runEwma();
    } else {
        Serial.println("ERR unknown — try help");
    }
}

void setup() {
    nicla::begin();
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=== adc-rate-nicla spike (A0, 12-bit) ===");
    analogReadResolution(12);
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
}
