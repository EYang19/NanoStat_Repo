# PBS Aptasensor Validation

This directory preserves the sensor datasets used to compare NanoStat and Autolab measurements.

## Data Groups

- `raw_and_processed/earlystagenanostatdata/`: early NanoStat sensor sessions, including failed or damaged-sensor trials retained for provenance;
- `raw_and_processed/sensortest819_nanostat/`: later NanoStat measurements;
- `raw_and_processed/sensortest820_autolab/`: Autolab measurements from the following day;
- `raw_and_processed/analysis/`: original comparison tables and outputs;
- `analysis_outputs/`: generated figures, response tables, blank analyses, and Autolab processing outputs;
- root Python files: scripts used to generate the principal sensor comparisons.

## Interpretation

The study demonstrates that NanoStat can acquire an SWV methylene-blue peak from a three-electrode aptasensor in PBS. The highest tested concentration gave the clearest response on both NanoStat and Autolab. The lower-concentration measurements were not consistently separated from blank variability.

The archive includes reused sensors, sessions on different days, unequal repeat counts, and different staircase step sizes between instruments. It is therefore unsuitable for claiming a calibrated cortisol response or chemical limit of detection. These limitations are part of the result and are retained in the repository rather than hidden by selecting only favourable traces.

