# Data and Reproducibility

## General Layout

Each validation directory contains some or all of the following:

- `data/`: raw captures and derived numeric tables;
- `code/`: scripts used for analysis or figure generation;
- `figures/`: generated plots and photographic evidence;
- `README.md`: protocol, result, and evidence-boundary notes.

The later sensor archive retains its original session-oriented directory structure because filenames encode instrument, sensor, concentration, and run information.

## Python Environment

Create an isolated environment from the repository root:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r analysis_requirements.txt
```

Run scripts from their own directory unless the script states otherwise. Several historical scripts used relative assumptions based on the original analysis folder; inspect the input constants at the top of the script before execution.

## Raw and Derived Data

Raw CSV and text captures are preserved where available. Derived CSV files are also retained so that reported values can be inspected without rerunning every analysis. Generated PNG figures are included because they are used by the dissertation and provide a quick record of the expected script output.

## Waveform Registration

The NI myDAQ and NanoStat captures did not share a hardware trigger. The theoretical and measured SWV profiles were therefore registered offline using the first valid stair near `-450 mV`, followed by a small edge-based timing refinement. Voltage-offset removal was used for shape and increment error calculations. These figures validate relative waveform generation; they are not an absolute triggered timing calibration.

## Sensor Processing

NanoStat and Autolab sensor curves were processed offline with a common conceptual pipeline: smoothing, baseline estimation outside a fixed methylene-blue peak window, baseline subtraction, and peak extraction. Savitzky-Golay filtering with a five-point window (`SG5`) was used in the archived comparison workflow. SG5 is one smoothing method; it is not the same as the AD5941 embedded digital filtering used during ADC acquisition.

## Metadata Caveat

The file `5mVBattery_run2.csv` in the power-source experiment contains stale exported metadata identifying the source as `mac_usb_c`. The laboratory record and filename identify it as a battery run. The analysis follows the experimental record. This is documented rather than silently rewriting the raw capture.

## Excluded Material

The public archive excludes build products, IDE caches, temporary report-generation files, personal planning notes, an unpublished manuscript supplied for background reading, and screenshots extracted from third-party publications. These exclusions do not remove raw data used for the project results.

