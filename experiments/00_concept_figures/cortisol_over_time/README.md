# Cortisol over time

## Purpose

This folder contains an original redraw of published salivary cortisol summary data for a Chapter 1 background figure. The three panels show different biological time scales and should not be joined into one continuous curve.

- Panel a: model-derived diurnal reference percentiles for adults aged 21-30 years, assuming awakening at 07:00.
- Panel b: mean cortisol awakening response on a neutral day and an examination day in 25 female students; error bars show SD.
- Panel c: mean response to the Trier Social Stress Test (TSST) and the matched control condition; error bars show SD (stress n=19, control n=16).

## Suggested report use

Place the combined figure in Chapter 1 after the text introducing circadian variation and acute stress responses. It supports the need for time-resolved cortisol measurement: concentration changes over the day, rises after awakening, and can change rapidly under acute stress.

Suggested caption:

> Published salivary cortisol profiles across three time scales: (a) model-derived diurnal reference medians and 5th-95th percentiles for adults aged 21-30 years; (b) mean +/- SD awakening responses on neutral and examination days; and (c) mean +/- SD responses to acute social stress and the matched control condition. Data redrawn from Miller et al. (2016), Losiak and Losiak-Pilch (2020), and Luethi et al. (2009).

Do not describe the figure as one cohort or one experiment. Panel a contains reference percentiles, whereas panels b and c contain study means and SDs.

## Sources

1. Miller, R. et al. (2016). *The CIRCORT database: Reference ranges and seasonal changes in diurnal salivary cortisol derived from a meta-dataset comprised of 15 field studies*. Psychoneuroendocrinology, 73, 16-23. https://doi.org/10.1016/j.psyneuen.2016.07.201
2. Losiak, W. and Losiak-Pilch, J. (2020). *Cortisol Awakening Response, Self-Reported Affect and Exam Performance in Female Students*. Applied Psychophysiology and Biofeedback, 45, 11-16. https://doi.org/10.1007/s10484-019-09449-9
3. Luethi, M., Meier, B. and Sandi, C. (2009). *Stress effects on working memory, explicit memory, and implicit memory for neutral and emotional stimuli in healthy men*. Frontiers in Behavioral Neuroscience, 2, 5. https://doi.org/10.3389/neuro.08.005.2008

## Data notes

- The CIRCORT values were transcribed from Table 3 and are LC/MS-MS-calibrated concentrations in nmol/L.
- The exam paper labels its Table 1 values as `nmol`; the CSV preserves that wording in `reported_unit`. The plotted numerical values are shown on the shared salivary-cortisol concentration scale, with this source-label issue documented here.
- The acute-stress x-axis is categorical because the second and third samples were tied to protocol stages, and the exact elapsed time differed between the stress and control schedules.
- These are published group summaries, not raw participant-level observations. No smoothing or synthetic points were added.

## Rebuild

Run:

```bash
python3 code/plot_cortisol_over_time.py
```

Outputs are written to `figures/cortisol_over_time.png` and `figures/cortisol_over_time.pdf`.
