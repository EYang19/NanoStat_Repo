# Post-measurement high-Z and cell-off verification

## Purpose

Verify that the electrode paths are opened and the AFE is disabled after a scan, reducing unintended sensor polarization during electrode changes.

## Completed multimeter verification

The High-Z validation test was configured to step the electrode potential over approximately `-400 mV` to `-200 mV`, while the multimeter measured the voltage between `RE` and `WE`.

The multimeter was not sufficiently fast or precise to resolve the individual SWV steps or their transient response. However, a clear state change was observed:

- During the driven test, a non-zero `RE-WE` voltage was observed;
- After pressing `Apply High-Z`, the reading returned to approximately `0.00 V`;
- The RTT log simultaneously reported `SW_MATRIX_OPEN=1`, `LPDAC_OFF=1`, `LPTIA_OPEN=1`, `HSTIA_OPEN=1`, and `ADC_OFF=1`.

This verifies that the electrode path is released and the AFE outputs are disabled after the High-Z command. It does not constitute a precision measurement of the `-400 mV` to `-200 mV` transient waveform, which would require an oscilloscope or a faster differential measurement instrument.

For repeat checks, use the same `0.995 MOhm` dummy resistor, measure `RE-WE` in DC-voltage mode, run the High-Z test, and compare the reading before and after `Apply High-Z`. The webapp `Reset High-Z Test` control can re-arm the test without rebooting NanoStat.

For cell-off verification, measure the AD5941 analogue rail (`AVDD`, LDO2 output) and digital/IO rail (`DVDD` or LS1 output) relative to NanoStat ground before the shutdown and after the safe shutdown sequence. Do not measure `VSYS`, because VSYS is the system rail and is expected to remain powered while the AFE rails are disabled.

If the dummy resistor is removed after power-off, a continuity test is only a wiring check. It cannot prove the switch matrix is open while the resistor remains connected, because the resistor itself provides a DC path.

## Suggested report wording

> A multimeter was used to monitor the `RE-WE` voltage during the High-Z validation procedure. The instrument could not resolve the individual SWV transients, but the electrode voltage returned to approximately `0 V` after High-Z was applied. Together with the firmware event flags confirming an open switch matrix and disabled AFE paths, this verifies the post-measurement electrode-release function.

## Report placement

Methods: measurement lifecycle and safety state. Results: firmware/hardware verification. Do not claim zero electrode current from a multimeter reading alone; distinguish firmware state evidence from electrical measurement evidence.

## Required files

Add RTT logs, meter/oscilloscope records if available, firmware version, and a short interpretation here.

The NI myDAQ end-of-scan evidence figure is:

- `figures/highz_transition_evidence.png`

It shows the final SWV pulse and the subsequent return of the measured `WE-RE`
voltage toward `0 mV` for the 5 mV, 2 mV and 1 mV segmented captures. The
figure is kept separate from the SWV-profile comparison in test 02 so that
High-Z is treated as a post-measurement safety-state result rather than part of
the waveform-quality comparison.
