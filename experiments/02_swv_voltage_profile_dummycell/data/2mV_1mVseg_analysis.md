# Updated 2 mV and 1 mV Segmented SWV Captures

## Direct waveform observations

Both captures were sampled at `100 kS/s` with a nominal `10 us` interval.

| Profile | Samples | Duration | Observed active scan | Segment gaps |
|---|---:|---:|---:|---:|
| 2 mV full sequencer | `896,324` | `8.963 s` | approximately `3.02--4.79 s` | no comparable long gap |
| 1 mV segmented | `893,100` | `8.931 s` | approximately `3.86--8.05 s` | approximately `0.367 s` and `0.317 s` |

The 1 mV segmented capture contains three active groups. The groups contain
approximately 152, 151 and 121 detected 120 Hz cycles. The two pauses correspond
to approximately 44 and 38 nominal 120 Hz periods. During the pauses the
measured voltage remains at an approximately fixed potential rather than
continuing the regular SWV staircase.

## Why the delay matters electrochemically

SWV is a repetitive double-step chronoamperometric experiment. Each cycle
contains a staircase increment and a square-wave pulse, and the current is
usually sampled late in each pulse so that the fast non-faradaic charging
current has decayed. The measured differential current therefore depends on
both the instantaneous potential and the time history of the electrode.

The segmented pauses can affect that history in several ways:

1. **Double-layer charging changes.** If the potential is held constant during
   a pause, the transient charging current decays substantially before the next
   pulse. The first pulse after the pause therefore does not start from the
   same electrochemical state as an ordinary adjacent pulse.
2. **Diffusion-layer evolution.** A solution-phase redox species continues to
   diffuse during the pause. The diffusion layer can become thicker and the
   concentration profile at the electrode can differ when the next pulse
   begins. This changes the subsequent faradaic transient even if the next
   programmed voltage step is correct.
3. **Surface-confined MB/aptamer state.** For a methylene-blue-labelled,
   surface-bound aptasensor, the label oxidation state and the aptamer
   conformation can relax during a constant-potential hold. The direction and
   size of the effect depend on the hold potential, electron-transfer kinetics
   and the sensor state, so a universal increase or decrease in peak current
   cannot be assumed.
4. **Sampling-history discontinuity.** The nominal frequency remains 120 Hz
   within each segment, but the complete scan is no longer a uniform 120 Hz
   sequence. The first one or more samples after each pause may contain a
   boundary transient and should not automatically be treated as equivalent to
   steady-state samples.

These effects are consistent with the established SWV principle that the
sampling delay is used to let non-faradaic current decay before the Faradaic
current is evaluated. A segmented pause is different: it changes the
electrochemical state before the next pulse, not merely the sampling instant.

## Does placing the gaps outside the peak window solve the problem?

It reduces the risk but does not eliminate it. If the peak region is far from a
segment boundary, the central peak points may still be usable because they are
recorded after several regular cycles. However:

- the baseline shoulders can be shifted by a boundary transient;
- smoothing or baseline subtraction can spread a discontinuity into nearby
  points;
- the peak current can be affected if the electrode reaction has a relaxation
  time comparable with `0.3--0.4 s`;
- a surface-bound aptasensor can retain a state change after the scan resumes.

For the dummy resistor, these electrochemical memory effects are absent. The
dummy waveform therefore verifies the electrical timing and the existence of
the pauses, but it cannot prove that a real aptasensor gives the same response
after the pauses.

## Recommended treatment

For future segmented measurements:

1. Keep the segment pause duration and hold potential deterministic.
2. Place boundaries outside the expected peak and both baseline shoulders.
3. Discard or flag the first few cycles after each pause when extracting a
   peak.
4. Use the same segmentation pattern for every sensor and every concentration.
5. Compare segmented 1 mV against a continuous 1 mV sequence on at least a
   dummy cell and, if possible, one regenerated aptasensor.

For the current report, the defensible statement is:

> The segmented 1 mV waveform preserved the nominal 1 mV staircase within
> each segment, but introduced two approximately 0.3--0.4 s holds. These holds
> break the uniform SWV timing and may alter double-layer charging, diffusion
> and surface-confined redox history. Locating the holds away from the expected
> peak reduces direct peak contamination, but does not make the segmented scan
> electrochemically identical to a continuous sequencer.

## References for the interpretation

- [Continuous Square Wave Voltammetry for High Information Content
  Interrogation of Conformation Switching Sensors](https://pmc.ncbi.nlm.nih.gov/articles/PMC9936610/)
  discusses end-of-pulse sampling and capacitive-current decay.
- [Characterizing electrode reactions by multisampling in square-wave
  voltammetry](https://doi.org/10.1016/j.electacta.2016.07.128) describes SWV
  as a repetitive double-step chronoamperometric experiment.
- [Comparison of voltammetric methods used in the interrogation of
  electrochemical aptamer-based sensors](https://doi.org/10.1039/D3SD00083D)
  discusses SWV timing and diffusion-layer effects in aptamer sensors.
