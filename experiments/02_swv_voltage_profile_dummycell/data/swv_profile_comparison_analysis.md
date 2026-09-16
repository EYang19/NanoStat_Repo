# SWV Voltage-Profile Comparison

The three figures compare direct NI myDAQ captures with the waveform implied by the current `ad5941_app.c` sequencer settings.
The theory uses `CODE_LOW=0x800`, `CODE_HIGH=0xB46`, `LPDAC LSB=0.5372 mV/code`, `pulse_code=65`, and `120 Hz`.
The step and pulse errors are calculated from the two half-period levels of each logical SWV step; segmented hold intervals are excluded and reported separately. The sequence index is anchored to the first valid approximately -450 mV stair, identified from its first rising edge; the focused overlay is then evaluated near -400 mV using signed transition edges. Plateau overlay bias/RMSE is calculated only at the centres of the high and low plateaus, so square-wave transitions are not misreported as voltage error.

| Profile | Firmware step | Measured step (fit scatter) | Step error | Firmware pulse p-p | Measured pulse p-p | Pulse error | Cycle-fit RMSE | Plateau bias / RMSE | Segment gaps |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 5MV | 5.372 mV | 5.369 mV (0.099 mV) | 0.05% | 69.84 mV | 69.71 mV | 0.18% | 0.10 mV | 0.6 / 0.6 mV | `none` |
| 2MV | 2.149 mV | 2.148 mV (0.103 mV) | 0.03% | 69.84 mV | 69.71 mV | 0.18% | 0.10 mV | 0.6 / 0.6 mV | `none` |
| 1MVSEG | 1.074 mV | 1.072 mV (0.003 mV) | 0.18% | 69.84 mV | 69.71 mV | 0.18% | 0.08 mV | 0.6 / 0.6 mV | `0.3700;0.3000` |

For the 2 mV comparison, the first two complete cycles near -400 mV were used for timing alignment. At the independent approximately -300 mV check, the fitted local phase drift was -0.14 ms, the mean voltage bias was 0.5 mV, and the pulse-amplitude difference was -0.13 mV.

## Interpretation

The 5 mV and 2 mV captures are continuous full-sequencer scans. Their regular 120 Hz staircase and repeated pulse levels are therefore directly comparable with a uniform theoretical profile.

The 1 mV capture preserves the nominal staircase inside each segment, but the two approximately 0.3--0.4 s holds are real timing discontinuities. They are shown as amber regions and are not treated as ordinary SWV cycles in the endpoint error.

The standalone comparison figures use short windows anchored at the first complete stair near -400 mV: 2 steps for 5 mV, 5 steps for 2 mV, and 10 steps for 1 mV. The 1 mV window remains inside the first segment and therefore avoids the segmented hold. All three overlays use signed transition-edge alignment; the 2 mV phase is specifically anchored with the first two cycles near -400 mV and then checked independently around -300 mV. The plateau overlay bias is reported separately from the relative step/pulse error.

## Report wording

> Direct NI myDAQ measurements reproduced the programmed NanoStat SWV timing for the 5 mV and 2 mV full-sequencer profiles, including the 120 Hz staircase and approximately 69.84 mV peak-to-peak pulse. The 1 mV segmented profile preserved the programmed step height within each segment but introduced two approximately 0.3--0.4 s hold intervals, so it was not temporally equivalent to a continuous 1 mV sequence.

Use the `*_theory_vs_mydaq.png` files for complete timing and the `*_comparison.png` files for focused waveform comparison. The combined metrics are in `swv_profile_comparison_metrics.csv`.
