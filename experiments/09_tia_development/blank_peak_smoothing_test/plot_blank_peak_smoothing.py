#!/usr/bin/env python3
"""Plot blank/peak SWV output with potential-axis smoothing comparisons.

Delta_I is expected to use the DropSens-style convention I_forward - I_reverse
for upward MB reduction peaks.

Usage:
    python plot_blank_peak_smoothing.py
    python plot_blank_peak_smoothing.py --input blank_peak_smoothing_rtt.txt --output smoothing_plot.png

The parser accepts both the original test 'l' CSV:
    E_WE_RE_mV,Delta_I_nA,Delta_I_avg3_nA,I_forward_nA,I_reverse_nA

and the extended test 'n' CSV:
    E_WE_RE_mV,Delta_I_nA,Delta_I_avg3_nA,Delta_I_smooth5_nA,Delta_I_smooth7_nA,Delta_I_sg5_nA,I_forward_nA,I_reverse_nA

If smooth5/smooth7/SG5 are not present in the RTT file, this script computes
them from Delta_I.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

import matplotlib.pyplot as plt


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_INPUT = SCRIPT_DIR / "blank_peak_smoothing_rtt.txt"
DEFAULT_OUTPUT = SCRIPT_DIR / "blank_peak_smoothing_plot.png"
CSV_PREFIX = "[BLANK-PEAK-CSV]"
PEAK_SEARCH_MIN_MV = -400.0
PEAK_SEARCH_MAX_MV = -80.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", "-i", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", "-o", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def extract_frequency_hz(text: str) -> int | None:
    match = re.search(r"\[BLANK-PEAK\].*?f=(\d+)Hz", text)
    return int(match.group(1)) if match else None


def centered_mean(values: list[float], index: int, radius: int) -> float:
    start = max(0, index - radius)
    end = min(len(values) - 1, index + radius)
    window = values[start : end + 1]
    return sum(window) / len(window)


def savgol5(values: list[float], index: int) -> float:
    if index < 2 or index + 2 >= len(values):
        return centered_mean(values, index, 2)
    y0, y1, y2, y3, y4 = values[index - 2 : index + 3]
    return ((-3 * y0) + (12 * y1) + (17 * y2) + (12 * y3) - (3 * y4)) / 35.0


def peak_by_abs(v_mv: list[float], y_na: list[float]) -> tuple[int, float, float]:
    candidates = [
        i for i, v in enumerate(v_mv)
        if PEAK_SEARCH_MIN_MV <= v <= PEAK_SEARCH_MAX_MV
    ]
    if not candidates:
        candidates = list(range(len(y_na)))
    idx = max(candidates, key=lambda i: abs(y_na[i]))
    return idx, v_mv[idx], y_na[idx]


def read_rows(path: Path) -> tuple[dict[str, list[float]], int | None]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    frequency_hz = extract_frequency_hz(text)
    raw_rows: list[list[str]] = []

    for line in text.splitlines():
        if CSV_PREFIX not in line:
            continue
        payload = line.split(CSV_PREFIX, 1)[1].strip()
        if not payload or payload.startswith("V_mV") or payload.startswith("E_WE_RE_mV"):
            continue
        raw_rows.extend(csv.reader([payload]))

    if not raw_rows:
        raise ValueError(f"No {CSV_PREFIX} data rows found in {path}")

    data: dict[str, list[float]] = {
        "v": [],
        "delta": [],
        "avg3": [],
        "smooth5": [],
        "smooth7": [],
        "sg5": [],
        "ifwd": [],
        "irev": [],
    }

    has_extended = False
    for row in raw_rows:
        if len(row) >= 8:
            v, delta, avg3, smooth5, smooth7, sg5, ifwd, irev = [float(x.strip()) for x in row[:8]]
            has_extended = True
        elif len(row) >= 5:
            v, delta, avg3, ifwd, irev = [float(x.strip()) for x in row[:5]]
            smooth5 = smooth7 = sg5 = float("nan")
        else:
            continue

        data["v"].append(v)
        data["delta"].append(delta)
        data["avg3"].append(avg3)
        data["smooth5"].append(smooth5)
        data["smooth7"].append(smooth7)
        data["sg5"].append(sg5)
        data["ifwd"].append(ifwd)
        data["irev"].append(irev)

    if not data["v"]:
        raise ValueError(f"{CSV_PREFIX} rows were present, but none parsed cleanly")

    if not has_extended:
        data["smooth5"] = [centered_mean(data["delta"], i, 2) for i in range(len(data["delta"]))]
        data["smooth7"] = [centered_mean(data["delta"], i, 3) for i in range(len(data["delta"]))]
        data["sg5"] = [savgol5(data["delta"], i) for i in range(len(data["delta"]))]

    return data, frequency_hz


def main() -> None:
    args = parse_args()
    data, frequency_hz = read_rows(args.input)
    v_mv = data["v"]
    title_freq = f" ({frequency_hz} Hz)" if frequency_hz is not None else ""

    peak_idx, peak_v, peak_i = peak_by_abs(v_mv, data["sg5"])

    fig, ax = plt.subplots(figsize=(11, 6.5))
    ax.plot(v_mv, data["delta"], marker="o", markersize=2.5, linewidth=0.9, alpha=0.35, label="Delta_I raw")
    ax.plot(v_mv, data["avg3"], marker="o", markersize=2.5, linewidth=1.3, alpha=0.75, label="Delta_I avg3")
    ax.plot(v_mv, data["smooth5"], linewidth=2.0, label="Delta_I smooth5")
    ax.plot(v_mv, data["smooth7"], linewidth=2.0, label="Delta_I smooth7")
    ax.plot(v_mv, data["sg5"], linewidth=2.2, label="Delta_I SG5")
    ax.axhline(0, color="black", linewidth=0.8, alpha=0.45)
    ax.scatter([peak_v], [peak_i], s=85, zorder=5, label=f"Peak SG5: {peak_i:.3f} nA @ {peak_v:.3f} mV")
    ax.annotate(
        f"{peak_i:.3f} nA\n{peak_v:.1f} mV",
        xy=(peak_v, peak_i),
        xytext=(10, 12),
        textcoords="offset points",
        arrowprops={"arrowstyle": "->", "lw": 1},
    )

    ax.set_title(f"AD5941 SWV Blank/Peak Smoothing Comparison{title_freq}")
    ax.set_xlabel("Potential E_WE-RE (mV)")
    ax.set_ylabel("Differential current Delta_I (nA)")
    ax.axvspan(PEAK_SEARCH_MIN_MV, PEAK_SEARCH_MAX_MV, color="tab:green", alpha=0.08,
               label="Peak search window")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=180)

    print(f"Parsed points: {len(v_mv)}")
    if frequency_hz is not None:
        print(f"Frequency: {frequency_hz} Hz")
    print(f"Peak SG5 abs in {PEAK_SEARCH_MIN_MV:.0f}..{PEAK_SEARCH_MAX_MV:.0f} mV: "
          f"step={peak_idx + 1}, V={peak_v:.3f} mV, Delta_I_sg5={peak_i:.3f} nA")
    for key in ("delta", "avg3", "smooth5", "smooth7", "sg5"):
        values = data[key]
        print(f"{key}: min={min(values):.3f} nA, max={max(values):.3f} nA, span={max(values)-min(values):.3f} nA")
    print(f"Saved plot: {args.output}")


if __name__ == "__main__":
    main()
