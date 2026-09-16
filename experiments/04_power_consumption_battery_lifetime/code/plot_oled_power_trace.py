"""Plot the latest OLED-connected battery-current trace in the standard report style."""

from __future__ import annotations

import csv
import re
from pathlib import Path

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
FIGURES = ROOT / "figures"
INPUT = DATA / "oled_battery_trace_rtt.md"
SUMMARY = DATA / "oled_power_trace_phase_summary.csv"
BATCUR_RE = re.compile(r"BATCUR,t_ms=(\d+),IBAT_UA=(-?\d+|NA)")


def main() -> None:
    raw = []
    for line in INPUT.read_text(errors="replace").splitlines():
        match = BATCUR_RE.search(line)
        if match and match.group(2) != "NA":
            raw.append((int(match.group(1)), int(match.group(2)) / 1000.0))
    if not raw:
        raise SystemExit("No BATCUR records found")

    first_t = raw[0][0]
    x = [(time_ms - first_t) / 1000.0 for time_ms, _ in raw]
    y = [current_mA for _, current_mA in raw]

    intervals = []
    with SUMMARY.open(newline="") as handle:
        for row in csv.DictReader(handle):
            intervals.append(
                {
                    "label": row["label"],
                    "start_s": (float(row["start_ms"]) - first_t) / 1000.0,
                    "end_s": (float(row["end_ms"]) - first_t) / 1000.0,
                    "mean_mA": float(row["mean_mA"]),
                    "std_mA": float(row["std_mA"]),
                }
            )

    FIGURES.mkdir(exist_ok=True)
    fig, ax = plt.subplots(figsize=(15, 7.2), dpi=180)
    ax.set_axisbelow(True)
    ax.plot(
        x,
        y,
        color="#7f8c99",
        linewidth=0.8,
        marker=".",
        markersize=2.0,
        alpha=0.42,
    )
    ax.set_xlabel("Time from current trace start (s)")
    ax.set_ylabel("Battery current IBAT (mA)")
    ax.set_title("NanoStat battery current: workflow phases and interval means")
    ax.grid(True, alpha=0.25)

    colours = [
        "#90caf9", "#a5d6a7", "#ffe082", "#ce93d8", "#b0bec5",
        "#b39ddb", "#cfd8dc", "#ba68c8", "#cfd8dc", "#9575cd",
        "#cfd8dc", "#7e57c2", "#80cbc4", "#ef9a9a",
    ]
    for index, interval in enumerate(intervals):
        start_s = interval["start_s"]
        end_s = interval["end_s"]
        mean = interval["mean_mA"]
        std = interval["std_mA"]
        colour = colours[index % len(colours)]
        ax.axvspan(start_s, end_s, color=colour, alpha=0.13, linewidth=0)
        ax.axvline(start_s, color="#455a64", linestyle="--", linewidth=2.0, alpha=0.9)
        ax.plot(
            [start_s, end_s],
            [mean, mean],
            color=colour,
            linewidth=4.0,
            solid_capstyle="butt",
            zorder=4,
        )
        ax.fill_between(
            [start_s, end_s],
            [mean - std, mean - std],
            [mean + std, mean + std],
            color=colour,
            alpha=0.22,
            linewidth=0,
            zorder=2,
        )
        ax.text(
            (start_s + end_s) / 2.0,
            0.98,
            interval["label"],
            transform=ax.get_xaxis_transform(),
            ha="center",
            va="top",
            fontsize=7.5,
            rotation=90,
            color="#263238",
            bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.62, "pad": 1.2},
            zorder=5,
        )

    handles = [
        plt.Line2D([], [], color="#7f8c99", alpha=0.55, linewidth=1.2, label="Raw IBAT samples"),
        plt.Line2D([], [], color="#7e57c2", linewidth=4, label="Interval mean"),
        plt.Rectangle((0, 0), 1, 1, facecolor="#b39ddb", alpha=0.22, label="Mean ± 1 SD"),
    ]
    ax.legend(handles=handles, loc="lower right", fontsize=8, ncol=3)
    fig.subplots_adjust(top=0.78, right=0.98, left=0.08, bottom=0.12)
    fig.savefig(FIGURES / "power_current_trace.png", bbox_inches="tight")
    fig.savefig(FIGURES / "power_current_trace_oled.png", bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    main()
