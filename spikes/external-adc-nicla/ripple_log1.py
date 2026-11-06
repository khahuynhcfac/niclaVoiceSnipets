#!/usr/bin/env python3
"""Plot the wiggle on the leaking capacitor in log1.csv.

A 16-bit converter that measures from 0 to 4.096 V can only report voltage
in steps of 4.096 / 65536 = 62.5 µV. One of those steps is called 1 LSB.
"2–4 LSB" means the wiggle is 2 to 4 of those steps tall.

Writes log1_ripple.png next to this script.
"""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
STEP_UV = 4.096 / 65536 * 1e6  # 62.5 µV, one step of that converter


def main():
    t, uv = np.loadtxt(HERE / "log1.csv", delimiter=",", skiprows=1, usecols=(0, 1), unpack=True)

    seconds = np.arange(int(np.floor(t[-1])) + 1)
    span_uv = []
    for s in seconds:
        window = uv[(t >= s) & (t < s + 1)]
        span_uv.append(window.max() - window.min() if len(window) else np.nan)
    span_uv = np.array(span_uv)
    steps = span_uv / STEP_UV

    quiet = 0  # two levels, 125 µV apart
    noisy = 2  # three levels, 250 µV from top to bottom

    print(f"one step (1 LSB) = {STEP_UV:.1f} µV")
    print(f"voltage falls {uv[0] - uv[-1]:.0f} µV in {t[-1]:.1f} s (the leak)")
    print(f"in each second the wiggle is {steps.min():.0f} to {steps.max():.0f} steps tall")
    print(f"  {np.sum(steps == 2):.0f} seconds are 2 steps (125 µV)")
    print(f"  {np.sum(steps == 4):.0f} seconds are 4 steps (250 µV)")
    print(f"example, second {quiet}: {_span_text(t, uv, quiet)}")
    print(f"example, second {noisy}: {_span_text(t, uv, noisy)}")

    fig = plt.figure(figsize=(10, 8.2))
    ax_all = fig.add_subplot(3, 1, 1)
    ax_quiet = fig.add_subplot(3, 2, 3)
    ax_noisy = fig.add_subplot(3, 2, 4)
    ax_steps = fig.add_subplot(3, 1, 3)

    ax_all.plot(t, uv / 1e6, color="#1f4e79", lw=0.8)
    ax_all.set_title("The slow fall is the capacitor leaking")
    ax_all.set_xlabel("time (s)")
    ax_all.set_ylabel("voltage (V)")
    ax_all.set_xlim(t[0], t[-1])
    ax_all.text(
        0.01,
        0.08,
        "The downward slope is the leak. Each little vertical jump is the wiggle.",
        transform=ax_all.transAxes,
        fontsize=9,
        color="#333333",
    )

    _zoom(ax_quiet, t, uv, quiet, "A quiet second: the reading flips across 2 steps")
    _zoom(ax_noisy, t, uv, noisy, "A noisier second: the reading covers 4 steps")

    ax_steps.bar(seconds, steps, width=0.9, color="#1f4e79", label="height of the wiggle")
    ax_steps.axhline(1, color="#c45911", lw=1.2, label="1 step: smallest wiggle of a 16-bit, 0–4.096 V converter")
    ax_steps.set_ylim(0, 5)
    ax_steps.set_yticks([0, 1, 2, 3, 4])
    ax_steps.set_xlabel("time (s)")
    ax_steps.set_ylabel("wiggle height (steps)")
    ax_steps.set_title("Each second, the wiggle is 2 or 4 steps tall  —  that is what “2–4 LSB” means")
    ax_steps.legend(loc="upper right", fontsize=8)
    ax_steps.set_xlim(-0.6, seconds[-1] + 0.6)

    fig.tight_layout()
    out = HERE / "log1_ripple.png"
    fig.savefig(out, dpi=140)
    print(f"wrote {out}")


def _span_text(t, uv, second):
    window = uv[(t >= second) & (t < second + 1)]
    levels = np.unique(window)
    span = levels[-1] - levels[0]
    listed = ", ".join(f"{v / 1e6:.6f} V" for v in levels)
    return f"{span:.0f} µV = {span / STEP_UV:.0f} steps, levels {listed}"


def _zoom(ax, t, uv, second, title):
    m = (t >= second) & (t < second + 1)
    tt, vv = t[m], uv[m]
    levels = np.unique(vv)
    lo, hi = levels[0], levels[-1]
    for level in levels:
        ax.axhline(level, color="#d0d0d0", lw=0.6, zorder=0)
    ax.plot(tt, vv, color="#1f4e79", lw=1.0, marker=".", ms=3)
    span = hi - lo
    t_arrow = float(np.median(tt[vv == lo]))
    ax.annotate(
        "",
        xy=(t_arrow, hi),
        xytext=(t_arrow, lo),
        arrowprops=dict(arrowstyle="<->", color="#c45911", lw=1.4),
    )
    n_steps = span / STEP_UV
    ax.text(
        0.18,
        0.97,
        f"top to bottom: {span:.0f} µV  =  {n_steps:.0f} steps",
        transform=ax.transAxes,
        ha="left",
        va="top",
        fontsize=9,
        color="#c45911",
        bbox=dict(boxstyle="square,pad=0.25", fc="white", ec="none"),
    )
    ax.set_xlim(second, second + 1)
    ax.set_ylim(lo - 80, hi + 100)
    ax.set_yticks(levels)
    ax.ticklabel_format(axis="y", useOffset=False, style="plain")
    ax.yaxis.set_major_formatter(lambda v, _p: f"{v:.0f}")
    ax.set_title(title, fontsize=11)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("voltage (µV)")


if __name__ == "__main__":
    main()
