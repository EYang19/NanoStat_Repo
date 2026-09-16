#!/usr/bin/env python3
"""Plot literature-derived salivary cortisol changes on three time scales."""

from pathlib import Path
import csv

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
FIGURE_DIR = ROOT / "figures"

COLORS = {
    "navy": "#24507A",
    "teal": "#2A8C82",
    "red": "#C94F5C",
    "grey": "#687684",
}


def style_axes(ax):
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.grid(axis="y", color="#DDE3E8", linewidth=0.7)
    ax.set_axisbelow(True)
    ax.tick_params(labelsize=8)


def read_rows(filename):
    with (DATA_DIR / filename).open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def plot_diurnal(ax):
    data = read_rows("circort_age_21_30_reference.csv")
    for sex, color in [("Female", COLORS["red"]), ("Male", COLORS["navy"])]:
        part = [row for row in data if row["sex"] == sex]
        x = [float(row["time_after_waking_h"]) for row in part]
        median = [float(row["median_nmol_L"]) for row in part]
        lower = [float(row["p05_nmol_L"]) for row in part]
        upper = [float(row["p95_nmol_L"]) for row in part]
        ax.fill_between(x, lower, upper, color=color, alpha=0.12, linewidth=0)
        ax.plot(x, median, color=color, linewidth=2.0, marker="o", markersize=3.4,
                label=f"{sex}: median (5th-95th)")

    ax.set_title("a  Diurnal reference pattern", loc="left", fontsize=8.8, weight="bold")
    ax.set_xlabel("Time after waking (h)", fontsize=8.5)
    ax.set_ylabel("Salivary cortisol (nmol/L)", fontsize=8.5)
    ax.set_xlim(0.5, 16.5)
    ax.set_ylim(0, 52)
    ax.legend(frameon=False, fontsize=7, loc="upper right")
    style_axes(ax)


def plot_awakening(ax):
    data = read_rows("exam_awakening_response.csv")
    for condition, color in [("Neutral day", COLORS["teal"]), ("Exam day", COLORS["red"])]:
        part = [row for row in data if row["condition"] == condition]
        ax.errorbar(
            [float(row["time_after_waking_min"]) for row in part],
            [float(row["mean_reported"]) for row in part],
            yerr=[float(row["sd_reported"]) for row in part],
            color=color,
            marker="o",
            markersize=4,
            linewidth=2,
            capsize=3,
            label=condition,
        )

    ax.set_title("b  Awakening response", loc="left", fontsize=8.8, weight="bold")
    ax.set_xlabel("Time after waking (min)", fontsize=8.5)
    ax.set_xlim(-3, 33)
    ax.set_xticks([0, 30])
    ax.set_ylim(0, 52)
    ax.legend(frameon=False, fontsize=7, loc="upper left")
    style_axes(ax)


def plot_acute_stress(ax):
    data = read_rows("acute_stress_response.csv")
    x = [0, 1, 2]
    for condition, color in [("Control", COLORS["grey"]), ("TSST stress", COLORS["red"])]:
        part = sorted(
            (row for row in data if row["condition"] == condition),
            key=lambda row: int(row["sample_order"]),
        )
        ax.errorbar(
            x,
            [float(row["mean_nmol_L"]) for row in part],
            yerr=[float(row["sd_nmol_L"]) for row in part],
            color=color,
            marker="o",
            markersize=4,
            linewidth=2,
            capsize=3,
            label=condition,
        )

    ax.set_title("c  Acute stress response", loc="left", fontsize=8.8, weight="bold")
    ax.set_xticks(x, ["Sample 1\nBaseline", "Sample 2\nPost-stress", "Sample 3\nEnd"])
    ax.set_ylim(0, 52)
    ax.legend(frameon=False, fontsize=7, loc="upper left")
    style_axes(ax)


def main():
    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "axes.labelcolor": "#202830",
        "xtick.color": "#3D4852",
        "ytick.color": "#3D4852",
        "text.color": "#182028",
    })

    fig, axes = plt.subplots(
        1,
        3,
        figsize=(7.5, 3.25),
        sharey=True,
        gridspec_kw={"width_ratios": [1.18, 0.90, 1.05], "wspace": 0.28},
    )
    plot_diurnal(axes[0])
    plot_awakening(axes[1])
    plot_acute_stress(axes[2])
    axes[1].tick_params(labelleft=False)
    axes[2].tick_params(labelleft=False)

    fig.subplots_adjust(left=0.075, right=0.99, top=0.91, bottom=0.21)
    FIGURE_DIR.mkdir(parents=True, exist_ok=True)
    fig.savefig(
        FIGURE_DIR / "cortisol_over_time.png",
        dpi=600,
        bbox_inches="tight",
        facecolor="white",
    )
    fig.savefig(FIGURE_DIR / "cortisol_over_time.pdf", bbox_inches="tight", facecolor="white")
    plt.close(fig)


if __name__ == "__main__":
    main()
