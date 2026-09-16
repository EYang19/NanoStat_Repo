# Experiment Archive

The archive is ordered from concept figures through electrical validation, system integration, sensor validation, and retained development evidence.

| ID | Directory | Purpose |
|---|---|---|
| 00 | `00_concept_figures` | Cortisol and electrochemical-method concept figures |
| 01 | `01_dummy_stepheight_repeatability` | Known-load current accuracy and repeatability |
| 02 | `02_swv_voltage_profile_dummycell` | NI myDAQ validation of SWV voltage profiles |
| 03 | `03_post_measurement_highz_celloff` | High-impedance release after measurement |
| 04 | `04_power_consumption_battery_lifetime` | Stage current and modelled battery life |
| 05 | `05_ble_range_data_reliability` | BLE range and transfer checks |
| 06 | `06_usb_vs_battery_dummy_noise` | Power-source and mains-coupling noise comparison |
| 07 | `07_enclosure_system_validation` | Enclosure and integrated-system documentation |
| 08 | `08_sensor_validation` | NanoStat and Autolab PBS aptasensor datasets and analysis |
| 09 | `09_tia_development` | Selected early LPTIA/HSTIA, filter, external RTIA, and blank tests |

Experiments 01-07 use a common structure where available:

```text
README.md
data/
code/
figures/
```

The sensor archive retains its original session filenames because these identify the sensor, concentration, instrument, and acquisition order. See [`../docs/data_and_reproducibility.md`](../docs/data_and_reproducibility.md) before rerunning scripts or interpreting derived values.

