# Validation Summary

This page summarises what each archived experiment establishes and what it does not establish.

## Electrical and System Validation

| ID | Experiment | Evidence | Boundary |
|---|---|---|---|
| 01 | [Known-load repeatability](../experiments/01_dummy_stepheight_repeatability/) | Correct nanoamp-scale differential current and low run-to-run variation on a `0.995 MOhm` load | One principal current operating point; not a full-range calibration |
| 02 | [SWV voltage profile](../experiments/02_swv_voltage_profile_dummycell/) | Staircase step, pulse amplitude, direction, and relative timing agree with the programmed sequence | Offline registration was required because myDAQ and NanoStat had no shared trigger |
| 03 | [Post-measurement high impedance](../experiments/03_post_measurement_highz_celloff/) | External voltage transient and firmware flags support electrode disconnection after measurement | Does not directly measure switch off-leakage |
| 04 | [Power consumption](../experiments/04_power_consumption_battery_lifetime/) | Stage-level current trace and active-test charge estimate | No complete battery-discharge test and no board-level ship-current measurement |
| 05 | [BLE range and reliability](../experiments/05_ble_range_data_reliability/) | Stable links to `18 m` and connection near `28 m` in an unobstructed corridor | Environment was unusually favourable; no RF chamber or obstructed-building characterisation |
| 06 | [Power-source noise](../experiments/06_usb_vs_battery_dummy_noise/) | Mains-connected USB setup strongly increased noise; battery and floating USB were similar | Tested at one dummy-cell protocol |
| 07 | [Enclosure and system integration](../experiments/07_enclosure_system_validation/) | Mechanical and interface documentation | Design evidence, not an environmental or durability qualification |

## Known-Load Results

The dummy resistor was `0.995 MOhm`, with approximately `70 mV` differential excitation. The expected current was `70.19 nA`.

| Step configuration | Mean current | Run-to-run SD | CV | Error from theory |
|---|---:|---:|---:|---:|
| `5.37 mV` | `69.711 nA` | `0.146 nA` | `0.210%` | `0.67%` |
| `2.15 mV` | `69.524 nA` | `0.130 nA` | `0.187%` | `0.94%` |
| `1.07 mV`, segmented | `69.532 nA` | `0.057 nA` | `0.081%` | `0.93%` |

## Power-Source Comparison

| Condition | Runs | Run-to-run SD | Within-scan SD | Baseline RMS |
|---|---:|---:|---:|---:|
| Battery only | 20 | `0.036 nA` | `2.375 nA` | `2.336 nA` |
| USB-C plus battery, laptop floating | 20 | `0.035 nA` | `2.218 nA` | `2.176 nA` |
| USB-C only, no battery | 20 | `0.284 nA` | `6.413 nA` | `6.541 nA` |
| USB-C plus battery, laptop on mains | 10 | `1.656 nA` | `67.049 nA` | `67.347 nA` |

The data support mains-related common-mode or USB-ground coupling as the dominant disturbance in the tested setup. They do not prove that USB power is intrinsically noisier than battery power in every configuration.

## Sensor Validation

The [sensor-validation archive](../experiments/08_sensor_validation/) contains early NanoStat data, the later NanoStat session, the Autolab session, analysis scripts, and derived figures. Both instruments resolved a methylene-blue peak in a similar potential region, and the highest concentration produced the clearest response. Sensor 6 at `1000 ng/mL` produced an approximately `10.4%` response on NanoStat and `26.0%` on Autolab under the archived analysis.

The lower concentrations were not consistently separated from blank variability. Sensors were reused, sessions occurred on different days, instruments used different staircase step sizes, and repeat counts were limited. These results demonstrate electrochemical sensor readout in PBS; they do not establish a calibration curve, selectivity, a chemical limit of detection, or performance in saliva.

## Early Development Evidence

[`09_tia_development`](../experiments/09_tia_development/) preserves selected early step-response, LPTIA/HSTIA, external `2 MOhm` RTIA, filter, and blank-peak investigations. These records explain design decisions but are not presented as final instrument specifications.

