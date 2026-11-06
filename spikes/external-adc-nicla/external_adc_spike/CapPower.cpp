#include "CapPower.h"
#include "CapEnergy.h"
#include "AdsVoltage.h"

// OLS constants for sample index i = 0 .. N-1 (computed at compile time).
static constexpr int32_t N      = PWR_WINDOW_SPS;
static constexpr int64_t SUM_I  = (int64_t)N * (N - 1) / 2;
static constexpr int64_t SUM_I2 = (int64_t)(N - 1) * N * (2 * N - 1) / 6;
static constexpr int64_t DENOM  = (int64_t)N * SUM_I2 - SUM_I * SUM_I;

static int32_t gIdx      = 0;
static int64_t gSumV     = 0;
static int64_t gSumIV    = 0;
static int16_t gPower_uW = 0;

bool capPowerFeed(int16_t counts) {
    const int32_t uv = countsToUv(counts);
    gSumV  += uv;
    gSumIV += (int64_t)gIdx * uv;
    ++gIdx;

    if (gIdx < N) {
        return false;
    }

    // OLS: V(i) = a*i + b
    const int64_t num_a    = (int64_t)N * gSumIV - SUM_I * gSumV;
    const int64_t a        = num_a / DENOM;                  // µV per sample index
    const int64_t b        = (gSumV - a * SUM_I) / N;        // µV at i=0

    const int32_t v_start  = (int32_t)b;
    const int32_t v_end    = (int32_t)(a * (N - 1) + b);

    const int64_t e_start  = capEnergyNj(v_start, CAP_UF);
    const int64_t e_end    = capEnergyNj(v_end,   CAP_UF);

    // Δt = (N-1) samples / N SPS; normalise to 1 s: P = ΔE * N / (N-1)
    // ΔE in nJ → divide by 1000 for µJ → P in µW (µJ/s)
    gPower_uW = (int16_t)((e_end - e_start) * N / (N - 1) / 1000);

    gIdx   = 0;
    gSumV  = 0;
    gSumIV = 0;
    return true;
}

int16_t capPowerGet_uW() {
    return gPower_uW;
}
