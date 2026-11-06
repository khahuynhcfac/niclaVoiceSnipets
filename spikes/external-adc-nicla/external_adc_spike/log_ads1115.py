#!/usr/bin/env python3
"""Log ADS1115 microvolts from the Nicla Voice and show them live.

Each integer line from the board is one sample in microvolts. Any other line
is printed and not stored.

Opening the port resets the board. The log clock starts at the first sample.

    python log_ads1115.py -p COM5 -o capture.csv
    python log_ads1115.py --port COM5 --time 30 --output capture.csv

Requires: pip install pyserial matplotlib
"""

import argparse
import csv
import sys
import threading
import time
from collections import deque

import matplotlib.pyplot as plt
import serial
from matplotlib.animation import FuncAnimation
from matplotlib.widgets import RadioButtons

BAUD_DEFAULT = 115200
WINDOWS_S = (0.1, 1, 2, 5, 10, 20, 50, 100)
MAX_WINDOW_S = WINDOWS_S[-1]
DEFAULT_WINDOW_S = 5
PLOT_POINTS = 1000


def parse_args():
    parser = argparse.ArgumentParser(
        description="Log Nicla ADS1115 samples to CSV and plot them live."
    )
    parser.add_argument(
        "-p", "--port", required=True, help="Serial port, for example COM5"
    )
    parser.add_argument(
        "-t",
        "--time",
        type=float,
        default=None,
        help="Seconds to log, starting at the first sample. Omit to log until the plot window is closed",
    )
    parser.add_argument(
        "-o", "--output", required=True, help="CSV file to write"
    )
    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=BAUD_DEFAULT,
        help=f"Baud rate (default {BAUD_DEFAULT})",
    )
    args = parser.parse_args()
    if args.time is not None and args.time <= 0:
        parser.error("--time must be greater than 0")
    if args.baud <= 0:
        parser.error("--baud must be greater than 0")
    return args


def y_axis_scale(values_uv):
    peak = max(abs(value) for value in values_uv)
    if peak >= 1_000_000:
        return 1_000_000, "V"
    if peak >= 1_000:
        return 1_000, "mV"
    return 1, "uV"


def downsample_plot(xs, ys, limit):
    count = len(xs)
    if count <= limit:
        return xs, ys
    last = limit - 1
    index = [i * (count - 1) // last for i in range(limit)]
    return [xs[i] for i in index], [ys[i] for i in index]


def parse_sample(line):
    text = line.strip()
    if not text:
        return None
    if text[0] == "-":
        digits = text[1:]
    else:
        digits = text
    if digits.isdecimal():
        return int(text)
    return None


def main():
    args = parse_args()
    stop = threading.Event()
    lock = threading.Lock()
    samples = deque()
    state = {"window_s": DEFAULT_WINDOW_S, "done": False, "count": 0}

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as exc:
        print(exc, file=sys.stderr)
        return 1

    csv_file = open(args.output, "w", newline="")
    writer = csv.writer(csv_file)
    writer.writerow(["t_s", "uv", "dt_s"])

    def read_serial():
        t_first = None
        t_prev = None
        try:
            while not stop.is_set():
                if (
                    args.time is not None
                    and t_first is not None
                    and time.perf_counter() - t_first >= args.time
                ):
                    break
                raw = port.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                sample = parse_sample(line)
                now = time.perf_counter()
                if sample is None:
                    if line:
                        print(line)
                    continue
                if t_first is None:
                    t_first = now
                    if args.time is None:
                        print(f"logging until the plot window closes, to {args.output}")
                    else:
                        print(f"logging {args.time:g} s to {args.output}")
                t_s = now - t_first
                dt_s = 0.0 if t_prev is None else now - t_prev
                t_prev = now
                writer.writerow([f"{t_s:.6f}", sample, f"{dt_s:.6f}"])
                csv_file.flush()
                with lock:
                    samples.append((t_s, sample))
                    while samples and samples[0][0] < t_s - MAX_WINDOW_S:
                        samples.popleft()
                    state["count"] += 1
                if args.time is not None and t_s >= args.time:
                    break
        finally:
            state["done"] = True
            port.close()
            csv_file.close()

    worker = threading.Thread(target=read_serial, name="ads-serial", daemon=True)
    worker.start()

    fig, ax = plt.subplots()
    fig.subplots_adjust(left=0.28)
    line, = ax.plot([], [], lw=1)
    ax.set_xlabel("time since first sample (s)")
    ax.set_ylabel("uV")
    ax.set_title(args.port)
    radio_ax = fig.add_axes([0.02, 0.25, 0.16, 0.55])
    radio = RadioButtons(
        radio_ax,
        [f"{window:g} s" for window in WINDOWS_S],
        active=WINDOWS_S.index(DEFAULT_WINDOW_S),
    )

    def on_window(label):
        state["window_s"] = float(label.split()[0])

    radio.on_clicked(on_window)

    def on_close(_event):
        stop.set()

    fig.canvas.mpl_connect("close_event", on_close)

    def update(_frame):
        window_s = state["window_s"]
        with lock:
            if not samples:
                ax.set_xlim(0, window_s)
                return (line,)
            t_last = samples[-1][0]
            t_min = t_last - window_s
            visible = [(t_s, uv) for t_s, uv in samples if t_s >= t_min]
            count = state["count"]
        xs = [t_s for t_s, _uv in visible]
        ys_uv = [uv for _t_s, uv in visible]
        scale, unit = y_axis_scale(ys_uv)
        ys = [uv / scale for uv in ys_uv]
        xs, ys = downsample_plot(xs, ys, PLOT_POINTS)
        line.set_data(xs, ys)
        ax.set_xlim(t_last - window_s, t_last)
        ax.set_ylabel(unit)
        ymin = min(ys)
        ymax = max(ys)
        span = ymax - ymin
        pad = span * 0.1 if span else max(abs(ymax) * 0.1, 1.0)
        ax.set_ylim(ymin - pad, ymax + pad)
        phase = "done" if state["done"] else "logging"
        ax.set_title(f"{args.port}  {phase}  n={count}")
        return (line,)

    anim = FuncAnimation(fig, update, interval=50, blit=False)
    print(f"{args.port} at {args.baud} baud. Close the plot window to stop early.")
    plt.show()
    if anim.event_source is not None:
        anim.event_source.stop()
    stop.set()
    worker.join()
    print(f"wrote {state['count']} samples to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
