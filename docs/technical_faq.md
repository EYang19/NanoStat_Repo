# Technical FAQ

## Is NanoStat limited to cortisol?

No. NanoStat supplies electrochemical excitation and current acquisition. The electrode chemistry determines molecular selectivity. The completed validation focuses on SWV and a cortisol aptasensor in PBS, while the AD5941 hardware can support other electrochemical methods such as cyclic voltammetry, differential-pulse voltammetry, chronoamperometry, and impedance measurements. Those additional methods are platform capabilities, not all fully implemented and validated NanoStat protocols.

## What is the minimum measurable current?

There is no single defensible measured value yet. The ideal current increment is determined by ADC range, PGA gain, and RTIA, but minimum detectable current is set by measured input-referred noise and drift. The AD5941 data-sheet statement of approximately `50 pA` to `3 mA` is a device-level range across multiple paths and settings. It is not a guaranteed NanoStat limit under one configuration.

## Why use an external RTIA?

An external precision resistor provides a known gain and can offer better tolerance and stability than relying on a selected internal RTIA setting. The sensor experiments used `330 kOhm`; development records also show operation with `2 MOhm`. Higher resistance gives a smaller ideal current per ADC code but lowers the current that can be accepted before saturation and usually narrows bandwidth.

## How does ADC resolution map to current?

For voltage increment `V_LSB` and transimpedance `R_TIA`:

```text
I_LSB = V_LSB / R_TIA
```

PGA gain changes the input-referred ADC increment. ADC integral nonlinearity, analogue noise, RTIA tolerance, and offset remain separate error terms. Quoting an ideal LSB without a noise measurement overstates practical resolution.

## Which filters are used?

The ADC input includes its analogue anti-aliasing path. The AD5941 then provides digital sinc decimation and optional notch filtering after conversion. The tested high-speed SWV path uses SINC2 output and bypasses the notch filter. Offline SG5 processing is a five-point Savitzky-Golay smoother used for visualisation and peak extraction; it is separate from the embedded ADC filters.

## Does the ADC turn on only at the endpoint?

No. In the tested implementation, ADC acquisition is continuous during the relevant scan interval. Firmware selects and averages samples near the end of each half-pulse. This avoids repeated ADC start-up transients and follows the general purpose of end-of-pulse sampling: reduce the contribution of the initial charging current while retaining the faradaic response.

## How does the FIFO work?

The AD5941 FIFO buffers filtered ADC words. During a scan, the nRF52840 services FIFO events and transfers chunks over SPI into MCU RAM. The complete `-450 mV` to `0 mV` scan is not left in the AD5941 until the end. BLE transfer is outside the time-critical sequencer loop.

## Why does the segmented `1 mV` mode pause?

The fine scan requires more sequencer commands than the chosen single-block layout. Firmware divides it into blocks, then prepares and starts the next block from the MCU. That transition introduced measured hold intervals of approximately `0.37 s` and `0.30 s`. The delay can be reduced substantially with prebuilt blocks and tighter interrupt handling, but a genuinely continuous transition requires an explicitly designed rolling sequence; the AD5941 does not expose automatic hardware double-buffer swapping of sequencer SRAM.

## What do the driven guards protect?

They surround sensitive WE, RE, and TIA high-impedance nodes. A guard is driven close to the protected node potential, reducing voltage across surface contamination and therefore reducing leakage into the measurement node. WE/TIA leakage appears directly as current error. RE leakage can load and polarise the reference path, shifting the controlled electrode potential. The guard buffers were added because the AD5941 bias nodes should not directly drive substantial guard capacitance.

## Does the guard buffer phase delay affect the measured current?

The guard is not in the intended electrochemical signal path. Its finite bandwidth can leave a transient voltage difference between guard and protected node during fast changes, temporarily reducing guard effectiveness. It should not directly alter the TIA transfer function unless parasitic coupling, loading, or instability is introduced. This is a layout and stability issue that should be checked experimentally.

## Why can a concentration trace fall below its blank?

The measured peak includes sensor-to-sensor variation, surface history, drift, baseline-fit uncertainty, handling, and electronic noise. In the archived study, sensor reuse and different-day measurements were important confounds. A lower trace at one concentration therefore does not demonstrate a true inverse biochemical response. This is why the data are reported as a proof of sensor readout rather than a calibration.

## What is common between NanoStat and Autolab processing?

The offline comparison applies the same conceptual SG5 smoothing, two-shoulder least-squares baseline, fixed methylene-blue peak window, baseline subtraction, and peak extraction to both datasets. Their acquisition hardware and staircase step sizes were not identical, so common post-processing does not make the experiments perfectly matched.

## Was battery life measured?

Active-stage current was measured and integrated into a usage model. Ship-mode consumption was taken from the nPM1300 data sheet. The result is an engineering estimate for different daily measurement counts, not a complete discharge measurement of the assembled device.

