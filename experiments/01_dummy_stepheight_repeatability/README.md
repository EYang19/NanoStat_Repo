# Dummy-cell step-height and repeatability

## Status

Completed using two randomized battery-powered sessions. Each session contained five repeats of each profile, giving `n = 10` per profile.

## Purpose

To isolate the NanoStat electronic readout chain from aptasensor chemistry and compare repeatability at approximately 5 mV, 2 mV, and 1 mV segmented step height.

## Setup

- Dummy resistor: measured `0.995 MOhm`.
- Connection: `WE -- R_dummy -- RE/CE`, with RE and CE shorted.
- TIA: external 330 kOhm HSTIA path; verify and normalize the firmware export label before final publication.
- Frequency: 120 Hz.
- Power: Li-Po battery.
- Expected differential current: `2 * 34.918 mV / 0.995 MOhm = 70.19 nA`.

## Archived result

| Profile | n | Mean Delta I (nA) | Run-to-run SD (nA) | CV | Error |
|---|---:|---:|---:|---:|---:|
| 5.37 mV full | 10 | 69.711 | 0.146 | 0.210% | 0.67% |
| 2.15 mV full | 10 | 69.524 | 0.130 | 0.187% | 0.94% |
| 1.07 mV segmented | 10 | 69.532 | 0.057 | 0.081% | 0.93% |

The run-to-run SD is the primary repeatability metric: it is calculated from the mean Delta I of complete scans. The within-scan point-to-point SD is a different quantity and was approximately 7--9 nA.

The files named `dummy_blank_*` are retained legacy summaries from the earlier 15-repeat dataset for traceability. The report numbers above and `data/random_session_summary.csv` are based only on `dummyrandomsession1.csv` and `dummyrandomsession2.csv`.

## Report placement

Use this in Chapter 6, `Dummy Resistor Validation`, for the setup and metrics. In Chapter 7, `Hardware and Firmware Verification`, report the table and figure before the PBS sensor results. In the discussion, state that the result supports lower-step-height SWV as a promising firmware improvement, but does not prove improved aptasensor performance without real-sensor validation.

## Limitations

The 1 mV condition used segmented SWV and a longer scan time, so the improvement cannot be attributed exclusively to step height. The dummy resistor does not reproduce electrode kinetics, double-layer charging, MB redox, or sensor regeneration.
