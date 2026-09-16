"""Compare OLED and no-OLED traces after matching each phase duration."""

from __future__ import annotations

import csv
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
OLD_POINTS = DATA / "power_trace_points.csv"
NEW_POINTS = DATA / "oled_battery_trace_rtt.md"
OLD_PHASES = DATA / "power_trace_interval_summary.csv"
NEW_PHASES = DATA / "oled_power_trace_phase_summary.csv"

BATCUR_RE = re.compile(r"BATCUR,t_ms=(\d+),IBAT_UA=(-?\d+|NA)")


def load_old_points() -> list[tuple[float, float]]:
    with OLD_POINTS.open(newline="") as handle:
        return [(float(row["t_s"]), float(row["ibat_ma"])) for row in csv.DictReader(handle)]


def load_new_points() -> list[tuple[float, float]]:
    points = []
    for line in NEW_POINTS.read_text(errors="replace").splitlines():
        match = BATCUR_RE.search(line)
        if match and match.group(2) != "NA":
            points.append((int(match.group(1)) / 1000.0, int(match.group(2)) / 1000.0))
    if not points:
        raise SystemExit("No new OLED BATCUR records found")
    start = points[0][0]
    return [(time_s - start, current_mA) for time_s, current_mA in points]


def load_phase_bounds(path: Path, old_start_ms: float = 0.0) -> dict[str, dict]:
    bounds = {}
    with path.open(newline="") as handle:
        for row in csv.DictReader(handle):
            bounds[row["label"]] = {
                "start_s": float(row["start_ms"]) / 1000.0 - old_start_ms,
                "duration_s": float(row["duration_s"]),
            }
    return bounds


def interpolate(points: list[tuple[float, float]], time_s: float) -> float:
    if time_s <= points[0][0]:
        return points[0][1]
    if time_s >= points[-1][0]:
        return points[-1][1]
    for (left_t, left_v), (right_t, right_v) in zip(points, points[1:]):
        if left_t <= time_s <= right_t:
            if right_t == left_t:
                return left_v
            ratio = (time_s - left_t) / (right_t - left_t)
            return left_v + ratio * (right_v - left_v)
    return points[-1][1]


def time_weighted_mean(points: list[tuple[float, float]], start_s: float, duration_s: float) -> float:
    end_s = start_s + duration_s
    clipped = [(start_s, interpolate(points, start_s))]
    clipped.extend((time_s, value) for time_s, value in points if start_s < time_s < end_s)
    clipped.append((end_s, interpolate(points, end_s)))
    area = sum(
        (right_t - left_t) * (left_v + right_v) / 2.0
        for (left_t, left_v), (right_t, right_v) in zip(clipped, clipped[1:])
    )
    return area / duration_s


def main() -> None:
    old_points = load_old_points()
    new_points = load_new_points()
    old_start_ms = float(next(csv.DictReader(OLD_POINTS.open()))["t_ms"])
    with NEW_PHASES.open(newline="") as handle:
        new_start_ms = min(float(row["start_ms"]) for row in csv.DictReader(handle))
    old_bounds = load_phase_bounds(OLD_PHASES, old_start_ms=old_start_ms / 1000.0)
    new_bounds = load_phase_bounds(NEW_PHASES, old_start_ms=new_start_ms / 1000.0)

    rows = []
    total_old = total_new = total_duration = 0.0
    for label, old in old_bounds.items():
        new = new_bounds.get(label)
        if new is None:
            continue
        duration = min(old["duration_s"], new["duration_s"])
        old_mean = time_weighted_mean(old_points, old["start_s"], duration)
        new_mean = time_weighted_mean(new_points, new["start_s"], duration)
        delta = new_mean - old_mean
        rows.append(
            {
                "label": label,
                "aligned_duration_s": duration,
                "no_oled_mean_mA": old_mean,
                "oled_mean_mA": new_mean,
                "delta_mA": delta,
                "delta_percent": delta / old_mean * 100.0 if old_mean else "",
            }
        )
        total_old += old_mean * duration
        total_new += new_mean * duration
        total_duration += duration

    total_old_mean = total_old / total_duration
    total_new_mean = total_new / total_duration
    rows.append(
        {
            "label": "ALIGNED FULL CYCLE",
            "aligned_duration_s": total_duration,
            "no_oled_mean_mA": total_old_mean,
            "oled_mean_mA": total_new_mean,
            "delta_mA": total_new_mean - total_old_mean,
            "delta_percent": (total_new_mean - total_old_mean) / total_old_mean * 100.0,
        }
    )

    output = DATA / "oled_vs_no_oled_aligned_comparison.csv"
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    print(
        f"Aligned full cycle: no OLED={total_old_mean:.3f} mA, "
        f"OLED={total_new_mean:.3f} mA, "
        f"delta={(total_new_mean - total_old_mean):.3f} mA "
        f"({(total_new_mean - total_old_mean) / total_old_mean * 100.0:.1f}%), "
        f"duration={total_duration:.3f} s"
    )


if __name__ == "__main__":
    main()
