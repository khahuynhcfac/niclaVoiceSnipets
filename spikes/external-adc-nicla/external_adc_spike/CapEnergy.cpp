#include "CapEnergy.h"

// E = 0.5 * C * V^2
// C in uF, V in mV (= uv / 1000), result in nJ: E_nJ = C_uF * V_mV^2 / 2000

int64_t capEnergyNj(int32_t uv, int32_t capacitance_uF) {
    const int32_t v_mV = uv / 1000;
    return ((int64_t)capacitance_uF * (int64_t)v_mV * (int64_t)v_mV) / 2000;
}

int32_t capEnergyUj(int32_t uv, int32_t capacitance_uF) {
    return (int32_t)(capEnergyNj(uv, capacitance_uF) / 1000);
}
