import argparse
import os
import re

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


DEFAULT_INPUT = "step_response_current_position_plugged_no_cage.txt"
DEFAULT_OUTPUT = "step_response_current_position_plugged_no_cage.png"


MODE_PATTERNS = {
    "LPTIA": (
        "LPTIA endpoint step response",
        "Mode J: LPTIA",
        "capture=LPTIA Step Response",
        "LPTIA Step Response",
    ),
    "HSTIA": (
        "HSTIA endpoint step response",
        "Mode K: HSTIA",
        "capture=HSTIA Step Response",
        "HSTIA Step Response",
    ),
}


CAPTURE_RE = re.compile(
    r"Captured\s+(?P<count>\d+)\s+samples,\s+estimated sample interval="
    r"(?P<sample_us>\d+)\s+us,\s+step_index=(?P<step_index>\d+)"
)
CSV_RE = re.compile(r"\[STEP-CSV\]\s*(?P<idx>\d+),(?P<t_us>-?\d+),(?P<i_na>-?\d+(?:\.\d+)?)")


def detect_mode(line, current_mode):
    for mode, patterns in MODE_PATTERNS.items():
        if any(pattern in line for pattern in patterns):
            return mode
    return current_mode


def new_run(mode):
    return {
        "mode": mode,
        "sample_us": None,
        "step_index": None,
        "rows": [],
    }


def parse_rtt_log(file_path):
    runs = []
    current_mode = None
    current_run = None

    with open(file_path, "r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            previous_mode = current_mode
            current_mode = detect_mode(line, current_mode)

            if current_mode != previous_mode and current_mode is not None:
                if current_run and current_run["rows"]:
                    runs.append(current_run)
                current_run = new_run(current_mode)

            capture = CAPTURE_RE.search(line)
            if capture:
                if current_run is None:
                    current_run = new_run(current_mode or "UNKNOWN")
                current_run["sample_us"] = int(capture.group("sample_us"))
                current_run["step_index"] = int(capture.group("step_index"))

            csv = CSV_RE.search(line)
            if csv:
                if current_run is None:
                    current_run = new_run(current_mode or "UNKNOWN")
                current_run["rows"].append(
                    (
                        int(csv.group("idx")),
                        int(csv.group("t_us")),
                        float(csv.group("i_na")),
                    )
                )

    if current_run and current_run["rows"]:
        runs.append(current_run)

    frames = {}
    for run in runs:
        mode = run["mode"]
        df = pd.DataFrame(run["rows"], columns=["index", "t_us_raw", "I_nA"])
        if df.empty:
            continue

        sample_us = run["sample_us"]
        step_index = run["step_index"]

        if sample_us is None:
            positive_dt = df.loc[df["t_us_raw"] > 0, "t_us_raw"].diff().dropna()
            sample_us = int(round(positive_dt.median())) if not positive_dt.empty else 138

        if step_index is None:
            first_positive = df.index[df["t_us_raw"] > 0]
            step_index = int(df.loc[first_positive[0] - 1, "index"]) if len(first_positive) else 0

        # Firmware prints t_us=0 for pre-step samples. Rebuild a monotonic
        # relative time axis from sample index, step_index, and sample_us.
        df["t_ms"] = ((df["index"] - step_index) * sample_us) / 1000.0
        df["mode"] = mode
        df["sample_us"] = sample_us
        df["step_index"] = step_index

        frames.setdefault(mode, []).append(df)

    return frames


def average_window(df, start_ms, end_ms):
    window = df[(df["t_ms"] >= start_ms) & (df["t_ms"] <= end_ms)]
    if window.empty:
        return np.nan
    return float(window["I_nA"].mean())


def crossing_time_ms(df, baseline, steady, fraction):
    delta = steady - baseline
    if abs(delta) < 1e-12:
        return np.nan

    target = baseline + delta * fraction
    after_step = df[df["t_ms"] >= 0].copy()
    if after_step.empty:
        return np.nan

    if delta < 0:
        hit = after_step[after_step["I_nA"] <= target]
    else:
        hit = after_step[after_step["I_nA"] >= target]

    if hit.empty:
        return np.nan
    return float(hit.iloc[0]["t_ms"])


def estimate_50hz_amplitude(df, start_ms=5.0, end_ms=50.0):
    segment = df[(df["t_ms"] >= start_ms) & (df["t_ms"] <= end_ms)].copy()
    if len(segment) < 10:
        return np.nan

    t = segment["t_ms"].to_numpy() / 1000.0
    y = segment["I_nA"].to_numpy()
    design = np.column_stack(
        [
            np.sin(2.0 * np.pi * 50.0 * t),
            np.cos(2.0 * np.pi * 50.0 * t),
            np.ones_like(t),
        ]
    )
    coeffs, *_ = np.linalg.lstsq(design, y, rcond=None)
    return float(np.sqrt(coeffs[0] ** 2 + coeffs[1] ** 2))


def summarize(df):
    baseline = average_window(df, -5.0, -1.0)
    if np.isnan(baseline):
        baseline = average_window(df, -15.0, -1.0)

    steady = average_window(df, 40.0, 50.0)
    if np.isnan(steady):
        steady = average_window(df, 20.0, 50.0)

    endpoint_start = 1000.0 / 150.0 / 2.0 * 0.75
    endpoint_end = 1000.0 / 150.0 / 2.0 * 0.95
    endpoint = average_window(df, endpoint_start, endpoint_end)
    endpoint_error = endpoint - steady if not np.isnan(endpoint) and not np.isnan(steady) else np.nan
    endpoint_error_pct = endpoint_error / steady * 100.0 if steady and not np.isnan(endpoint_error) else np.nan

    return {
        "baseline": baseline,
        "steady": steady,
        "delta": steady - baseline,
        "t63_ms": crossing_time_ms(df, baseline, steady, 0.632),
        "t90_ms": crossing_time_ms(df, baseline, steady, 0.90),
        "t95_ms": crossing_time_ms(df, baseline, steady, 0.95),
        "t99_ms": crossing_time_ms(df, baseline, steady, 0.99),
        "endpoint": endpoint,
        "endpoint_error": endpoint_error,
        "endpoint_error_pct": endpoint_error_pct,
        "mains_amp": estimate_50hz_amplitude(df),
    }


def plot_step_response(frames, save_path, title):
    modes = [mode for mode in ("LPTIA", "HSTIA") if mode in frames]
    if not modes:
        raise ValueError("No LPTIA/HSTIA STEP-CSV data found in the log file.")

    fig, axes = plt.subplots(len(modes), 1, figsize=(13, 5 * len(modes)), sharex=True)
    if len(modes) == 1:
        axes = [axes]

    endpoint_start = 1000.0 / 150.0 / 2.0 * 0.75
    endpoint_end = 1000.0 / 150.0 / 2.0 * 0.95
    colors = {"LPTIA": "#1f77b4", "HSTIA": "#ff7f0e"}
    labels = {"LPTIA": "LPTIA (2M + 220pF)", "HSTIA": "HSTIA (2M + 4pF)"}

    summaries = {}
    for ax, mode in zip(axes, modes):
        # If the file contains repeated runs, plot the last run by default.
        df = frames[mode][-1]
        stats = summarize(df)
        summaries[mode] = stats

        ax.plot(df["t_ms"], df["I_nA"], label=labels.get(mode, mode), color=colors.get(mode), linewidth=1.5)
        ax.axvline(0, color="red", linestyle="--", linewidth=1.4, alpha=0.85, label="Step trigger")
        ax.axvspan(endpoint_start, endpoint_end, color="green", alpha=0.18, label="150Hz endpoint window (75%-95%)")

        if not np.isnan(stats["steady"]):
            ax.axhline(
                stats["steady"],
                color="purple",
                linestyle=":",
                linewidth=2,
                label=f"steady ~{stats['steady']:.2f} nA",
            )

        detail = (
            f"t63={stats['t63_ms']:.3g} ms, "
            f"t95={stats['t95_ms']:.3g} ms, "
            f"endpoint err={stats['endpoint_error_pct']:.2f}%, "
            f"50Hz amp~{stats['mains_amp']:.2f} nA"
        )
        ax.set_title(f"{mode} Step Response ({detail})", fontsize=13, fontweight="bold")
        ax.set_ylabel("Current (nA)")
        ax.grid(True, linestyle="--", alpha=0.55)
        ax.legend(loc="best")

    axes[-1].set_xlabel("Time from DAC step (ms)")
    if title:
        fig.suptitle(title, fontsize=15, fontweight="bold")
        fig.tight_layout(rect=(0, 0, 1, 0.96))
    else:
        fig.tight_layout()

    plt.savefig(save_path, dpi=300, bbox_inches="tight")
    print(f"Saved plot: {save_path}")
    return summaries


def print_summary(frames, summaries):
    for mode in ("LPTIA", "HSTIA"):
        if mode not in frames:
            print(f"{mode}: no data")
            continue
        df = frames[mode][-1]
        stats = summaries[mode]
        print(f"\n{mode}:")
        print(f"  samples: {len(df)}")
        print(f"  sample_us: {int(df['sample_us'].iloc[0])}")
        print(f"  step_index: {int(df['step_index'].iloc[0])}")
        print(f"  baseline: {stats['baseline']:.3f} nA")
        print(f"  steady: {stats['steady']:.3f} nA")
        print(f"  delta: {stats['delta']:.3f} nA")
        print(f"  t63/t90/t95/t99: {stats['t63_ms']:.3f} / {stats['t90_ms']:.3f} / {stats['t95_ms']:.3f} / {stats['t99_ms']:.3f} ms")
        print(f"  endpoint avg: {stats['endpoint']:.3f} nA")
        print(f"  endpoint error: {stats['endpoint_error']:.3f} nA ({stats['endpoint_error_pct']:.2f}%)")
        print(f"  estimated 50Hz amplitude: {stats['mains_amp']:.3f} nA")


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    parser = argparse.ArgumentParser(description="Plot AD5941 LPTIA/HSTIA endpoint step-response RTT logs.")
    parser.add_argument(
        "-i",
        "--input",
        default=os.path.join(script_dir, DEFAULT_INPUT),
        help="RTT text log containing j/k STEP-CSV output.",
    )
    parser.add_argument(
        "-o",
        "--output",
        default=os.path.join(script_dir, DEFAULT_OUTPUT),
        help="Output PNG path.",
    )
    parser.add_argument(
        "--title",
        default="AD5941 Step Response: current position, laptop plugged, no Faraday cage",
        help="Plot title.",
    )
    args = parser.parse_args()

    frames = parse_rtt_log(args.input)
    summaries = plot_step_response(frames, args.output, args.title)
    print_summary(frames, summaries)


if __name__ == "__main__":
    main()
