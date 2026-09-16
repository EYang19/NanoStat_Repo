# Power consumption and battery lifetime

## Purpose

Measure NanoStat operating and sleep/idle current and estimate practical lifetime from the 190 mAh Li-Po battery under the intended measurement schedule.

## Recommended protocol: webapp for SWV, RTT for battery data

No webapp change is required. The firmware now polls the RTT key `b` approximately every 10 ms during its background loop, so a battery snapshot can be requested while a BLE-triggered SWV scan is running. The BLE battery page is not required for this experiment because its current command path rejects a request while the AFE reports itself busy.

Use this connection arrangement:

- NanoStat powered from the Li-Po battery only;
- USB-C disconnected, so the battery current is not mixed with charger current;
- J-Link connected through SWD for RTT only;
- Webapp connected to NanoStat over BLE for the SWV operation.

Do not enter the RTT measurement menu with `y`; leave the device in the idle loop so that the `b` key remains available.

At each state, press `b` in RTT and save the complete snapshot. Record the local time beside each snapshot:

1. After boot, before BLE connection: system idle baseline;
2. After BLE connection, before SWV: BLE-connected idle;
3. Immediately before pressing `Start` in the webapp;
4. During or immediately after the SWV scan: active/post-scan state;
5. After the webapp receives `EVT,DONE` and High-Z is applied: post-measurement idle.

For the active measurement state, run a repeat sequence or a dummy repeat test and press `b` repeatedly. A single human keypress is not precise enough to capture a 0.7 s, 120 Hz scan. The nPM1300 gauge current is also an averaged value, so interpret it as a system-level snapshot rather than an oscilloscope-like instantaneous current trace.

The RTT output to retain is:

```text
[BAT] VBAT=...
[BAT] IBAT=...
[BAT] DIE=...
[BAT] CHG_STATUS=...
[BAT] CHG_ERROR=...
[BAT] VBUS_LIMIT=...
[BAT] CHARGE_CURRENT_CFG=...
[BAT] DISCHARGE_LIMIT_CFG=...
```

The important field for battery consumption is `IBAT`. With USB disconnected, positive `IBAT` is battery discharge. `VBAT` and the voltage-only SOC estimate are supporting context, not a direct capacity measurement.

## Dense current trace through RTT

For a time-aligned trace, use uppercase `P` while the device is in the idle BLE loop. Press `P` once to start and press it again to stop. The firmware outputs one compact line approximately every `100 ms`:

```text
BATCUR,t_ms=123456,IBAT_UA=18400
```

The `t_ms` field is the nRF52840 uptime in milliseconds and `IBAT_UA` is the nPM1300 battery current in microamperes. Save these lines from RTT while operating the webapp. The trace can show BLE connection, SWV start/end, High-Z, and return to idle as changes in current. Use `b` separately for full charger/temperature snapshots; do not enable the dense trace and repeatedly press `b` at the same time.

The trace is sampled by the nRF52840 main loop and requests the nPM1300 gauge at approximately 10 Hz. It is a system-current trace, not a high-bandwidth current probe. The nPM1300 gauge itself reports an averaged current, so short SWV pulses will appear as a change in the averaged current rather than as individual electrochemical pulses.

When uppercase `P` is active, the firmware suppresses the large per-point SWV
CSV dump on RTT. The SWV data are still sent to the webapp over BLE. This
prevents `printk` output from blocking the main loop and creating artificial
gaps in the `BATCUR` time series. A few summary messages may still appear,
but the per-point SWV stream is deliberately kept off RTT during the trace.

This improves logger timing; it does not turn `IBAT` into a high-bandwidth
current probe. The nPM1300 result remains an ADC/gauge measurement with finite
resolution and its own conversion/update schedule.

## Correct interpretation of nPM1300 current data

nPM1300 exposes battery current through its internal system-monitor ADC. The
host requests an `IBAT` conversion and reads the result after the measurement
is ready; charger mode determines the current direction and the configured
charge/discharge limit determines the current full-scale. Therefore the
`BATCUR` stream is suitable for system-level average-current and duty-cycle
analysis, but not for reconstructing each SWV pulse.

Nordic's recommended lifetime approach is to feed periodic battery voltage,
current, temperature, and charger-state measurements into the nRF Fuel Gauge
with a model of the actual cell. That algorithm provides SoC and
time-to-empty/time-to-full estimates. A voltage-only percentage is only a
rough diagnostic and must not be used as measured consumed capacity.

For a report-level estimate without a calibrated fuel-gauge model:

```text
Q_used (mAh) = sum(I_battery_mA * elapsed_time_h)
daily demand = I_sleep * sleep_hours
             + I_idle * idle_hours
             + I_active * total_active_hours
runtime days = usable_capacity_mAh / daily demand_mAh
```

Use `IBAT > 0` only when USB/VBUS is physically disconnected. Record a fresh
`b` snapshot before the trace and retain the VBUS indication. Do not mix USB
input current, battery charging current, and battery discharge current in one
capacity calculation. Use a conservative usable capacity instead of assuming
the full nominal `190 mAh` is available at every load and cutoff.

For high-resolution validation, use an external current-sense resistor with a
differential oscilloscope, a power analyzer, a PPK2, or an SMU. The nPM1300 EK
documentation treats those instruments as the measurement path for current
traces; the internal `IBAT` result is not a replacement for them.

## Planned protocol

Fully charge the battery, disconnect USB, and record an RTT battery snapshot before each state. Use the `b` battery-status command or the webapp battery page if its response is reliable. Record `VBAT`, `IBAT`, charger status, and die temperature at idle after boot, BLE-connected idle, during SWV, immediately after high-Z, and after the display/BLE workflow returns to idle. Take at least five readings per state.

The primary measurement is the nPM1300 `IBAT` value from the RTT `b` command. With USB disconnected, this is the battery current supplied to the complete NanoStat system. Use the sign convention printed by the firmware: positive current is discharge and negative current is charging. Record at least five readings for idle, BLE-connected idle, active SWV, post-scan High-Z, and display/BLE activity. The current readings should be averaged by state; a single instantaneous reading is not a lifetime measurement.

For the practical duty-cycle estimate, use one measurement per hour for approximately 16 daytime hours and no measurements overnight. Record scan count, elapsed time, start/end battery voltage, and average current. If a full 16-hour test is impractical, use measured state currents to calculate a conservative daily charge budget and label it as an estimate.

For each state, calculate the mean and standard deviation of `IBAT`. For the duty-cycle estimate, use:

```text
daily charge = I_idle * 24 h
             + (I_active - I_idle) * total active scan time
             + BLE/display workflow overhead
runtime days = usable battery capacity / daily charge
```

Because the SWV scan is short, the idle/BLE-connected current and the real measurement schedule will usually dominate the estimate. A full-day battery test is therefore more useful than trying to infer lifetime from one active-scan snapshot.

## Report placement

Methods: power measurement and duty-cycle definition. Results: system-level performance against requirements. Use measured average current for the lifetime estimate; do not infer capacity from voltage alone.

## Required files

Add RTT logs, battery snapshots, charger configuration, runtime calculation code, and plots here. Do not mix USB-powered and battery-powered measurements in the battery-lifetime dataset.
