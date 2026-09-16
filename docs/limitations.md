# Limitations and Future Work

## Current Evidence Limits

- Electrical accuracy was demonstrated at selected known-load operating points, not across the complete AD5941 current range.
- Minimum detectable current and instrumental limit of detection were not established with a traceable injected-current sweep.
- The fine-step `1 mV` scan is segmented and contains inter-block delays.
- Waveform comparison used offline alignment because a shared hardware trigger was unavailable.
- Guard effectiveness was not isolated experimentally.
- Battery lifetime was estimated, not measured through a full use-and-recharge cycle.
- The BLE range test was performed in a narrow unobstructed corridor and is not a general propagation specification.
- Sensor experiments used PBS, reused sensors, limited repeats, and measurements on different days.
- Saliva matrix effects, selectivity, calibration, and chemical detection limit remain untested.

## Electrical Characterisation Needed for Publication

1. Characterise HSTIA and LPTIA paths separately over multiple RTIA and PGA settings.
2. Measure input-referred RMS noise and peak-to-peak noise with a shielded zero-current or equivalent input condition.
3. Inject traceable currents through precision resistors across each intended range.
4. Report gain error, offset, linearity, usable full scale, bandwidth, and saturation limits.
5. Define current resolution statistically, for example one ADC code, RMS noise, `3 sigma`, and `10 sigma`, without conflating these quantities.
6. Repeat the tests on multiple boards and across temperature where possible.
7. Compare guard enabled, guard disabled, clean, and deliberately contaminated board conditions.

## Firmware Improvements

- Replace the segmented fine-step pause with a rolling sequencer-block update or a continuous sequence that fits the available SRAM.
- Add measurement-state recovery and FIFO-overflow diagnostics.
- Add automatic inactivity shutdown after confirming that acquisition and transfer are idle.
- Add versioned metadata describing firmware commit, analogue path, RTIA, PGA, filters, clocks, and protocol parameters in every export.

## Sensor Study Improvements

- Use fresh independently prepared sensors for each concentration or a validated regeneration procedure.
- Randomise concentration order and instrument order.
- Use matched SWV parameters on NanoStat and the reference instrument.
- Acquire sufficient blank and low-concentration replicates to calculate uncertainty and a defensible detection limit.
- Progress from PBS to controlled artificial saliva and then real saliva only after matrix controls are established.

