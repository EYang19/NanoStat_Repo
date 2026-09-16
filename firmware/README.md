# NanoStat Firmware

This directory contains the nRF Connect SDK application for the NanoStat custom nRF52840 board.

## Prerequisites

- nRF Connect SDK `v2.9.0`;
- a configured Zephyr `west` workspace;
- a compatible SWD probe for flashing the nRF52840;
- the NanoStat PCB or equivalent wiring for the external devices.

## Build

From this directory:

```bash
west build -p always -b NanoStat/nrf52840 . -- -DBOARD_ROOT="$PWD"
```

Flash using the normal west runner configured for the connected probe:

```bash
west flash
```

## Source Layout

```text
src/                         application modules
boards/.../NanoStat/         custom board definition and devicetree
lib/ad5940lib/               Analog Devices AD5940/AD5941 library
prj.conf                     Zephyr and subsystem configuration
CMakeLists.txt               application build definition
```

## Hardware Interfaces

- SPI to the AD5941;
- I2C to the nPM1300 and OLED;
- QSPI provision for external flash;
- USB CDC ACM for development console output;
- GPIO for AD5941 interrupt/reset/chip select and local buttons;
- BLE Nordic UART Service for the browser-facing protocol.

## Measurement Notes

The SWV protocol, analogue path, RTIA, ADC PGA, digital filter, endpoint window, and sequence segmentation are configured in `src/ad5941_app.c`. The current code reflects the final project-development state and includes experimental modes. Review constants and command handlers before treating a build as a fixed instrument release.

The ADI library in `lib/ad5940lib/` retains its own licence and source notices.

