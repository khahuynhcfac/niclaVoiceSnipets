# ADS1115 sampler — Nicla Voice

Reference for timed single-shot samples from an ADS1115. The piece to copy
into another Nicla Voice sketch is `external_adc_spike/external_adc_spike.ino`.

`AdsSampler`, `AdsVoltage`, and `AdsAdvert` sit next to the sketch.
`loop()` drains a ring that the timer interrupt has already filled.
`adsSamplerPrintMicrovolts()` prints one signed microvolt value per line.
When the ring has a sample, `adsAdvertSend()` also sends the newest raw
count in one non-connectable advertisement. The 4-byte payload matches
`references/NiclaBleNus_LowPowerTest`: application code `0xC3`, int16
little-endian count, checksum. `NiclaBleNus_AdvScanner`, in the folder
next to this sketch, has its own copy of `AdsVoltage` and turns that count
back into microvolts with `countsToUv()`.

## Wiring

| Signal | Nicla Voice |
|---|---|
| ADDR | J2 LPIO3, Arduino pin 0 (`P0_10`), driven low → `0x48` |
| SDA | Arduino pin 4 (`P0_22`) |
| SCL | Arduino pin 3 (`P0_23`) |
| VDD | `VDDIO_EXT`, 3.3 V from `nicla::enable3V3LDO()` |
| Input | AIN0, single-ended |

Library: **Adafruit ADS1X15**. `ads.begin()` starts `Wire` and checks the
address. The tick does not call `Wire` or the library. `Wire` locks a mutex,
and that lock cannot run from the timer interrupt. The tick uses
`nrfx_twi_tx` / `nrfx_twi_rx` on the TWI peripheral `Wire` already enabled.

Do not call `Wire` while the ticker is attached.

`fifo_queue.h` is not part of this sampler. Its mutex cannot run from the tick.
The ring in the sketch is the sample buffer. A full ring drops the new sample.

## Timing

`ADS_POLL_SPS` is the only rate to set. It is how often `TIMER1` reads the
ADC and starts the next conversion. Default is 128. A value outside
1 .. half the fastest hardware rate does not compile. That cap is 430 SPS.

There is no ALERT / RDY pin, so the sketch cannot wait for the chip to say a
conversion is done. `ADS_HW_SPS` is the legal ADS1115 data-rate list.
`ADS_CONV_RATE` is the entry nearest to twice `ADS_POLL_SPS` (an equal
distance takes the faster entry) and that field is written to the chip.
At the default poll of 128, twice that is 256, and the nearest entry is 250 SPS.

`mbed::Ticker` runs on `TIMER1`. Its handler queues the next tick from the
previous timestamp before `onSampleTick()` runs, so the I2C time is not added
to the period. After `attach`, `TIMER1` is set to NVIC priority 0.

`countsToUv()` treats the conversion register as a signed 16-bit code.
One LSB is the PGA full-scale range divided by 32768. At `GAIN_ONE` that is
125 µV (±4.096 V).

## Build

```bash
CLI="$LOCALAPPDATA/arduino-cli/arduino-cli.exe"
FQBN=arduino:mbed_nicla:nicla_voice
ROOT="D:/OneDrive - Cardiff University/PhD/Research/SCPI_Si5351"

"$CLI" compile -b "$FQBN" "$ROOT/spikes/external-adc-nicla/external_adc_spike"
"$CLI" upload -p COMx --fqbn "$FQBN" "$ROOT/spikes/external-adc-nicla/external_adc_spike"
```

USB serial at **115200**. On success the port is only sample lines. If the
chip does not ACK, or the TWI peripheral is not the one on `P0_22`, the
sketch prints `ADS1115 not ready` and does not start the ticker.

## Host check

```bash
python external_adc_spike/log_ads1115.py -p COMx -o capture.csv
```

Requires `pyserial` and `matplotlib`. Integer lines are stored. Any other
line is printed and skipped.
