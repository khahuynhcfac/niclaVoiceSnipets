#include "AdsVoltage.h"

int32_t countsToUv(int16_t counts) {
    int32_t fsrUv;
    switch (ADS_GAIN) {
    case GAIN_TWOTHIRDS: fsrUv = 6144000; break;
    case GAIN_ONE:       fsrUv = 4096000; break;
    case GAIN_TWO:       fsrUv = 2048000; break;
    case GAIN_FOUR:      fsrUv = 1024000; break;
    case GAIN_EIGHT:     fsrUv = 512000;  break;
    case GAIN_SIXTEEN:   fsrUv = 256000;  break;
    default:             fsrUv = 4096000; break;
    }
    return (int32_t)(((int64_t)counts * fsrUv) / 32768);
}
