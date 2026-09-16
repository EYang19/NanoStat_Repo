#!/usr/bin/env python3
"""Generate a compact overview of electrochemical measurement modes."""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


OUT_DIR = Path(__file__).resolve().parent / "figures"


def style_axis(ax, title, output, status, implemented=False):
    accent = "#087f8c" if implemented else "#586577"
    ax.set_title(title, loc="left", fontsize=11, fontweight="bold", color="#18212b")
    ax.text(
        0.02,
        0.045,
        output,
        transform=ax.transAxes,
        fontsize=8.3,
        color="#364152",
        bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.88, "pad": 1.2},
    )
    ax.text(
        0.98,
        0.91,
        status,
        transform=ax.transAxes,
        ha="right",
        va="top",
        fontsize=7.2,
        color=accent,
        fontweight="bold",
        bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.88, "pad": 1.2},
    )
    ax.set_xlabel("Time", fontsize=8.5)
    ax.set_ylabel("Potential", fontsize=8.5)
    ax.set_xticks([])
    ax.set_yticks([])
    ax.grid(True, color="#e3e8ee", linewidth=0.7)
    for spine in ax.spines.values():
        spine.set_color(accent if implemented else "#aeb7c2")
        spine.set_linewidth(1.5 if implemented else 0.9)


def plot_swv(ax):
    t = np.linspace(0, 1, 1200)
    step = np.floor(t * 6) / 6
    pulse = np.where((t * 6) % 1 < 0.5, 0.18, -0.18)
    ax.step(t, 0.15 + 0.62 * step + pulse, where="post", color="#087f8c", lw=1.8)
    ax.set_ylim(-0.10, 1.18)
    style_axis(ax, "(a) SWV", r"Output: $\Delta i$ vs potential", "VALIDATED", True)


def plot_cv(ax):
    t = np.linspace(0, 1, 500)
    y = np.where(t <= 0.5, -0.7 + 2.8 * t, 2.1 - 2.8 * t)
    ax.plot(t, y, color="#c65d21", lw=1.9)
    ax.set_ylim(-0.90, 0.92)
    style_axis(ax, "(b) CV", "Output: current vs potential and scan direction", "PLATFORM EXTENSION")


def plot_dpv(ax):
    t = np.linspace(0, 1, 1200)
    step = np.floor(t * 7) / 7
    pulse = np.where((t * 7) % 1 > 0.68, 0.22, 0.0)
    ax.step(t, 0.05 + 0.8 * step + pulse, where="post", color="#7a5195", lw=1.8)
    ax.set_ylim(-0.08, 1.10)
    style_axis(ax, "(c) DPV", r"Output: $\Delta i$ vs potential", "PLATFORM EXTENSION")


def plot_ca(ax):
    t = np.linspace(0, 1, 500)
    y = np.where(t < 0.28, 0.15, 0.82)
    ax.step(t, y, where="post", color="#2f6f3e", lw=1.9)
    ax.set_ylim(0.0, 0.95)
    style_axis(ax, "(d) Chronoamperometry", "Output: current vs time", "PLATFORM EXTENSION")


def plot_eis(ax):
    t = np.linspace(0, 1, 700)
    y = 0.48 + 0.22 * np.sin(2 * np.pi * 5 * t)
    ax.plot(t, y, color="#2563a6", lw=1.8)
    ax.axhline(0.48, color="#9aa5b1", lw=0.8, ls="--")
    ax.set_ylim(0.12, 0.88)
    style_axis(ax, "(e) EIS", r"Output: $|Z|$ and phase vs frequency", "PLATFORM EXTENSION")


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(2, 3, figsize=(11.2, 5.9))
    plot_swv(axes[0, 0])
    plot_cv(axes[0, 1])
    plot_dpv(axes[0, 2])
    plot_ca(axes[1, 0])
    plot_eis(axes[1, 1])

    ax = axes[1, 2]
    ax.axis("off")
    ax.text(0.02, 0.84, "Platform status", fontsize=11, fontweight="bold", color="#18212b")
    ax.text(0.02, 0.62, "SWV", fontsize=10, fontweight="bold", color="#087f8c")
    ax.text(0.25, 0.62, "Implemented and electrically validated", fontsize=9, color="#364152")
    ax.text(0.02, 0.39, "CV / DPV / CA / EIS", fontsize=10, fontweight="bold", color="#586577")
    ax.text(0.02, 0.25, "Supported by the AFE architecture; each requires", fontsize=9, color="#364152")
    ax.text(0.02, 0.14, "dedicated firmware, processing and validation.", fontsize=9, color="#364152")

    fig.suptitle("Electrochemical excitation and measurement modes", fontsize=15, fontweight="bold", y=0.99)
    fig.subplots_adjust(left=0.065, right=0.985, top=0.90, bottom=0.08, wspace=0.25, hspace=0.38)
    fig.savefig(OUT_DIR / "electrochemical_methods_overview.pdf", bbox_inches="tight")
    fig.savefig(OUT_DIR / "electrochemical_methods_overview.png", dpi=300, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    main()
