# System Architecture

NanoStat separates time-critical electrochemical control from user-interface and wireless tasks.

## Hardware Domains

| Domain | Main device | Responsibility |
|---|---|---|
| Electrochemical analogue front end | AD5941 | Potentiostat loop, waveform generation, TIA paths, filtering, ADC, sequencer, and FIFO |
| Embedded control and radio | nRF52840 | Measurement configuration, SPI transfer, endpoint processing, local UI, data buffering, and BLE |
| Power management | nPM1300 | Li-Po charging, regulated rails, current reporting, button handling, and ship mode |
| Storage | External QSPI flash | Provision for non-volatile measurement storage |
| Local interface | OLED and buttons | Status, parameters, results, power control, and reset |
| Remote interface | Browser application | Protocol configuration, acquisition control, plotting, and CSV export |

## Electrochemical Signal Path

The AD5941 applies a programmed potential between the working electrode (WE) and reference electrode (RE). The RE input is sensed at high impedance so that negligible current is intentionally drawn from the reference electrode. Cell current flows between WE and counter electrode (CE). The selected TIA converts WE current to voltage, which is filtered and digitised by the AD5941 ADC.

The sensor-validation configuration used the high-speed TIA path with an external `330 kOhm` precision feedback resistor. Earlier development tests also exercised a `2 MOhm` external feedback configuration. Higher transimpedance gain improves current-to-voltage conversion but reduces the allowable current before the analogue path saturates and imposes stricter noise, leakage, and bandwidth requirements.

## Timing and Data Movement

The AD5941 sequencer executes SWV timing locally. This prevents BLE scheduling and the nRF52840 operating system from directly modulating pulse widths.

During acquisition, filtered ADC words accumulate in the AD5941 FIFO. The nRF52840 services FIFO interrupts and transfers data over SPI into MCU memory. It does not wait for the entire potential sweep to remain inside the AD5941. Endpoint extraction and BLE transfer are performed from the MCU-side data after acquisition.

The segmented fine-step mode reloads sequencer commands between blocks. The current implementation includes a software-controlled margin during each transition, producing visible hold intervals. A future rolling two-block sequencer scheme could prepare the next block while the current block runs, but AD5941 sequencer SRAM is not a hardware double buffer and the transition logic must be implemented explicitly.

## PCB Strategy

The four-layer PCB separates analogue, radio, digital, and power functions. Sensitive WE, RE, and TIA high-impedance nodes are surrounded by buffered driven guards. A guard is driven close to the protected node voltage, reducing the voltage difference across contamination and surface-leakage paths. This is a preventative design measure for higher transimpedance gains, low-current saliva measurements, and disposable electrode interfaces. The present project did not include an isolated guard-on/guard-off experiment.

## Measurement Completion

After a scan, firmware opens the electrode switches, disables the DAC, TIA, ADC, and waveform generator as appropriate, and leaves the sensor connection in a high-impedance state. This limits unintended post-measurement polarisation.

