# USB versus battery dummy-load noise

## Purpose

Measure whether USB-C power introduces additional noise compared with isolated Li-Po battery operation.

## Planned protocol

Use the same dummy resistor, external 330 kOhm HSTIA path, SWV profile, frequency, cabling, and repeat count for both power sources. Randomize or alternate the power condition where practical. Compare run-to-run SD, within-scan SD, mean current error, baseline drift, and any spectral or mains-correlated features.

## Report placement

Methods: controlled power comparison. Results: system-level noise validation. Discussion: use only as evidence for a power-related contribution if the two conditions were measured with the same hardware and protocol.

## Required files

Add paired raw CSVs, power-source metadata, analysis script, summary table, and comparison plots here.

## Completed dataset

The first three non-mains conditions were repeated in a second session. The combined dataset contains 20 runs for Battery only, USB-C with the battery attached, and USB-C only; the mains-connected condition currently contains 10 runs. All runs used the same 0.995 MOhm dummy resistor, 5.37 mV SWV profile, 120 Hz frequency, and external HSTIA path.

| Condition | Mean Delta I (nA) | Run-to-run SD (nA) | Within-scan SD (nA) | Baseline RMS (nA) | Fixed-potential SD (nA) |
|---|---:|---:|---:|---:|---:|
| Battery only | 69.412 | 0.036 | 2.375 | 2.336 | 3.526 |
| USB-C + battery, MacBook unplugged | 69.396 | 0.035 | 2.218 | 2.176 | 1.886 |
| USB-C + battery, MacBook on mains | 70.065 | 1.656 | 67.049 | 67.347 | 67.348 |
| USB-C only, no battery | 69.617 | 0.284 | 6.413 | 6.541 | 7.597 |

The mains-connected MacBook condition produced a substantially larger point-to-point variation, baseline RMS, fixed-potential variation, and run-to-run variation. Across the combined non-mains data, USB-C with the battery attached was only slightly lower-noise than battery-only operation; the difference is small compared with the session-to-session variation. USB-C-only operation without the battery showed an intermediate increase in variability.

The second session alone had run-to-run SD values of 0.008 nA for battery, 0.030 nA for USB-C with battery, and 0.030 nA for USB-C only. Its within-scan SD values were 0.332, 1.636, and 2.478 nA, respectively. This shows that the apparent USB-C advantage in the combined table should not be interpreted as a robust superiority of USB-C over battery.

## Interpretation and report placement

These results support a more specific conclusion than simply blaming USB power: the dominant observed increase occurred when the MacBook was connected to mains power. The result is consistent with USB-ground or mains-coupled interference, but it does not prove the coupling mechanism because no oscilloscope or spectrum measurement was performed. USB-C with the battery attached and battery-only operation were broadly comparable across the repeated measurements; the small numerical difference is not sufficient to claim that USB-C is intrinsically quieter. The USB-C-only and battery-attached USB conditions should be reported separately.

The second-session file `5mVBattery_run2.csv` contains `power_source=mac_usb_c` in its exported metadata even though it was physically measured in the battery-only condition. The physical condition is therefore assigned from the filename and experiment record, and this metadata inconsistency should be corrected in the webapp before using the CSVs as formal traceable evidence.

Use the setup and metrics in Chapter 6, `Dummy Resistor Validation` or a short `Power-Condition Comparison` subsection. Put the figures and table in Chapter 7, `Hardware and Firmware Verification`, and refer to them in the discussion of the NanoStat versus Autolab noise difference. This dummy-cell test does not establish the effect on a real aptasensor blank.

The analysis script is `code/analyze_usb_battery_noise.py`. It generates `data/per_run_power_noise_metrics.csv`, `data/power_condition_summary.csv`, and the three figures in `figures/`.
