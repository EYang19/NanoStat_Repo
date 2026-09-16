#!/usr/bin/env python3
"""Plot single-frequency SWV blank/peak RTT CSV output.

Usage:
    python plot_blank_peak.py
    python plot_blank_peak.py --input blank_peak_rtt.txt --output my_plot.png
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

import matplotlib.pyplot as plt


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_INPUT = SCRIPT_DIR / "blank_peak_rtt.txt"
DEFAULT_OUTPUT = SCRIPT_DIR / "blank_peak_plot.png"


CSV_PREFIX = "[BLANK-PEAK-CSV]"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", "-i", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", "-o", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def extract_frequency_hz(text: str) -> int | None:
    match = re.search(r"\[BLANK-PEAK\].*?f=(\d+)Hz", text)
    if match:
        return int(match.group(1))
    return None


def read_blank_peak_csv(path: Path) -> tuple[list[float], list[float], list[float], list[float], list[float], int | None]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    frequency_hz = extract_frequency_hz(text)
    rows: list[list[str]] = []

    for line in text.splitlines():
        if CSV_PREFIX not in line:
            continue

        payload = line.split(CSV_PREFIX, 1)[1].strip()
        if not payload or payload.startswith("V_mV"):
            continue

        rows.extend(csv.reader([payload]))

    if not rows:
        raise ValueError(f"No {CSV_PREFIX} data rows found in {path}")

    v_mv: list[float] = []
    delta_na: list[float] = []
    delta_avg3_na: list[float] = []
    forward_na: list[float] = []
    reverse_na: list[float] = []

    for row in rows:
        if len(row) < 5:
            continue
        v, d, d3, fwd, rev = [float(x.strip()) for x in row[:5]]
        v_mv.append(v)
        delta_na.append(d)
        delta_avg3_na.append(d3)
        forward_na.append(fwd)
        reverse_na.append(rev)

    if not v_mv:
        raise ValueError(f"{CSV_PREFIX} rows were present, but none parsed cleanly")

    return v_mv, delta_na, delta_avg3_na, forward_na, reverse_na, frequency_hz


def peak_by_abs(v_mv: list[float], y_na: list[float]) -> tuple[int, float, float]:
    idx = max(range(len(y_na)), key=lambda i: abs(y_na[i]))
    return idx, v_mv[idx], y_na[idx]


def main() -> None:
    args = parse_args()
    v_mv, delta_na, delta_avg3_na, forward_na, reverse_na, frequency_hz = read_blank_peak_csv(args.input)

    raw_idx, raw_v, raw_i = peak_by_abs(v_mv, delta_na)
    avg_idx, avg_v, avg_i = peak_by_abs(v_mv, delta_avg3_na)

    title_freq = f" ({frequency_hz} Hz)" if frequency_hz is not None else ""
    fig, ax = plt.subplots(figsize=(10, 6))

    ax.plot(v_mv, delta_na, marker="o", markersize=3, linewidth=1.1, alpha=0.55, label="Delta_I")
    ax.plot(v_mv, delta_avg3_na, marker="o", markersize=3, linewidth=1.8, label="Delta_I avg3")
    ax.axhline(0, color="black", linewidth=0.8, alpha=0.45)
    ax.scatter([avg_v], [avg_i], s=80, zorder=5, label=f"Peak avg3: {avg_i:.3f} nA @ {avg_v:.3f} mV")
    ax.annotate(
        f"{avg_i:.3f} nA\n{avg_v:.1f} mV",
        xy=(avg_v, avg_i),
        xytext=(10, 12),
        textcoords="offset points",
        arrowprops={"arrowstyle": "->", "lw": 1},
    )

    ax.set_title(f"AD5941 Single-Frequency SWV Blank/Peak Scan{title_freq}")
    ax.set_xlabel("Potential Vbase (mV)")
    ax.set_ylabel("Differential current Delta_I (nA)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=180)

    print(f"Parsed points: {len(v_mv)}")
    if frequency_hz is not None:
        print(f"Frequency: {frequency_hz} Hz")
    print(f"Peak raw abs:  step={raw_idx + 1}, V={raw_v:.3f} mV, Delta_I={raw_i:.3f} nA")
    print(f"Peak avg3 abs: step={avg_idx + 1}, V={avg_v:.3f} mV, Delta_I_avg3={avg_i:.3f} nA")
    print(f"Delta_I avg3 min/max: {min(delta_avg3_na):.3f} / {max(delta_avg3_na):.3f} nA")
    print(f"Saved plot: {args.output}")


if __name__ == "__main__":
    main()

