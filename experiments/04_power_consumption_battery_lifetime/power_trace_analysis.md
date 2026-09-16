# Power Trace Analysis

## Dataset

This dataset records a battery-powered NanoStat workflow with the normal
display-equipped system configuration. The RTT current trace was collected
from the nPM1300 `IBAT` monitor while the webapp connected over BLE and ran
five dummy-cell SWV scans.

The workflow was:

1. Start the `P` current trace;
2. Connect the webapp over BLE;
3. Run five dummy-cell SWV scans at `5.37 mV`, `120 Hz`, HSTIA external
   `330 kOhm`, and `995 kOhm` dummy resistance;
4. Disconnect the webapp;
5. Stop the trace.

All five dummy scans completed successfully. The trace therefore represents
the complete system configuration used for this test, rather than an OLED
on/off comparison.

## Current trace results

| Phase | Mean IBAT | Median IBAT | Comment |
|---|---:|---:|---|
| BLE advertising before connection | 11.22 mA | 2.0 mA | Quantised PMIC readings |
| BLE connected idle | 12.17 mA | 4.0 mA | Connected system baseline |
| Setup / quiet | 11.13 mA | 5.0 mA | Short setup interval |
| SWV run 1 | 11.24 mA | 5.0 mA | Dummy-cell scan completed |
| Gap 1--2 | 15.11 mA | 5.0 mA | Between scans |
| SWV run 2 | 14.00 mA | 5.0 mA | Dummy-cell scan completed |
| Gap 2--3 | 16.11 mA | 5.0 mA | Between scans |
| SWV run 3 | 12.62 mA | 7.0 mA | Dummy-cell scan completed |
| Gap 3--4 | 16.05 mA | 7.0 mA | Between scans |
| SWV run 4 | 14.54 mA | 5.0 mA | Dummy-cell scan completed |
| Gap 4--5 | 13.27 mA | 5.0 mA | Between scans |
| SWV run 5 | 8.89 mA | 3.0 mA | Dummy-cell scan completed |
| Connected after five runs | 13.33 mA | 5.0 mA | Short post-run interval |
| BLE disconnected | 16.42 mA | 7.0 mA | Final recorded state |

The phase summary is stored in
`data/oled_power_trace_phase_summary.csv`. The current trace contains 860
valid points over approximately 90 seconds. The median sample interval is
approximately 105 ms, with longer gaps caused by the low-rate nPM1300 monitor
and system scheduling.

## Interpretation

The experiment demonstrates that the complete battery-powered
BLE/webapp/dummy-SWV workflow executes successfully while monitoring nPM1300
`IBAT`. The five scans completed and the device returned to a safe state after
the measurement sequence.

The nPM1300 current channel is a low-bandwidth, quantised system-monitor
measurement, not a coulomb counter or a high-bandwidth current probe. The
trace repeatedly reports values near discrete current levels, so individual
raw points should not be interpreted as instantaneous current pulses from the
AD5941 or the display. Phase means are more appropriate for system-level
energy estimates than individual samples.

The figure treats raw samples as background context and uses interval means as
the primary visual signal. Each shaded interval corresponds to one workflow
stage: BLE advertising, connected idle, setup, each SWV run, each run-to-run
gap, post-run connected operation, and the final disconnected state. The thick
horizontal segment is the interval mean; the lighter band represents mean plus
or minus one population standard deviation. Dashed boundaries and labels make
the timing of each stage explicit.

## Report use

Use this dataset as a system-level battery-current trace for the normal
NanoStat configuration. It supports an engineering estimate of the energy used
by an active measurement workflow, but it does not provide a precise
instantaneous SWV power measurement or a direct battery-capacity estimate.

The official lifetime method would periodically provide battery voltage,
current, temperature and charger state to a fuel-gauge algorithm using a model
of the actual cell. Without that model, use time-integrated phase-average
current and a conservative usable-capacity assumption.

The main figure is:

- `figures/power_current_trace.png`

The plotting code is:

- `code/plot_oled_power_trace.py`

The source data and summary are:

- `data/oled_battery_trace_rtt.md`
- `data/oled_power_trace_phase_summary.csv`

For the report, describe the display as part of the tested NanoStat system
configuration. Do not use this experiment to claim a separately measured
display-only current or an exact display power increase.
