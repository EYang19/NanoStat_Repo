# Hardware Exports

This directory collects the hardware material available in the archived NanoStat project folder.

## Contents

- `bom/`: dated bill of materials export;
- `schematics/`: power, MCU/communications, and analogue front-end schematic images;
- `pcb/`: complete PCB layout PDF export;
- `interface/`: web application screenshots used to document system integration.

## Design Summary

NanoStat combines an AD5941 electrochemical analogue front end, nRF52840 wireless controller, nPM1300 power-management IC, external precision TIA resistors, buffered driven guards, QSPI storage provision, OLED, buttons, USB-C, battery connection, SWD, and three electrode interfaces.

## Source-File Status

Native ECAD schematic and layout source files were not present in the source project tree used to assemble this repository. The exported files are suitable for review and documentation but are not sufficient for direct revision or manufacturing without reconstruction and independent design-rule verification.

