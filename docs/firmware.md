# Firmware Overview

## Execution Model

The firmware uses Zephyr through nRF Connect SDK `v2.9.0`. Responsibilities are divided across the following modules:

| Module | Responsibility |
|---|---|
| `main.c` | Device bring-up, command flow, and subsystem integration |
| `ad5941_app.c/.h` | AD5941 configuration, SWV sequence construction, acquisition, and current processing |
| `ad5941_port.c` | Zephyr GPIO/SPI adaptation for the ADI device library |
| `ble_service.c/.h` | BLE Nordic UART Service command and data transport |
| `power_config.c/.h` | nPM1300 rail sequencing and long-press shutdown handling |
| `oled_display.c/.h` | Local OLED status and result display |

## SWV Acquisition

The tested SWV implementation uses the AD5941 sequencer for deterministic waveform timing. ADC data are produced continuously through the configured digital filtering path, placed in the FIFO, and drained by the MCU during the scan.

For each forward and reverse half-pulse, firmware selects samples near the pulse endpoint, after the initial charging transient. A window mean is used rather than repeatedly starting and stopping the ADC for individual points. The differential current is then calculated as the forward endpoint current minus the reverse endpoint current.

## Digital Filtering

The AD5941 analogue input chain contains the analogue anti-aliasing function associated with the ADC input path. Post-ADC processing uses the AD5941 digital decimation filters configured by firmware. The tested high-speed SWV path uses SINC2 output with the notch filter bypassed; exact OSR and clock settings are defined in `ad5941_app.c` and depend on protocol frequency. Offline Savitzky-Golay smoothing is a separate plotting and peak-extraction operation and is not part of the embedded ADC filter.

## Fine-Step Segmentation

The full sequence fits for the coarser step configurations. The `1 mV` implementation divides the scan into sequencer blocks because of command-memory limits. Each block is valid internally, but the current firmware pauses between blocks while the next commands are prepared and started. These pauses are retained in the waveform-validation data and must not be interpreted as ordinary SWV steps.

## BLE Protocol

BLE communication uses a UART-like service for commands, status, and measurement data. BLE is not in the real-time waveform loop. This keeps radio scheduling latency outside the excitation timing path.

## Power State

At the end of measurement, the firmware releases the electrochemical cell into high impedance and disables measurement blocks. The nPM1300 shutdown path supports user-controlled ship mode. A future inactivity timeout can reuse this sequence after checking that no scan or data transfer is active.

