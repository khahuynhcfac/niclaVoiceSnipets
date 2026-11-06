#!/usr/bin/env python3
"""Plot how fast the capacitor in log1.csv is losing energy.

Energy leaves at P = C * V * dV/dt. A falling voltage makes this negative,
so the plotted leak is -P, in µW (same as µJ/s).

Sample-to-sample differences are the 250 µV wiggle, not the leak. The slope
is a straight-line fit inside a sliding window instead.

Writes log1_leak_uW.png and log1_leak_steps.png next to this script.
"""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
C_F = 22e-3  # 22 millifarad
WINDOW_S = 5.0
STEP_S = 0.25


def leak_uW(t, v):
    """One result per window: slope, voltage at the middle, and leak power.

    Leak power is positive when energy is leaving the capacitor.
    """
    centres = np.arange(t[0] + WINDOW_S / 2, t[-1] - WINDOW_S / 2, STEP_S)
    slopes = np.empty(len(centres))
    v_centres = np.empty(len(centres))
    for i, centre in enumerate(centres):
        m = (t >= centre - WINDOW_S / 2) & (t < centre + WINDOW_S / 2)
        slopes[i], _intercept = np.polyfit(t[m], v[m], 1)
        v_centres[i] = np.interp(centre, t, v)
    power = -C_F * v_centres * slopes * 1e6
    return centres, slopes, v_centres, power


def main():
    t, uv = np.loadtxt(HERE / "log1.csv", delimiter=",", skiprows=1, usecols=(0, 1), unpack=True)
    v = uv * 1e-6
    centres, slopes, v_centres, power = leak_uW(t, v)
    example = int(np.argmin(np.abs(centres - 10.0)))

    print(f"C = {C_F * 1e3:.0f} mF, window = {WINDOW_S:.0f} s")
    print(f"voltage {v[0]:.6f} V -> {v[-1]:.6f} V in {t[-1]:.1f} s")
    print(f"leak over the record: mean {power.mean():.2f} µW, "
          f"min {power.min():.2f} µW, max {power.max():.2f} µW")
    print(
        f"example window centred at {centres[example]:.2f} s: "
        f"slope {slopes[example] * 1e6:.2f} µV/s, "
        f"V {v_centres[example]:.6f} V, "
        f"leak {power[example]:.2f} µW"
    )

    fig, ax = plt.subplots(figsize=(10, 4.2))
    ax.plot(centres, power, color="#1f4e79", lw=1.4)
    ax.axhline(power.mean(), color="#c45911", lw=1.0, label=f"mean {power.mean():.2f} µW")
    ax.set_xlim(centres[0], centres[-1])
    ax.set_xlabel("time (s)")
    ax.set_ylabel("leak (µW, same as µJ/s)")
    ax.set_title(f"Energy leaving the {C_F * 1e3:.0f} mF capacitor  —  slope over {WINDOW_S:.0f} s")
    ax.legend(loc="best", fontsize=9)
    fig.tight_layout()
    out = HERE / "log1_leak_uW.png"
    fig.savefig(out, dpi=140)
    print(f"wrote {out}")

    steps = HERE / "log1_leak_steps.png"
    fig_steps = plot_steps(t, v, centres, slopes, v_centres, power, example)
    fig_steps.savefig(steps, dpi=140)
    print(f"wrote {steps}")


def plot_steps(t, v, centres, slopes, v_centres, power, example):
    """Five panels, in the order the leak is calculated."""
    centre = centres[example]
    half = WINDOW_S / 2
    m = (t >= centre - half) & (t < centre + half)
    slope, intercept = np.polyfit(t[m], v[m], 1)
    blue = "#1f4e79"
    orange = "#c45911"

    fig, axes = plt.subplots(5, 1, figsize=(10, 12.5), constrained_layout=True)
    ax_v, ax_zoom, ax_slope, ax_vc, ax_p = axes

    ax_v.plot(t, v, color=blue, lw=0.7)
    ax_v.axvspan(centre - half, centre + half, color=orange, alpha=0.18)
    ax_v.axvline(centre, color=orange, lw=0.8)
    ax_v.set_xlim(t[0], t[-1])
    ax_v.set_ylabel("voltage (V)")
    ax_v.set_title("1. Measured voltage. The shaded band is one 5 s window.")

    ax_zoom.plot(t[m], v[m] * 1e6, color=blue, lw=0.8, marker=".", ms=2, label="samples in this window")
    t_line = np.array([centre - half, centre + half])
    ax_zoom.plot(t_line, (slope * t_line + intercept) * 1e6, color=orange, lw=1.6, label="straight-line fit")
    ax_zoom.plot(centre, v_centres[example] * 1e6, "o", color=orange, ms=6, label="voltage used at the middle")
    ax_zoom.set_xlim(centre - half, centre + half)
    ax_zoom.ticklabel_format(axis="y", useOffset=False, style="plain")
    ax_zoom.set_ylabel("voltage (µV)")
    ax_zoom.set_title(
        f"2. That window, centred at {centre:.2f} s. "
        f"Slope of the line = {slope * 1e6:.1f} µV/s."
    )
    ax_zoom.legend(loc="best", fontsize=8)

    ax_slope.plot(centres, slopes * 1e6, color=blue, lw=1.2)
    ax_slope.plot(centre, slopes[example] * 1e6, "o", color=orange, ms=6)
    ax_slope.set_xlim(centres[0], centres[-1])
    ax_slope.set_ylabel("slope (µV/s)")
    ax_slope.set_title("3. Slope of the straight line in every window. Negative means the voltage is falling.")

    ax_vc.plot(centres, v_centres, color=blue, lw=1.2)
    ax_vc.plot(centre, v_centres[example], "o", color=orange, ms=6)
    ax_vc.set_xlim(centres[0], centres[-1])
    ax_vc.set_ylabel("voltage (V)")
    ax_vc.set_title("4. Voltage at the middle of every window, read from the samples on either side of that time.")

    ax_p.plot(centres, power, color=blue, lw=1.2, label="leak")
    ax_p.plot(centre, power[example], "o", color=orange, ms=6, label="the window from panel 2")
    ax_p.axhline(power.mean(), color=orange, lw=1.0, label=f"mean {power.mean():.2f} µW")
    ax_p.set_xlim(centres[0], centres[-1])
    ax_p.set_xlabel("time (s)")
    ax_p.set_ylabel("leak (µW)")
    ax_p.set_title(
        f"5. Leak = −C × panel 4 × panel 3, with C = {C_F * 1e3:.0f} mF. "
        f"This point is {power[example]:.2f} µW."
    )
    ax_p.legend(loc="best", fontsize=8)
    return fig


if __name__ == "__main__":
    main()
