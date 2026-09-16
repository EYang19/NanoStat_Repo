# NI myDAQ 5 mV SWV Capture Analysis

## Measurement summary

| Quantity | Result |
|---|---:|
| Samples | `529,353` |
| Capture duration | `5.29352 s` |
| Nominal sample interval | `10 us` (`100 kS/s`) |
| Measured voltage range | `-486.0 to +215.0 mV` |
| Detected SWV cycles | `85` |
| Cycle period | `8.333 ms` (`120 Hz`) |
| Median staircase step | `5.375 mV` |
| SD of staircase increments | `0.200 mV` |
| Reconstructed pulse peak-to-peak | `69.7 mV` |
| SD of pulse peak-to-peak estimate | `0.33 mV` |

## Interpretation

The capture contains an initial near-zero settling region, followed by a
short transition to approximately `-450 mV`. The actual SWV scan occurs from
approximately `3.85 s` to `4.56 s`. The scan contains 85 cycles, which is
consistent with a 5 mV profile at 120 Hz over the approximately 450 mV range.

When the waveform is split into 120 Hz cycles, the pulse-low level advances by
approximately `5.375 mV` per cycle. The high and low levels differ by an
estimated `69.7 mV` peak-to-peak, equivalent to approximately `+/-34.9 mV`
around the staircase level. This is consistent with the programmed SWV pulse
offset of approximately 35 mV.

The raw capture also shows short startup and end-of-scan transients, including
a positive excursion to approximately `+215 mV` at the initial transition and
a low excursion to approximately `-486 mV`. These are confined to the
transition regions and do not obscure the regular scan cycles.

## Quality assessment

This is good supporting evidence that NanoStat generated a continuous,
monotonic 5 mV SWV staircase with the expected 120 Hz timing and pulse
amplitude. The result supports the waveform shape and timing required by the
report.

The myDAQ input is suitable for this capture: the device provides 16-bit AI,
up to 200 kS/s and a DC-to-400 kHz analog-input passband. However, its typical
absolute accuracy on the +/-2 V AI range is approximately 4.9 mV, so the
absolute amplitude of an individual 5 mV step should not be described as
precisely calibrated. The repeated relative step estimate is more informative
than the absolute error of one sample.

## Suggested report wording

> A direct NI myDAQ measurement of the 5 mV NanoStat SWV output captured 85
> cycles at 120 Hz. Cycle-wise analysis measured a median staircase increment
> of 5.375 mV and a reconstructed pulse amplitude of approximately 69.7 mV
> peak-to-peak, corresponding to a +/-34.9 mV pulse around the staircase
> level. The result supports the expected waveform timing, continuity and
> approximate amplitude; it is not intended as a precision calibration of the
> 5 mV step.

## Files

- Raw capture: `5mV.csv`
- Analysis script: `../code/analyze_mydaq_5mv.py`
- Generated figure: `../figures/mydaq_5mV_voltage_profile.png`
