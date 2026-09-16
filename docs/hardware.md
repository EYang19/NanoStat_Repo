# Hardware Summary

## Major Components

| Component | Function |
|---|---|
| AD5941 | Electrochemical analogue front end and sequencer |
| nRF52840 | Embedded controller and Bluetooth Low Energy radio |
| nPM1300 | Battery charger, regulators, power control, and current monitoring |
| ADA4505-2 | Low-power buffers used to drive guard potentials without loading AD5941 bias nodes |
| External precision RTIA | Selectable high-accuracy transimpedance gain element |
| QSPI flash | Non-volatile storage provision |
| OLED | Local device status and measurement display |
| Li-Po cell | Portable power source; the prototype used a nominal `190 mAh` cell |

The complete component list is in [`../hardware/bom/NanoStat_BOM_2026-09-09.csv`](../hardware/bom/NanoStat_BOM_2026-09-09.csv).

## Available Design Files

- Power schematic export: [`../hardware/schematics/schematic_power.png`](../hardware/schematics/schematic_power.png)
- MCU and communications schematic export: [`../hardware/schematics/schematic_mcu.png`](../hardware/schematics/schematic_mcu.png)
- Analogue front-end schematic export: [`../hardware/schematics/schematic_afe.png`](../hardware/schematics/schematic_afe.png)
- PCB layout export: [`../hardware/pcb/NanoStat_PCB_Layout_2026-09-09.pdf`](../hardware/pcb/NanoStat_PCB_Layout_2026-09-09.pdf)

The original project archive supplied for this repository did not contain native ECAD source files. The exports above are therefore the complete hardware-design material currently available here.

## Current Range and Resolution

The AD5941 data sheet describes a device-level current range of approximately `50 pA` to `3 mA` across different signal paths, gains, filters, and operating conditions. That range is not a single-setting guaranteed NanoStat measurement range.

For a selected feedback resistance `R_TIA` and ADC voltage increment `V_LSB`, the ideal input-referred current increment is approximately:

```text
I_LSB = V_LSB / R_TIA
```

Increasing `R_TIA` or ADC PGA gain reduces the ideal input-referred code width. Practical minimum detectable current remains limited by analogue noise, ADC noise and nonlinearity, filter bandwidth, temperature drift, PCB leakage, interference, and the statistical definition used for detection. A complete instrument specification therefore requires measured noise and injected-current tests for each path and gain setting.

## Portable Interfaces

The enclosure exposes the electrode connectors, USB-C, local controls, and OLED. The SLP button controls power, RESET performs a hardware reset, and the BOOT button is reserved for future firmware functions. BLE provides the normal measurement interface; USB is useful for development and power but can introduce ground-coupled noise when connected to mains-powered equipment.

