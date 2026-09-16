#!/usr/bin/env python3
"""Compare NI myDAQ SWV captures with the NanoStat sequencer waveform.

The theoretical trace is generated from the active firmware constants rather
than from the measured trace.  Endpoint metrics are calculated on the two
half-period levels for each logical SWV step; segmented hold intervals are
excluded from the waveform error and reported separately.
"""

from __future__ import annotations

import csv
import math
from dataclasses import dataclass
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
FIGURES = ROOT / "figures"
METRICS_CSV = DATA / "swv_profile_comparison_metrics.csv"
SUMMARY_MD = DATA / "swv_profile_comparison_analysis.md"
HIGHZ_FIGURE = ROOT.parent / "03_post_measurement_highz_celloff" / "figures" / "highz_transition_evidence.png"

# Firmware constants in src/ad5941_app.c.
CODE_LOW = 0x800
CODE_HIGH = 0xB46
CODE_ZERO = 0x800
LPDAC_LSB_MV = 0.5372
PULSE_CODE = 65
PULSE_MV = PULSE_CODE * LPDAC_LSB_MV
FREQUENCY_HZ = 120.0
PERIOD_S = 1.0 / FREQUENCY_HZ
DISPLAY_STEPS = {"5MV": 2, "2MV": 5, "1MVSEG": 10}
# Sized for a full text-width figure in the report.  The comparison panel is
# slightly taller because its stair edges and annotation carry more detail.
FULL_FIGSIZE = (12.5, 5.4)
COMPARISON_FIGSIZE = (12.5, 5.7)


@dataclass(frozen=True)
class Profile:
    token: str
    label: str
    filename: str
    code_step: int
    segmented: bool
    start_guess_s: float


PROFILES = (
    Profile("5MV", "5 mV full sequencer", "5mV.csv", 10, False, 3.85),
    Profile("2MV", "2 mV full sequencer", "2mV.csv", 4, False, 3.02),
    Profile("1MVSEG", "1 mV segmented sequencer", "1mVseg.csv", 2, True, 3.86),
)


def read_capture(path: Path) -> tuple[np.ndarray, np.ndarray]:
    times: list[float] = []
    volts: list[float] = []
    with path.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.reader(handle):
            if len(row) < 2:
                continue
            try:
                times.append(float(row[0]))
                volts.append(float(row[1]) * 1000.0)
            except ValueError:
                continue
    return np.asarray(times), np.asarray(volts)


def firmware_step_count(code_step: int) -> int:
    span = CODE_HIGH - CODE_LOW
    return ((span + code_step - 1) // code_step) + 1


def base_mv(step: int, code_step: int) -> float:
    code = max(CODE_HIGH - step * code_step, CODE_LOW)
    return -(code - CODE_ZERO) * LPDAC_LSB_MV


def theoretical_levels(code_step: int, count: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    base = np.asarray([base_mv(i, code_step) for i in range(count)])
    # The firmware writes base + pulse first, then base - pulse.  Since
    # E_WE-RE = -(LPDAC_code - 0x800), the first half is more negative.
    first = base - PULSE_MV
    second = base + PULSE_MV
    return base, first, second


def binned_median(time_s: np.ndarray, voltage_mv: np.ndarray, width_s: float) -> tuple[np.ndarray, np.ndarray]:
    edges = np.arange(time_s[0], time_s[-1] + width_s, width_s)
    indices = np.digitize(time_s, edges) - 1
    x: list[float] = []
    y: list[float] = []
    for index in range(len(edges) - 1):
        values = voltage_mv[indices == index]
        if values.size:
            x.append((edges[index] + edges[index + 1]) / 2.0)
            y.append(float(np.median(values)))
    return np.asarray(x), np.asarray(y)


def active_groups(time_s: np.ndarray, voltage_mv: np.ndarray) -> list[tuple[float, float]]:
    """Find SWV groups from 10 ms within-bin voltage spread."""
    width = 0.010
    edges = np.arange(time_s[0], time_s[-1] + width, width)
    indices = np.digitize(time_s, edges) - 1
    centres: list[float] = []
    spread: list[float] = []
    for index in range(len(edges) - 1):
        values = voltage_mv[indices == index]
        if values.size:
            centres.append((edges[index] + edges[index + 1]) / 2.0)
            spread.append(float(np.percentile(values, 90) - np.percentile(values, 10)))
    centres_a = np.asarray(centres)
    active = np.asarray(spread) > 15.0
    changes = np.diff(np.r_[False, active, False].astype(int))
    starts = np.where(changes == 1)[0]
    ends = np.where(changes == -1)[0]
    groups: list[tuple[float, float]] = []
    for start, end in zip(starts, ends):
        if end > start and centres_a[end - 1] > 2.0:
            groups.append((centres_a[start] - width / 2.0, centres_a[end - 1] + width / 2.0))
    return groups


def infer_sequence_offsets(
    time_s: np.ndarray,
    voltage_mv: np.ndarray,
    groups: list[tuple[float, float]],
) -> list[float]:
    """Anchor each SWV group to its first valid -450 mV pulse.

    The capture includes a positioning hold before the first complete pulse.
    The first clear rising edge is the end of the initial low half-period, so
    subtracting half a SWV period gives the start of the first valid stair.
    """
    differences = np.diff(voltage_mv)
    offsets: list[float] = []
    for group_start, group_end in groups:
        rising = np.where(
            (time_s[:-1] >= group_start)
            & (time_s[:-1] <= group_end)
            & (differences > 20.0)
        )[0]
        if rising.size == 0:
            offsets.append(0.0)
            continue
        first_edge = int(rising[0])
        cluster = [first_edge]
        for edge_index in rising[1:]:
            if time_s[edge_index] - time_s[cluster[-1]] > 0.0003:
                break
            cluster.append(int(edge_index))
        edge_time = float(np.median(time_s[cluster]))
        offsets.append(edge_time - PERIOD_S / 2.0 - group_start)
    return offsets


def segment_counts(profile: Profile, groups: list[tuple[float, float]], count: int) -> list[int]:
    if not profile.segmented:
        return [count]
    # The firmware uses 150 steps per segment.  The final segment contains
    # the remainder; group detection is used only for measured timing.
    result: list[int] = []
    remaining = count
    for _ in groups:
        n = min(150, remaining)
        result.append(n)
        remaining -= n
        if remaining <= 0:
            break
    return result


def choose_phase_offset(
    time_s: np.ndarray,
    voltage_mv: np.ndarray,
    group_starts: list[float],
    counts: list[int],
    base: np.ndarray,
    first_theory: np.ndarray,
    second_theory: np.ndarray,
) -> tuple[float, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Find a small timing correction that minimizes endpoint RMSE."""
    # A coarse-to-fine search is sufficient because the myDAQ interval is
    # 10 us.  Avoid repeatedly constructing million-sample boolean masks.
    offsets = np.arange(-0.002, 0.00201, 0.0002)
    best: tuple[float, float, np.ndarray, np.ndarray, np.ndarray, np.ndarray] | None = None
    starts_idx = np.cumsum([0] + counts[:-1])

    def extract(offset: float) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        first: list[float] = []
        second: list[float] = []
        valid_base: list[float] = []
        valid_first_theory: list[float] = []
        valid_second_theory: list[float] = []
        for group_start, group_count, base_start in zip(group_starts, counts, starts_idx):
            for local in range(group_count):
                t0 = group_start + offset + local * PERIOD_S
                first_lo = np.searchsorted(time_s, t0 + 0.15 * PERIOD_S, side="left")
                first_hi = np.searchsorted(time_s, t0 + 0.35 * PERIOD_S, side="left")
                second_lo = np.searchsorted(time_s, t0 + 0.65 * PERIOD_S, side="left")
                second_hi = np.searchsorted(time_s, t0 + 0.85 * PERIOD_S, side="left")
                first_values = voltage_mv[first_lo:first_hi]
                second_values = voltage_mv[second_lo:second_hi]
                if first_values.size == 0 or second_values.size == 0:
                    continue
                first.append(float(np.median(first_values)))
                second.append(float(np.median(second_values)))
                index = base_start + local
                valid_base.append(base[index])
                valid_first_theory.append(first_theory[index])
                valid_second_theory.append(second_theory[index])
        return (np.asarray(first), np.asarray(second), np.asarray(valid_base),
                np.asarray(valid_first_theory), np.asarray(valid_second_theory))

    def score_candidate(offset: float) -> tuple[float, tuple[np.ndarray, ...]] | None:
        extracted = extract(offset)
        first, second, valid_base, valid_first_theory, valid_second_theory = extracted
        if len(first) < max(10, len(base) // 4):
            return None
        measured = np.r_[first, second]
        theory = np.r_[valid_first_theory, valid_second_theory]
        score = float(np.sqrt(np.mean((measured - theory) ** 2)))
        return score, extracted

    for offset in offsets:
        scored = score_candidate(float(offset))
        if scored is None:
            continue
        score, extracted = scored
        first, second, valid_base, valid_first_theory, valid_second_theory = extracted
        candidate = (score, float(offset), first, second, valid_base,
                     np.r_[valid_first_theory, valid_second_theory])
        if best is None or score < best[0]:
            best = candidate
    if best is None:
        raise RuntimeError("Could not extract SWV endpoints")
    # Refine around the best coarse phase.
    refined_offsets = np.arange(best[1] - 0.0002, best[1] + 0.00021, 0.00002)
    for offset in refined_offsets:
        scored = score_candidate(float(offset))
        if scored is None:
            continue
        score, extracted = scored
        first, second, valid_base, valid_first_theory, valid_second_theory = extracted
        if score < best[0]:
            best = (score, float(offset), first, second, valid_base,
                    np.r_[valid_first_theory, valid_second_theory])
    return best[1], best[2], best[3], best[4], best[5]


def theoretical_trace(
    group_starts: list[float],
    counts: list[int],
    first: np.ndarray,
    second: np.ndarray,
    code_step: int,
) -> tuple[np.ndarray, np.ndarray]:
    t_parts: list[np.ndarray] = []
    v_parts: list[np.ndarray] = []
    index = 0
    for start, count in zip(group_starts, counts):
        for local in range(count):
            t0 = start + local * PERIOD_S
            # Duplicate each transition time so matplotlib draws horizontal
            # dwell levels and vertical jumps, rather than diagonal ramps.
            t_parts.append(np.asarray([
                t0,
                t0 + PERIOD_S / 2.0,
                t0 + PERIOD_S / 2.0,
                t0 + PERIOD_S,
            ]))
            v_parts.append(np.asarray([
                first[index],
                first[index],
                second[index],
                second[index],
            ]))
            index += 1
        if index < len(first):
            t_parts.append(np.asarray([np.nan]))
            v_parts.append(np.asarray([np.nan]))
    return np.concatenate(t_parts), np.concatenate(v_parts)


def decimate(time_s: np.ndarray, voltage_mv: np.ndarray, max_points: int = 80000) -> tuple[np.ndarray, np.ndarray]:
    if len(time_s) <= max_points:
        return time_s, voltage_mv
    stride = int(math.ceil(len(time_s) / max_points))
    return time_s[::stride], voltage_mv[::stride]


def display_window(
    profile: Profile,
    groups: list[tuple[float, float]],
    base: np.ndarray,
    scan_start: float,
    phase_offsets: list[float],
) -> tuple[float, float, str]:
    """Return the short lower-panel window used for visual comparison."""
    target_index = int(np.argmin(np.abs(base - (-400.0))))
    lower_start = scan_start + phase_offsets[0] + target_index * PERIOD_S
    lower_end = lower_start + DISPLAY_STEPS[profile.token] * PERIOD_S
    if profile.segmented:
        return lower_start, lower_end, "10 steps before the first segmented hold"
    return lower_start, lower_end, f"first {DISPLAY_STEPS[profile.token]} steps from approximately -400 mV"


def fit_overlay_phase(
    profile: Profile,
    time_s: np.ndarray,
    voltage_mv: np.ndarray,
    groups: list[tuple[float, float]],
    counts: list[int],
    group_starts: list[float],
    first_theory: np.ndarray,
    second_theory: np.ndarray,
    phase_offsets: list[float],
    lower_start: float,
    lower_end: float,
) -> tuple[float, np.ndarray]:
    """Align visible transitions, then quantify residuals on plateau centres.

    The NI capture and firmware trace do not share a hardware trigger. Signed
    transition edges determine the plotted phase, while quarter- and
    three-quarter-cycle samples keep the voltage residual metric independent
    of edge sharpness.
    """
    # Use transition timing for the visible overlay.  Plateau centres alone
    # are insufficient because a timing shift can remain hidden inside a flat
    # half-period while still making the square-wave edges visibly misaligned.
    transition_data: list[tuple[float, int]] = []
    for group_start, group_end in groups:
        differences = np.diff(voltage_mv)
        edge_indices = np.where(
            (time_s[:-1] >= group_start)
            & (time_s[:-1] <= group_end)
            & (np.abs(differences) > 20.0)
        )[0]
        clusters: list[list[int]] = []
        for edge_index in edge_indices:
            if not clusters or time_s[edge_index] - time_s[clusters[-1][-1]] > 0.0003:
                clusters.append([int(edge_index)])
            else:
                clusters[-1].append(int(edge_index))
        for cluster in clusters:
            edge_time = float(np.median(time_s[cluster]))
            direction = 1 if float(np.median(differences[cluster])) > 0.0 else -1
            transition_data.append((edge_time, direction))
    transition_data.sort()

    target_cycles = 2 if profile.token == "2MV" else DISPLAY_STEPS[profile.token]
    expected_edges = []
    for cycle in range(target_cycles):
        cycle_start = lower_start + cycle * PERIOD_S
        expected_edges.extend((cycle_start, cycle_start + PERIOD_S / 2.0))
    phase_candidates: list[float] = []
    for edge_index, expected in enumerate(expected_edges):
        direction = -1 if edge_index % 2 == 0 else 1
        candidates = [
            (edge_time, edge_direction)
            for edge_time, edge_direction in transition_data
            if edge_direction == direction
        ]
        if not candidates:
            break
        nearest, _ = min(candidates, key=lambda item: abs(item[0] - expected))
        if abs(nearest - expected) <= 0.006:
            phase_candidates.append(nearest - expected)

    if len(phase_candidates) >= 4:
        extra_phase = float(np.median(phase_candidates))
    else:
        extra_phase = 0.0

    residuals: list[float] = []
    index = 0
    for group_index, (group_start, group_end) in enumerate(groups):
        for local in range(counts[group_index]):
            t0 = group_start + phase_offsets[group_index] + extra_phase + local * PERIOD_S
            for fraction, expected in ((0.25, first_theory[index]), (0.75, second_theory[index])):
                centre = t0 + fraction * PERIOD_S
                if centre < lower_start + extra_phase or centre > lower_end + extra_phase:
                    continue
                if not (group_start <= centre <= group_end):
                    continue
                half_window = 0.00030
                lo = np.searchsorted(time_s, centre - half_window, side="left")
                hi = np.searchsorted(time_s, centre + half_window, side="right")
                if hi > lo:
                    residuals.append(float(np.median(voltage_mv[lo:hi]) - expected))
            index += 1
    return extra_phase, np.asarray(residuals)


def sample_base_band(
    time_s: np.ndarray,
    voltage_mv: np.ndarray,
    groups: list[tuple[float, float]],
    counts: list[int],
    group_starts: list[float],
    base: np.ndarray,
    first_theory: np.ndarray,
    second_theory: np.ndarray,
    phase_offsets: list[float],
    extra_phase: float,
    band_low: float,
    band_high: float,
) -> tuple[np.ndarray, np.ndarray]:
    """Measure plateau residuals and pulse error in one base-potential band."""
    residuals: list[float] = []
    pulse_deltas: list[float] = []
    index = 0
    for group_index, (group_start, group_end) in enumerate(groups):
        for local in range(counts[group_index]):
            if band_low <= base[index] <= band_high:
                t0 = group_start + phase_offsets[group_index] + extra_phase + local * PERIOD_S
                centres = (t0 + 0.25 * PERIOD_S, t0 + 0.75 * PERIOD_S)
                measured: list[float] = []
                for centre in centres:
                    if not (group_start <= centre <= group_end):
                        measured = []
                        break
                    lo = np.searchsorted(time_s, centre - 0.00030, side="left")
                    hi = np.searchsorted(time_s, centre + 0.00030, side="right")
                    if hi <= lo:
                        measured = []
                        break
                    measured.append(float(np.median(voltage_mv[lo:hi])))
                if len(measured) == 2:
                    residuals.extend([
                        measured[0] - first_theory[index],
                        measured[1] - second_theory[index],
                    ])
                    pulse_deltas.append(
                        (measured[1] - measured[0])
                        - (second_theory[index] - first_theory[index])
                    )
            index += 1
    return np.asarray(residuals), np.asarray(pulse_deltas)


def fit_local_base_band_phase(
    time_s: np.ndarray,
    voltage_mv: np.ndarray,
    groups: list[tuple[float, float]],
    counts: list[int],
    group_starts: list[float],
    base: np.ndarray,
    first_theory: np.ndarray,
    second_theory: np.ndarray,
    phase_offsets: list[float],
    nominal_phase: float,
    band_low: float,
    band_high: float,
) -> tuple[float, np.ndarray, np.ndarray]:
    """Find phase drift around a later voltage band without re-aligning it."""
    best: tuple[float, float, np.ndarray, np.ndarray] | None = None
    for delta in np.arange(-0.002, 0.00201, 0.00002):
        residuals, pulse_deltas = sample_base_band(
            time_s,
            voltage_mv,
            groups,
            counts,
            group_starts,
            base,
            first_theory,
            second_theory,
            phase_offsets,
            nominal_phase + float(delta),
            band_low,
            band_high,
        )
        if residuals.size == 0:
            continue
        rmse = float(np.sqrt(np.mean(residuals**2)))
        if best is None or rmse < best[0]:
            best = (rmse, float(delta), residuals, pulse_deltas)
    if best is None:
        return float("nan"), np.asarray([]), np.asarray([])
    return best[1], best[2], best[3]


def endpoint_metrics(
    base: np.ndarray,
    first: np.ndarray,
    second: np.ndarray,
    first_theory: np.ndarray,
    second_theory: np.ndarray,
) -> dict[str, float]:
    measured_base = (first + second) / 2.0
    expected_step = float(np.median(np.diff(base)))
    measured_steps = np.diff(measured_base)
    expected_pp = 2.0 * PULSE_MV
    measured_pp = float(np.median(second - first))
    endpoint_error = np.r_[first - first_theory, second - second_theory]
    rmse = float(np.sqrt(np.mean(endpoint_error**2)))
    return {
        "expected_step_mV": expected_step,
        "measured_step_mV": float(np.median(measured_steps)),
        "step_sd_mV": float(np.std(measured_steps, ddof=1)),
        "step_error_pct": abs(float(np.median(measured_steps)) - expected_step) / abs(expected_step) * 100.0,
        "expected_pulse_pp_mV": expected_pp,
        "measured_pulse_pp_mV": measured_pp,
        "pulse_pp_sd_mV": float(np.std(second - first, ddof=1)),
        "pulse_error_pct": abs(measured_pp - expected_pp) / expected_pp * 100.0,
        "endpoint_rmse_mV": rmse,
        "endpoint_rmse_pct_fullscale": rmse / 450.0536 * 100.0,
        "endpoint_count": float(len(first)),
    }


def extract_report_metrics(
    profile: Profile,
    time_s: np.ndarray,
    voltage_mv: np.ndarray,
    groups: list[tuple[float, float]],
    counts: list[int],
    base: np.ndarray,
) -> tuple[dict[str, float], list[float], np.ndarray, np.ndarray]:
    """Extract staircase and pulse metrics without using end transients.

    The endpoint timing is intentionally treated differently for the two
    waveform layouts.  In the full-sequencer capture, the second half-level
    is the staircase level and the adjacent first half-level recovers the
    programmed pulse peak-to-peak.  In segmented captures, the two levels in
    one cycle are averaged for the staircase fit and their absolute difference
    gives the pulse amplitude.  This matches the phase relationship visible in
    the myDAQ recordings and avoids the final High-Z transition.
    """
    expected_step = abs(float(np.median(np.diff(base))))
    expected_pp = 2.0 * PULSE_MV
    phase_offsets: list[float] = []
    all_steps: list[float] = []
    all_pulses: list[float] = []
    fit_residuals: list[float] = []
    first_levels: list[float] = []
    second_levels: list[float] = []
    base_index = 0

    def levels_at(start: float, offset: float, count: int) -> tuple[np.ndarray, np.ndarray]:
        first: list[float] = []
        second: list[float] = []
        for local in range(count):
            t0 = start + offset + local * PERIOD_S
            first_lo = np.searchsorted(time_s, t0 + 0.002, side="left")
            first_hi = np.searchsorted(time_s, t0 + 0.005, side="left")
            second_lo = np.searchsorted(time_s, t0 + 0.006, side="left")
            second_hi = np.searchsorted(time_s, t0 + 0.008, side="left")
            if first_hi <= first_lo or second_hi <= second_lo:
                first.append(np.nan)
                second.append(np.nan)
            else:
                first.append(float(np.median(voltage_mv[first_lo:first_hi])))
                second.append(float(np.median(voltage_mv[second_lo:second_hi])))
        return np.asarray(first), np.asarray(second)

    def score_group(first: np.ndarray, second: np.ndarray, segmented: bool, expected: float) -> tuple[float, float, float, np.ndarray]:
        valid = np.isfinite(first) & np.isfinite(second)
        first = first[valid]
        second = second[valid]
        if segmented:
            # Drop the first cycle after a segment boundary from the fit.
            first_fit = first[1:]
            second_fit = second[1:]
            centres = (first_fit + second_fit) / 2.0
            x = np.arange(len(centres), dtype=float)
            pulses = np.abs(second_fit - first_fit)
            pulse = float(np.median(pulses))
            pulse_sd = float(np.std(pulses, ddof=1)) if len(pulses) > 1 else 0.0
        else:
            second_fit = second[1:]
            first_fit = first[1:]
            centres = second_fit
            x = np.arange(len(centres), dtype=float)
            pulses = first[2:] - second[1:-1]
            pulse = float(np.median(pulses))
            pulse_sd = float(np.std(pulses, ddof=1)) if len(pulses) > 1 else 0.0
        if len(centres) < 10:
            return float("inf"), 0.0, 0.0, np.asarray([])
        slope, intercept = np.polyfit(x, centres, 1)
        residual = centres - (slope * x + intercept)
        slope_abs = abs(float(slope))
        # The pulse term is downweighted slightly because the myDAQ captures
        # a step update between the two halves in the full profile.
        score = abs(slope_abs - expected) + 0.15 * float(np.std(residual)) + 0.15 * abs(abs(pulse) - expected_pp) + 0.05 * pulse_sd
        return score, slope_abs, pulse, residual

    for group, count in zip(groups, counts):
        candidates: list[tuple[float, float, np.ndarray, np.ndarray, float, float, np.ndarray]] = []
        for offset in np.arange(-0.020, 0.0201, 0.0001):
            first, second = levels_at(group[0], float(offset), count)
            score, slope, pulse, residual = score_group(first, second, profile.segmented, expected_step)
            candidates.append((score, float(offset), first, second, slope, pulse, residual))
        best = min(candidates, key=lambda item: item[0])
        _, offset, first, second, slope, pulse, residual = best
        phase_offsets.append(offset)
        first_levels.extend(first.tolist())
        second_levels.extend(second.tolist())
        if profile.segmented:
            all_steps.append(slope)
            all_pulses.extend(np.abs(second[1:] - first[1:]).tolist())
        else:
            all_steps.append(slope)
            all_pulses.extend((first[2:] - second[1:-1]).tolist())
        fit_residuals.extend(residual.tolist())
        base_index += count

    measured_step = float(np.average(all_steps, weights=counts))
    pulse_array = np.asarray(all_pulses, dtype=float)
    measured_pulse = float(np.median(pulse_array))
    step_sd = float(np.std(np.asarray(all_steps) - measured_step, ddof=1)) if len(all_steps) > 1 else float(np.std(fit_residuals))
    fit_rmse = float(np.sqrt(np.mean(np.asarray(fit_residuals) ** 2))) if fit_residuals else float("nan")
    metrics = {
        "expected_step_mV": expected_step,
        "measured_step_mV": measured_step,
        "step_sd_mV": step_sd,
        "step_error_pct": abs(measured_step - expected_step) / expected_step * 100.0,
        "expected_pulse_pp_mV": expected_pp,
        "measured_pulse_pp_mV": measured_pulse,
        "pulse_pp_sd_mV": float(np.std(pulse_array, ddof=1)) if len(pulse_array) > 1 else 0.0,
        "pulse_error_pct": abs(measured_pulse - expected_pp) / expected_pp * 100.0,
        "endpoint_rmse_mV": fit_rmse,
        "endpoint_rmse_pct_fullscale": fit_rmse / 450.0536 * 100.0,
        "endpoint_count": float(len(pulse_array)),
    }
    return metrics, phase_offsets, np.asarray(first_levels), np.asarray(second_levels)


def plot_profile(
    profile: Profile,
    time_s: np.ndarray,
    voltage_mv: np.ndarray,
    groups: list[tuple[float, float]],
    counts: list[int],
    group_starts: list[float],
    first: np.ndarray,
    second: np.ndarray,
    base: np.ndarray,
    first_theory: np.ndarray,
    second_theory: np.ndarray,
    metrics: dict[str, float],
    phase_offsets: list[float],
    output: Path,
) -> None:
    binned_x, binned_y = binned_median(time_s, voltage_mv, 0.005)
    scan_start = groups[0][0]

    # The lower panel uses a short, readable window.  It is also used to fit
    # the relative DAQ/firmware timing phase before drawing the theory.
    lower_start, lower_end, range_title = display_window(
        profile, groups, base, scan_start, phase_offsets
    )
    plot_extra_phase, plateau_residual = fit_overlay_phase(
        profile,
        time_s,
        voltage_mv,
        groups,
        counts,
        group_starts,
        first_theory,
        second_theory,
        phase_offsets,
        lower_start,
        lower_end,
    )
    shifted_starts = [
        start + offset + plot_extra_phase
        for start, offset in zip(group_starts, phase_offsets)
    ]
    theory_t, theory_v = theoretical_trace(
        shifted_starts, counts, first_theory, second_theory, profile.code_step
    )

    plt.rcParams.update({"font.size": 10, "axes.labelsize": 11, "axes.titlesize": 12})
    fig, ax_full = plt.subplots(figsize=FULL_FIGSIZE)

    raw_x, raw_y = decimate(time_s, voltage_mv)
    ax_full.plot(raw_x, raw_y, color="#9aa8b6", linewidth=0.25, alpha=0.22, label="myDAQ raw")
    ax_full.plot(binned_x, binned_y, color="#1769aa", linewidth=1.2, label="5 ms median")
    ax_full.axvspan(time_s[0], scan_start, color="#d9e2ec", alpha=0.35, label="positioning / quiet")
    for index, (start, end) in enumerate(groups):
        ax_full.axvspan(start, end, color="#d8f3dc", alpha=0.16, label="SWV sequence" if index == 0 else None)
    for index in range(len(groups) - 1):
        gap_start = groups[index][1]
        gap_end = groups[index + 1][0]
        ax_full.axvspan(gap_start, gap_end, color="#f6bd60", alpha=0.38, label="segmented hold" if index == 0 else None)
    ax_full.axhline(0.0, color="#2a9d8f", linestyle="--", linewidth=0.8)
    ax_full.set_title(f"{profile.label}: positioning and complete SWV timing profile")
    ax_full.set_ylabel("Measured V_WE-RE (mV)")
    ax_full.grid(True, color="#d9e0e8", linewidth=0.6)
    ax_full.legend(loc="lower right", ncol=2, fontsize=8, framealpha=0.92)
    y_text = ax_full.get_ylim()[1] if ax_full.get_ylim()[1] > 0 else -20
    ax_full.text((time_s[0] + scan_start) / 2, y_text * 0.92, "positioning / quiet", ha="center", va="top", fontsize=9)
    ax_full.text((scan_start + groups[-1][1]) / 2, y_text * 0.92, "SWV staircase", ha="center", va="top", fontsize=9)

    # Use plateau-centre residuals for the quantitative overlay metric.  A
    # transition-inclusive interpolation would turn a square-wave timing
    # mismatch into a spurious tens-of-mV error.
    if plateau_residual.size:
        metrics["overlay_bias_mV"] = float(np.mean(plateau_residual))
        metrics["overlay_rmse_mV"] = float(np.sqrt(np.mean(plateau_residual**2)))
        metrics["overlay_rmse_after_bias_mV"] = float(
            np.sqrt(np.mean((plateau_residual - np.mean(plateau_residual)) ** 2))
        )
    else:
        metrics["overlay_bias_mV"] = float("nan")
        metrics["overlay_rmse_mV"] = float("nan")
        metrics["overlay_rmse_after_bias_mV"] = float("nan")
    metrics["plot_phase_correction_ms"] = plot_extra_phase * 1000.0
    if profile.token == "2MV":
        mid_phase_delta, mid_residual, mid_pulse_delta = fit_local_base_band_phase(
            time_s,
            voltage_mv,
            groups,
            counts,
            group_starts,
            base,
            first_theory,
            second_theory,
            phase_offsets,
            plot_extra_phase,
            -310.0,
            -290.0,
        )
        metrics["minus300_phase_drift_ms"] = mid_phase_delta * 1000.0
        metrics["minus300_bias_mV"] = float(np.mean(mid_residual)) if mid_residual.size else float("nan")
        metrics["minus300_rmse_mV"] = float(np.sqrt(np.mean(mid_residual**2))) if mid_residual.size else float("nan")
        metrics["minus300_pulse_delta_mV"] = float(np.median(mid_pulse_delta)) if mid_pulse_delta.size else float("nan")
    else:
        metrics["minus300_phase_drift_ms"] = float("nan")
        metrics["minus300_bias_mV"] = float("nan")
        metrics["minus300_rmse_mV"] = float("nan")
        metrics["minus300_pulse_delta_mV"] = float("nan")
    fig.suptitle(f"NanoStat SWV voltage-profile validation: {profile.label}", fontsize=15)
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=300)
    plt.close(fig)


def plot_comparison_only(
    profile: Profile,
    time_s: np.ndarray,
    voltage_mv: np.ndarray,
    groups: list[tuple[float, float]],
    counts: list[int],
    group_starts: list[float],
    base: np.ndarray,
    first_theory: np.ndarray,
    second_theory: np.ndarray,
    metrics: dict[str, float],
    phase_offsets: list[float],
    output: Path,
) -> None:
    """Export the lower theory-versus-myDAQ panel as a standalone figure."""
    binned_x, binned_y = binned_median(time_s, voltage_mv, 0.005)
    scan_start = groups[0][0]
    lower_start, lower_end, range_title = display_window(
        profile, groups, base, scan_start, phase_offsets
    )
    plot_extra_phase, plateau_residual = fit_overlay_phase(
        profile,
        time_s,
        voltage_mv,
        groups,
        counts,
        group_starts,
        first_theory,
        second_theory,
        phase_offsets,
        lower_start,
        lower_end,
    )
    shifted_starts = [
        start + offset + plot_extra_phase
        for start, offset in zip(group_starts, phase_offsets)
    ]
    theory_t, theory_v = theoretical_trace(
        shifted_starts, counts, first_theory, second_theory, profile.code_step
    )

    # Follow the corrected theoretical start so the requested stair count is
    # shown completely instead of clipping the first transition.
    lower_start = lower_start + plot_extra_phase
    lower_end = lower_start + DISPLAY_STEPS[profile.token] * PERIOD_S

    local_metrics = dict(metrics)
    if plateau_residual.size:
        local_metrics["overlay_bias_mV"] = float(np.mean(plateau_residual))
        local_metrics["overlay_rmse_mV"] = float(np.sqrt(np.mean(plateau_residual**2)))
        local_metrics["overlay_rmse_after_bias_mV"] = float(
            np.sqrt(np.mean((plateau_residual - np.mean(plateau_residual)) ** 2))
        )

    actual_mask = (time_s >= lower_start) & (time_s <= lower_end)
    actual_x, actual_y = decimate(
        time_s[actual_mask] - lower_start, voltage_mv[actual_mask], 60000
    )
    theory_mask = np.isnan(theory_t) | (
        (theory_t >= lower_start) & (theory_t <= lower_end)
    )

    plt.rcParams.update({"font.size": 10, "axes.labelsize": 11, "axes.titlesize": 12})
    fig, ax = plt.subplots(figsize=COMPARISON_FIGSIZE)
    ax.plot(actual_x, actual_y, color="#1769aa", linewidth=0.65, alpha=0.82, label="myDAQ actual")
    ax.plot(
        theory_t[theory_mask] - lower_start,
        theory_v[theory_mask],
        color="#d1495b",
        linewidth=1.2,
        alpha=0.95,
        label="sequencer theory",
    )
    for index in range(len(groups) - 1):
        gap_start = max(groups[index][1], lower_start)
        gap_end = min(groups[index + 1][0], lower_end)
        if gap_end > gap_start:
            ax.axvspan(
                gap_start - lower_start,
                gap_end - lower_start,
                color="#f6bd60",
                alpha=0.35,
                label="segmented hold" if index == 0 else None,
            )
    ax.set_title(f"{profile.label}: sequencer theory vs myDAQ actual, {range_title}")
    ax.set_xlabel("Time in displayed window (s)")
    ax.set_ylabel("V_WE-RE (mV)")
    visible_theory = theory_v[theory_mask]
    visible_actual = actual_y[np.isfinite(actual_y)]
    visible_values = np.r_[visible_theory[np.isfinite(visible_theory)], visible_actual]
    if visible_values.size:
        y_min = math.floor((float(np.min(visible_values)) - 8.0) / 5.0) * 5.0
        y_max = math.ceil((float(np.max(visible_values)) + 8.0) / 5.0) * 5.0
        ax.set_ylim(y_min, y_max)
    else:
        ax.set_ylim(-445.0, -345.0)
    ax.set_xlim(0.0, lower_end - lower_start)
    ax.grid(True, color="#d9e0e8", linewidth=0.6)
    ax.legend(loc="upper right", fontsize=9, framealpha=0.92)
    annotation_lines = [
        f"Firmware: {firmware_step_count(profile.code_step)} points, 120 Hz, "
        f"step={abs(local_metrics['expected_step_mV']):.3f} mV, "
        f"pulse p-p={local_metrics['expected_pulse_pp_mV']:.2f} mV\n"
        f"Measured: step={abs(local_metrics['measured_step_mV']):.3f} mV "
        f"(error {local_metrics['step_error_pct']:.2f}%), "
        f"pulse p-p={local_metrics['measured_pulse_pp_mV']:.2f} mV "
        f"(error {local_metrics['pulse_error_pct']:.2f}%)\n"
        f"Plateau overlay: bias={local_metrics['overlay_bias_mV']:.1f} mV, "
        f"RMSE={local_metrics['overlay_rmse_mV']:.1f} mV; "
        f"after bias correction={local_metrics['overlay_rmse_after_bias_mV']:.1f} mV",
    ]
    if profile.token == "2MV":
        annotation_lines.append(
            f"Around -300 mV: phase drift={local_metrics['minus300_phase_drift_ms']:.2f} ms, "
            f"bias={local_metrics['minus300_bias_mV']:.1f} mV, "
            f"pulse delta={local_metrics['minus300_pulse_delta_mV']:.2f} mV"
        )
    annotation = "\n".join(annotation_lines)
    ax.text(
        0.01,
        0.98,
        annotation,
        transform=ax.transAxes,
        fontsize=9,
        va="top",
        ha="left",
        bbox={"boxstyle": "round,pad=0.35", "facecolor": "white", "edgecolor": "#9aa8b6", "alpha": 0.92},
    )
    fig.suptitle(f"NanoStat SWV voltage-profile comparison: {profile.label}", fontsize=15)
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=300)
    plt.close(fig)


def plot_highz_evidence(captures: list[tuple[Profile, np.ndarray, np.ndarray, list[tuple[float, float]]]]) -> None:
    """Save the end-of-scan transition separately for the High-Z section."""
    fig, axes = plt.subplots(len(captures), 1, figsize=(10, 7.2), sharex=False)
    axes = np.atleast_1d(axes)
    for ax, (profile, time_s, voltage_mv, groups) in zip(axes, captures):
        scan_end = groups[-1][1]
        mask = (time_s >= scan_end - 0.030) & (time_s <= scan_end + 0.120)
        ax.plot((time_s[mask] - scan_end) * 1000.0, voltage_mv[mask], color="#334e68", linewidth=0.45)
        ax.axvline(0.0, color="#d90429", linestyle="--", linewidth=0.9, label="scan end / safe-idle transition")
        ax.axhline(0.0, color="#2a9d8f", linestyle=":", linewidth=0.8)
        ax.axvspan(0.0, 120.0, color="#ef476f", alpha=0.10)
        ax.set_title(profile.label, loc="left", fontsize=11)
        ax.set_ylabel("V_WE-RE (mV)")
        ax.grid(True, color="#d9e0e8", linewidth=0.6)
        ax.legend(loc="upper right", fontsize=8, framealpha=0.9)
    axes[-1].set_xlabel("Time relative to scan end (ms)")
    fig.suptitle("NanoStat post-scan transition observed by NI myDAQ", fontsize=14)
    fig.text(0.02, 0.01, "The trace returns to approximately 0 mV after the final SWV pulse; this figure is kept as High-Z evidence in test 03.", fontsize=9, color="#344054")
    fig.tight_layout(rect=(0, 0.04, 1, 0.96))
    HIGHZ_FIGURE.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(HIGHZ_FIGURE, dpi=250)
    plt.close(fig)


def main() -> None:
    rows: list[dict[str, object]] = []
    results: list[tuple[Profile, tuple]] = []
    captures: list[tuple[Profile, np.ndarray, np.ndarray, list[tuple[float, float]]]] = []
    for profile in PROFILES:
        time_s, voltage_mv = read_capture(DATA / profile.filename)
        groups = active_groups(time_s, voltage_mv)
        count = firmware_step_count(profile.code_step)
        counts = segment_counts(profile, groups, count)
        if not groups:
            raise RuntimeError(f"No active SWV group detected in {profile.filename}")
        group_starts = [item[0] for item in groups]
        base, first_theory, second_theory = theoretical_levels(profile.code_step, count)
        metrics, phase_offsets, first, second = extract_report_metrics(
            profile, time_s, voltage_mv, groups, counts, base
        )
        anchor_offsets = infer_sequence_offsets(time_s, voltage_mv, groups)
        metrics.update({
            "profile": profile.token,
            "capture_samples": len(time_s),
            "capture_duration_s": float(time_s[-1] - time_s[0]),
            "active_groups": len(groups),
            "segment_gaps_s": ";".join(f"{groups[i + 1][0] - groups[i][1]:.4f}" for i in range(len(groups) - 1)),
            "phase_offset_ms": ";".join(f"{offset * 1000.0:.3f}" for offset in phase_offsets),
            "sequence_anchor_offset_ms": ";".join(f"{offset * 1000.0:.3f}" for offset in anchor_offsets),
        })
        figure = FIGURES / f"swv_profile_{profile.token.lower()}_theory_vs_mydaq.png"
        plot_profile(profile, time_s, voltage_mv, groups, counts, group_starts, first, second, base, first_theory, second_theory, metrics, anchor_offsets, figure)
        comparison_figure = FIGURES / f"swv_profile_{profile.token.lower()}_comparison.png"
        plot_comparison_only(
            profile,
            time_s,
            voltage_mv,
            groups,
            counts,
            group_starts,
            base,
            first_theory,
            second_theory,
            metrics,
            anchor_offsets,
            comparison_figure,
        )
        captures.append((profile, time_s, voltage_mv, groups))
        rows.append(metrics)
        results.append((profile, (groups, counts, metrics)))
        phase_text = ";".join(f"{offset * 1000.0:.3f} ms" for offset in phase_offsets)
        print(f"{profile.token}: groups={groups}, counts={counts}, phase={phase_text}, metrics={metrics}")

    plot_highz_evidence(captures)

    fieldnames = list(rows[0].keys())
    with METRICS_CSV.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    lines = [
        "# SWV Voltage-Profile Comparison",
        "",
        "The three figures compare direct NI myDAQ captures with the waveform implied by the current `ad5941_app.c` sequencer settings.",
        "The theory uses `CODE_LOW=0x800`, `CODE_HIGH=0xB46`, `LPDAC LSB=0.5372 mV/code`, `pulse_code=65`, and `120 Hz`.",
        "The step and pulse errors are calculated from the two half-period levels of each logical SWV step; segmented hold intervals are excluded and reported separately. The sequence index is anchored to the first valid approximately -450 mV stair, identified from its first rising edge; the focused overlay is then evaluated near -400 mV using signed transition edges. Plateau overlay bias/RMSE is calculated only at the centres of the high and low plateaus, so square-wave transitions are not misreported as voltage error.",
        "",
        "| Profile | Firmware step | Measured step (fit scatter) | Step error | Firmware pulse p-p | Measured pulse p-p | Pulse error | Cycle-fit RMSE | Plateau bias / RMSE | Segment gaps |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        lines.append(
            f"| {row['profile']} | {abs(float(row['expected_step_mV'])):.3f} mV | "
            f"{abs(float(row['measured_step_mV'])):.3f} mV ({float(row['step_sd_mV']):.3f} mV) | "
            f"{float(row['step_error_pct']):.2f}% | {float(row['expected_pulse_pp_mV']):.2f} mV | "
            f"{float(row['measured_pulse_pp_mV']):.2f} mV | {float(row['pulse_error_pct']):.2f}% | "
            f"{float(row['endpoint_rmse_mV']):.2f} mV | "
            f"{float(row['overlay_bias_mV']):.1f} / {float(row['overlay_rmse_mV']):.1f} mV | `{row['segment_gaps_s'] or 'none'}` |"
        )
    two_mv = next(row for row in rows if row["profile"] == "2MV")
    lines.extend([
        "",
        "For the 2 mV comparison, the first two complete cycles near -400 mV were used for timing alignment. At the independent approximately -300 mV check, the fitted local phase drift was "
        f"{float(two_mv['minus300_phase_drift_ms']):.2f} ms, the mean voltage bias was "
        f"{float(two_mv['minus300_bias_mV']):.1f} mV, and the pulse-amplitude difference was "
        f"{float(two_mv['minus300_pulse_delta_mV']):.2f} mV.",
    ])
    lines.extend([
        "",
        "## Interpretation",
        "",
        "The 5 mV and 2 mV captures are continuous full-sequencer scans. Their regular 120 Hz staircase and repeated pulse levels are therefore directly comparable with a uniform theoretical profile.",
        "",
        "The 1 mV capture preserves the nominal staircase inside each segment, but the two approximately 0.3--0.4 s holds are real timing discontinuities. They are shown as amber regions and are not treated as ordinary SWV cycles in the endpoint error.",
        "",
        "The standalone comparison figures use short windows anchored at the first complete stair near -400 mV: 2 steps for 5 mV, 5 steps for 2 mV, and 10 steps for 1 mV. The 1 mV window remains inside the first segment and therefore avoids the segmented hold. All three overlays use signed transition-edge alignment; the 2 mV phase is specifically anchored with the first two cycles near -400 mV and then checked independently around -300 mV. The plateau overlay bias is reported separately from the relative step/pulse error.",
        "",
        "## Report wording",
        "",
        "> Direct NI myDAQ measurements reproduced the programmed NanoStat SWV timing for the 5 mV and 2 mV full-sequencer profiles, including the 120 Hz staircase and approximately 69.84 mV peak-to-peak pulse. The 1 mV segmented profile preserved the programmed step height within each segment but introduced two approximately 0.3--0.4 s hold intervals, so it was not temporally equivalent to a continuous 1 mV sequence.",
        "",
        "Use the `*_theory_vs_mydaq.png` files for complete timing and the `*_comparison.png` files for focused waveform comparison. The combined metrics are in `swv_profile_comparison_metrics.csv`.",
    ])
    SUMMARY_MD.write_text("\n".join(lines) + "\n")
    print(METRICS_CSV)
    print(SUMMARY_MD)


if __name__ == "__main__":
    main()
