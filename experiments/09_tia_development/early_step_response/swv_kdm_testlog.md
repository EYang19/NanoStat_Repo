# NanoStat AD5941 SWV/KDM Test Log

Date range: 2026-06-28 to 2026-07-02  
Platform: nRF52840 + Zephyr RTOS + AD5941  
Output: SEGGER RTT Viewer / `printk`  
Current main candidate path: LPTIA external 2 Mohm + 220 pF + BOOST2, Notch bypassed, software `Delta_I_avg3`

## 1. Common System State

The following baseline bring-up path was already working before the SWV/KDM refinement:

- nPM1300 power sequence completes successfully.
- AD5941 SPI communication is working.
- AD5941 Chip ID reads `0x4144`.
- AD5940 library initialization completes.
- AD5941 clock configured from 16 MHz HFOSC.
- FIFO watermark interrupt routed to AD5941 GP0 and handled by nRF52840 GPIO interrupt.
- MCU reads FIFO asynchronously from Zephyr workqueue context.
- RTT output uses `printk`; Zephyr `LOG_*` was intentionally avoided.
- LPTIA hardware offset calibration is currently disabled.

Default digital path used by most SWV tests:

```text
FIFO source: SINC2/Notch path
Notch: usually bypassed for SWV
ADC PGA: 1.5
FIFO threshold: 10
```

Important hardware revisions and assumptions:

- Early tests used AD5941 internal LPTIA RTIA, commonly 512 kohm.
- The original external compensation capacitor was 100 nF, which caused severe RC delay for fast SWV.
- The external compensation capacitor was later changed to 220 pF.
- Current intended LPTIA path:
  - internal RTIA disconnected
  - external 2 Mohm precision RTIA connected through LPTIA SW9
  - external 220 pF compensation capacitor in parallel with external RTIA
  - LPAMP boost enabled
- HSTIA external AIN1 path:
  - external 2 Mohm feedback resistor between DE0 and AIN1
  - HSTIA internal RTIA opened

## 2. Test 5: Early Sequencer SWV Pulse Test

### Purpose

This was the first working Sequencer-controlled SWV waveform test. The goal was to prove that AD5941 could generate the square-wave staircase waveform from internal SRAM while the MCU only handled FIFO readout.

### Test Setup

```text
Mode: Sequencer SWV
Base sweep: 0x746 -> 0x8BA
Approx voltage window: -100 mV to +100 mV
Step size: 10 DAC codes
Pulse amplitude: +/-0x5D codes, about +/-50 mV
Half-period: 25 ms
Sequence length: 159 words
Sampling: continuous ADC
Post-processing: raw continuous current output, no endpoint extraction yet
```

### Representative Result

From the 2026-06-28 log:

```text
Sequencer capture finalized: drained_tail=9, total=289/750 samples
Sequencer SWV capture complete: 289 samples stored in MCU RAM
Sequencer SWV avg=1.258 nA, min=-895.340 nA, max=812.434 nA
```

### Analysis

This confirmed that the hardware Sequencer could generate the SWV waveform and that ADC/FIFO readout worked. However, this mode was not yet a quantitative SWV method because the MCU was only logging the continuous waveform. There was no clean extraction of forward and reverse pulse-end currents.

### Conclusion

This test proved the SWV waveform generation path, but it was superseded by later endpoint extraction tests.

## 3. Test 6: Early SWV End-of-Pulse / KDM Diagnostic

### Purpose

This was the first attempt to implement SWV differential current extraction in firmware. Instead of using all raw samples directly, the MCU mapped sampled points to the end of the forward and reverse pulses and computed:

```text
Delta_I = I_forward - I_reverse
```

### Test Setup

```text
Mode: Sequencer SWV-KDM diagnostic
Steps: 37
Base sweep: about -100 mV to +100 mV
Pulse amplitude: +/-0x5D
Half-period: 25 ms
ADC: continuous
MCU extraction: fixed time-index endpoint extraction
Expected raw points: about 277
```

### Representative Result

From the 2026-06-28 log:

```text
total=274 samples
Step 01: Delta_I=-360.790 nA
Step 02: Delta_I=-138.177 nA
Step 03: Delta_I=-131.322 nA
Step 04: Delta_I=-74.812 nA
...
Step 37: insufficient samples
Sequencer SWV-KDM avg=44.485 nA, min=-821.622 nA, max=808.788 nA
```

### Problem Analysis

The endpoint extraction logic worked conceptually, but the extracted Delta_I values were much smaller and less stable than expected for a pure resistor. The last step also had insufficient samples.

Later analysis showed several causes:

- ADC/SINC filtering and group delay affected pulse-end timing.
- External analog RC delay was a major problem when the external capacitor was still large.
- 50 Hz mains pickup produced repeating phase artifacts.
- Fixed endpoint indices were fragile.

### Conclusion

This test proved the KDM extraction idea but was not quantitatively reliable yet.

## 4. Test 7: Fast SWV-KDM with Faster SINC2

### Purpose

This test checked whether the attenuated SWV response was caused mainly by slow SINC2/Notch filtering.

### Test Setup

```text
Mode: Sequencer SWV-KDM fast
Half-period: 25 ms
SINC2 OSR: 22
Notch: bypassed
ADC: continuous
MCU extraction: endpoint differential current after capture
```

### Observations

The fast SINC2 configuration reduced digital filter delay, but the measured SWV differential current still did not fully recover in the early hardware state.

### Problem Analysis

The result showed that the digital filter was not the only bottleneck. The analog feedback network, especially the large feedback capacitor then present in the LPTIA path, was a dominant limitation.

### Conclusion

Fast SINC2 was necessary for fast SWV, but analog RC bandwidth had to be fixed as well.

## 5. Test 8: Slow-Motion SWV Single-Point Test

### Purpose

This diagnostic test stretched the SWV pulse timing so that the analog feedback capacitor had enough time to settle. It was intended to prove that the poor fast response was an RC time-constant problem.

### Test Setup

```text
Mode: Sequencer SWV slow-motion SP
Path: LPTIA
RTIA: internal 512 kohm
Half-pulse: 250 ms
ADC/FIFO: continuous
Endpoint extraction: MCU time-index endpoint extraction
```

### Observations

Earlier attempts at true ADC-on-only-at-end-of-pulse produced zero samples because the SINC2 filter needed more than the very short sampling window. The architecture was changed to continuous ADC with endpoint extraction in firmware.

### Problem Analysis

The test confirmed an important firmware lesson:

- Do not rapidly turn ADC conversion on and off for very short SWV endpoint sampling windows when using SINC filters.
- Let ADC run continuously and extract the stable pulse-end region in software.

### Conclusion

Test 8 is kept as an RC/settling diagnostic. It is not the final measurement mode.

## 6. Test 9: Low-Impedance LPTIA SWV Test

### Purpose

This test reduced the LPTIA feedback resistance to shorten the effective RC time constant and check whether fast pulse response improved.

### Test Setup

```text
Mode: Sequencer SWV low-impedance SP
Path: LPTIA
RTIA: 20 kohm
Half-pulse: 25 ms
ADC/FIFO: continuous
Endpoint extraction: MCU time-index endpoint extraction
```

### Result and Meaning

The low-impedance path responded faster, which supported the RC-limited diagnosis. However, lowering RTIA sacrifices current sensitivity and is not suitable for the final low-nA/pA biochemical measurement.

### Conclusion

Test 9 is a useful dynamic-response diagnostic, but not the preferred measurement path.

## 7. Test 0: Mixed-Mode HSTIA Diagnostic

### Purpose

This diagnostic kept the LPDAC + Sequencer waveform generation but routed ADC measurement through the HSTIA path. The goal was to bypass the slow LPTIA feedback capacitor and test the high-speed measurement path.

### Test Setup

```text
Mode: mixed-mode HSTIA
Waveform source: LPDAC + Sequencer
ADC path: HSTIA
Initial RTIA: internal HSTIA RTIA
Half-period: 20 ms
```

### Important Fixes Discovered

Early HSTIA mixed-mode tests produced near-zero current because the electrode path was not fully connected. Two fixes were added:

```text
T-switch: SWT_TRTIA | SWT_SE0
LPTIASW0 magic isolation value: 0x0094
```

The first connected SE0 into the HSTIA path. The second prevented LPTIA from stealing current from HSTIA.

### Result and Meaning

After routing fixes, HSTIA produced valid current readings. This confirmed that the HSTIA path could be used for high-speed SWV and as a comparison path.

### Conclusion

Test 0 was critical for debugging switch-matrix routing. It is not the final low-current mode but remains a useful high-speed diagnostic.

## 8. Hardware Change: External Capacitor Reduced to 220 pF

### Background

The original 100 nF feedback capacitor caused a very large RC time constant. With hundreds of kohms to megaohms of feedback resistance, this made 25 ms or faster pulses unable to reach steady state.

### Updated Hardware

```text
External LPTIA RTIA: 2 Mohm
External compensation capacitor: 220 pF
Internal RTIA: disconnected for external-only tests
```

### Bandwidth Tradeoff

With external 2 Mohm and 220 pF:

```text
tau = R * C = 2 Mohm * 220 pF = 0.44 ms
5 tau = 2.2 ms
```

This is compatible with 150 Hz SWV half-period:

```text
150 Hz step period = 6.667 ms
half-period = 3.333 ms
```

The updated capacitor made the LPTIA external 2 Mohm path realistic for 150 Hz endpoint sampling.

## 9. Test a: HSTIA External AIN1 2 Mohm, 150 Hz Comparison

### Purpose

Compare HSTIA against the optimized LPTIA path under the same SWV timing and software extraction.

### Test Setup

```text
Key: a
Path: HSTIA external AIN1-DE0 2 Mohm
Internal HSTIA RTIA: open
Frequency: 150 Hz
Half-period: 3333 us
Base window: 0x7DE to 0x826
Approx voltage window: about -18.3 mV to +20.4 mV
Step: 2 DAC codes
Pulse: +/-9 DAC codes
Notch: bypassed
SINC2 OSR: 22
Endpoint avg: 20%
Endpoint guard: 5%
Post-processing: Delta_I and Delta_I_avg3
Dummy resistor: 1.1 Mohm in the key comparison test
```

### Result

From the `Delta_I_avg3` comparison run:

```text
raw Delta_I:
  avg  = -9.91 nA
  min  = -14.59 nA
  max  = -1.48 nA
  span = 13.11 nA

Delta_I_avg3:
  avg  = -9.85 nA
  min  = -13.71 nA
  max  = -8.62 nA
  span = 5.09 nA
```

### Analysis

HSTIA worked and produced the correct order of current for the 1.1 Mohm dummy resistor. However, after software smoothing, the span was still larger than the LPTIA optimized path.

### Conclusion

HSTIA external 2 Mohm is valid and useful for comparison or high-frequency operation, but it was not the best 150 Hz low-current path.

## 10. Test b: LPTIA External 2 Mohm + 220 pF + BOOST2, 150 Hz Comparison

### Purpose

This is the current primary 150 Hz low-current SWV candidate.

### Test Setup

```text
Key: b
Path: LPTIA
Internal RTIA: open
External feedback: 2 Mohm + 220 pF
SW9: closed
LPAMP boost: BOOST2
Frequency: 150 Hz
Half-period: 3333 us
Base window: 0x7DE to 0x826
Step: 2 DAC codes
Pulse: +/-9 DAC codes
Notch: bypassed
SINC2 OSR: 22
Endpoint avg: 20%
Endpoint guard: 5%
Software smoothing: Delta_I_avg3
Dummy resistor: 1.1 Mohm
```

Expected current for 1.1 Mohm dummy:

```text
Delta V = 18 codes * 0.537 mV/code = about 9.67 mV
I = 9.67 mV / 1.1 Mohm = about 8.8 nA
```

### Result

From the key comparison run:

```text
raw Delta_I:
  avg  = -9.06 nA
  min  = -13.75 nA
  max  = -0.12 nA
  span = 13.63 nA

Delta_I_avg3:
  avg  = -9.01 nA
  min  = -11.64 nA
  max  = -7.80 nA
  span = 3.84 nA
```

### Problem Analysis

The raw Delta_I showed a clear 3-step periodic ripple. This is caused by 50 Hz mains pickup:

```text
150 Hz SWV step period = 6.667 ms
3 steps = 20 ms
50 Hz period = 20 ms
```

The centered 3-step moving average suppressed this phase-locked ripple very effectively.

### Conclusion

This is the best current 150 Hz low-current path:

```text
LPTIA external 2 Mohm + 220 pF + BOOST2 + Notch bypassed + Delta_I_avg3
```

It is close to the 1.1 Mohm theoretical current and has lower span than HSTIA at 150 Hz.

## 11. Test c: LPTIA External 2 Mohm + 220 pF + BOOST2, 210 Hz Detune

### Purpose

Check whether going above 150 Hz remains viable while avoiding exact 50 Hz locking. 200 Hz was avoided because it locks to 50 Hz every four steps.

### Test Setup

```text
Key: c
Path: LPTIA external 2 Mohm + 220 pF
Boost: BOOST2
Frequency: 210 Hz
Half-period: 2381 us
Notch: bypassed
SINC2 OSR: 22
```

### Result

Representative 210 Hz run:

```text
Delta_I avg = -5.17 nA
range = +0.28 nA to -9.57 nA
```

### Analysis

The 50 Hz locking was reduced, but the measured amplitude dropped significantly compared with the expected approximately -8.8 nA for the 1.1 Mohm dummy. This suggests that 210 Hz is close to or beyond the practical dynamic limit of the current LPTIA 2 Mohm + 220 pF path.

### Conclusion

210 Hz is useful as a diagnostic, but not recommended as the primary quantitative LPTIA mode. Above about 150 Hz, HSTIA should be considered.

## 12. Test d: LPTIA 512 kohm Parallel External 2 Mohm + BOOST2

### Purpose

Retain the internal 512 kohm RTIA while the external 2 Mohm path is also present. This creates a lower equivalent RTIA.

### Test Setup

```text
Key: d
Path: LPTIA
Internal RTIA: 512 kohm enabled
External RTIA: 2 Mohm in parallel
Equivalent RTIA: about 407 kohm
External capacitor: 220 pF
Boost: BOOST2
Frequency: 150 Hz
```

### Analysis

This mode sacrifices the precision and high sensitivity of the external 2 Mohm resistor. It also lowers transimpedance significantly.

### Conclusion

This is kept as a fail/control test, not as the final measurement path.

## 13. Test e: LPTIA External 2 Mohm + 220 pF, No Boost

### Purpose

Check whether LPAMP BOOST2 is necessary for 150 Hz SWV with 2 Mohm + 220 pF.

### Test Setup

```text
Key: e
Path: LPTIA external 2 Mohm + 220 pF
Internal RTIA: open
Boost: disabled
Frequency: 150 Hz
```

### Analysis

The no-boost path was less reliable than the boosted path. Since 150 Hz is already a demanding use case for the low-power TIA, BOOST2 provides useful analog bandwidth and stability margin.

### Conclusion

Kept as a fail/control test. Final LPTIA 150 Hz path uses BOOST2.

## 14. Tests f and g: Hardware Notch Enabled Controls

### Purpose

Test whether AD5941's 50 Hz Notch filter could suppress the observed mains ripple in 150 Hz SWV.

### Test f Setup

```text
Key: f
Path: HSTIA external 2 Mohm
Frequency: 150 Hz
Notch: enabled
```

Result:

```text
avg Delta_I = -6.25 nA
min = -13.267 nA
max = +0.984 nA
positive points = 1 / 37
```

### Test g Setup

```text
Key: g
Path: LPTIA external 2 Mohm + 220 pF + BOOST2
Frequency: 150 Hz
Notch: enabled
```

Result:

```text
avg Delta_I = -2.25 nA
min = -12.531 nA
max = +6.748 nA
positive points = 13 / 37
```

### Problem Analysis

The Notch filter did not simply remove 50 Hz noise. For 150 Hz SWV, its group delay and averaging behavior interfered with the pulse-end differential measurement. In the LPTIA path it significantly degraded the data and even flipped many Delta_I points positive.

### Conclusion

Do not use the hardware Notch for 150 Hz SWV. The better strategy is:

```text
Notch bypassed
Fast SINC2
Software 3-step moving average
Physical shielding / Faraday cage
Battery-powered operation
```

## 15. Software Improvement: Delta_I_avg3

### Purpose

Suppress the 3-step ripple caused by 50 Hz mains coupling in 150 Hz SWV.

### Method

For each step:

```text
Delta_I_avg3[N] = (Delta_I[N-1] + Delta_I[N] + Delta_I[N+1]) / 3
```

At the first and last points, a 2-point average is used.

### Result

For HSTIA external 2 Mohm at 150 Hz:

```text
raw span: 13.11 nA
avg3 span: 5.09 nA
```

For LPTIA external 2 Mohm + 220 pF + BOOST2 at 150 Hz:

```text
raw span: 13.63 nA
avg3 span: 3.84 nA
```

### Conclusion

The avg3 method is currently the best software-side 50 Hz mitigation for 150 Hz SWV. It preserves the expected current amplitude far better than the hardware Notch filter.

## 16. Test h: Dual-Frequency SWV/KDM Dummy Test

### Purpose

Implement a paper-style dual-frequency SWV/KDM workflow:

```text
Scan 1: 150 Hz signal-on
Scan 2: 10 Hz signal-off
KDM = (I_signal_on - I_signal_off) / ((I_signal_on + I_signal_off) / 2)
```

### Test Setup

```text
Key: h
Path: LPTIA external 2 Mohm + 220 pF
Internal RTIA: open
SW9: closed
Boost: BOOST2
Notch: bypassed
Potential window: -450 mV to 0 mV
DAC code window: 0x4BA to 0x800
Step size: 10 codes, about 5.37 mV
Pulse amplitude: +/-65 codes, about +/-35 mV
Scan 1 frequency: 150 Hz
Scan 2 frequency: 10 Hz
```

Sampling configuration:

```text
150 Hz scan: SINC2 OSR=22
10 Hz scan: SINC2 OSR=178
Endpoint avg: 20%
Endpoint guard: 5%
Peak extraction: maximum absolute Delta_I_avg3
```

### Current Status

This test is designed and implemented but still needs experimental validation with the current dummy resistor and later with the biochemical sensor. With a 1.1 Mohm dummy resistor, there is no real redox peak, so peak extraction is only a process validation method.

### Expected Meaning

This test validates:

- automatic two-scan sequencing
- 150 Hz and 10 Hz scan scheduling
- FIFO capture across both scans
- endpoint extraction over a wide potential window
- KDM ratio calculation

### Conclusion

Test h is the current bridge from diagnostic SWV tests toward the final cortisol KDM workflow.

## 17. Final Engineering Conclusions So Far

### Best 150 Hz Low-Current Path

```text
LPTIA external 2 Mohm + 220 pF
Internal RTIA open
SW9 closed
BOOST2 enabled
Notch bypassed
SINC2 OSR=22
Endpoint avg=20%
Endpoint guard=5%
Delta_I_avg3 enabled
```

### Frequency Partition Recommendation

```text
SWV <= 150 Hz:
  Use LPTIA external 2 Mohm + 220 pF + BOOST2

SWV > 150 Hz:
  Consider HSTIA external 2 Mohm
```

### Hardware Improvements Planned

- Faraday cage inside the enclosure
- Battery-powered operation during measurement
- Shorter electrode/dummy-cell leads
- Shielded or guarded analog routing
- Reduced coupling from USB/J-Link during sensitive measurements

### Tests to Keep

Primary:

```text
b: LPTIA 150 Hz optimized path
h: dual-frequency SWV/KDM workflow
```

Comparison:

```text
a: HSTIA external 2 Mohm 150 Hz
c: LPTIA 210 Hz detune
```

Fail/control:

```text
d: 512k || 2M
e: no boost
f/g: Notch enabled controls
8/9: RC and low-impedance diagnostics
```

