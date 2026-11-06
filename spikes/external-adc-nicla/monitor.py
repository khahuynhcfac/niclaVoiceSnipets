#!/usr/bin/env python3
"""
monitor.py — Live voltage (µV) and/or power (µW) monitor.

Requires:  pip install pyserial pyqtgraph PyQt5

Usage:
    python monitor.py --portV COM25 --portP COM26
    python monitor.py --portV COM25 --fileV v.csv
    python monitor.py --portP COM26 --fileP p.csv
"""

import argparse
import csv
import queue
import sys
import threading
import time
import serial
import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtWidgets


# ── constants ────────────────────────────────────────────────────────────────
BAUD      = 115200
UPDATE_MS = 50    # GUI refresh interval (ms)

# prefix, raw unit, SI quantity, plot colour, window label
_CHANNELS = {
    "V": ("voltage", "µV", "V", "#00d4ff", "V"),
    "P": ("power",   "µW", "W", "#ffd700", "P"),
}


# ── serial reader thread ─────────────────────────────────────────────────────
def serial_reader(port: str, baud: int, q: queue.Queue, stop: threading.Event,
                  t0: float):
    """Open port, read lines, push (kind, t, value) tuples into q.

    Timestamps are recorded here, in the reader thread, immediately after
    readline() returns — before the GIL or Qt timer can introduce batching.
    """
    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as exc:
        q.put(("error", 0.0, str(exc)))
        return

    while not stop.is_set():
        try:
            raw = ser.readline()
        except serial.SerialException as exc:
            q.put(("error", 0.0, str(exc)))
            break
        t = time.time() - t0          # timestamp at the moment of arrival
        line = raw.decode("ascii", errors="ignore").strip()
        if not line:
            continue
        try:
            q.put(("data", t, float(line)))
        except ValueError:
            pass  # skip header / non-numeric lines

    ser.close()


# ── main window ──────────────────────────────────────────────────────────────
class Monitor(QtWidgets.QMainWindow):
    def __init__(self, channels: list[dict]):
        super().__init__()
        titles = " | ".join(f"{ch['label']} ({ch['unit']})" for ch in channels)
        self.setWindowTitle(f"ADC Monitor  —  {titles}")

        self._stop = threading.Event()
        self._t0 = time.time()          # common time origin
        self._channels = channels

        pg.setConfigOption("background", "#1a1a1a")
        pg.setConfigOption("foreground", "#dddddd")

        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        hbox = QtWidgets.QHBoxLayout(central)
        hbox.setContentsMargins(6, 6, 6, 6)
        hbox.setSpacing(8)

        first_plot = None
        for i, ch in enumerate(channels):
            ch["q"] = queue.Queue()
            ch["ts"] = []
            ch["vals"] = []
            ch["csv"] = _open_csv(ch["file"], ch["col"])

            pw = pg.PlotWidget(title=f"{ch['name'].capitalize()}  [{ch['port']}]  ({ch['unit']})")
            pw.showGrid(x=True, y=True, alpha=0.25)
            pw.setLabel("left", ch["unit"])
            pw.setLabel("bottom", "time (s)")
            pw.setMinimumWidth(480)
            if first_plot is None:
                first_plot = pw
            elif len(channels) > 1:
                pw.setXLink(first_plot)
            hbox.addWidget(pw)
            ch["plot"] = pw
            ch["curve"] = pw.plot(pen=pg.mkPen(ch["color"], width=1))

            lbl = QtWidgets.QLabel(f"{ch['label']}: waiting…")
            lbl.setStyleSheet("padding: 0 8px;")
            if i:
                self.statusBar().addPermanentWidget(QtWidgets.QLabel("|"))
            self.statusBar().addPermanentWidget(lbl)
            ch["label_widget"] = lbl

            threading.Thread(
                target=serial_reader,
                args=(ch["port"], BAUD, ch["q"], self._stop, self._t0),
                daemon=True,
                name=f"reader-{ch['label']}",
            ).start()

        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._update)
        self._timer.start(UPDATE_MS)

        self.resize(640 * len(channels), 520)

    # ── update slot (runs in GUI thread) ─────────────────────────────────────
    def _update(self):
        for ch in self._channels:
            self._drain(ch)
            if ch["ts"]:
                div, lbl = _choose_unit(ch["vals"], ch["quantity"])
                ch["plot"].setLabel("left", lbl)
                ch["curve"].setData(ch["ts"], [v / div for v in ch["vals"]])

    def _drain(self, ch):
        last = None
        while True:
            try:
                kind, t, val = ch["q"].get_nowait()
            except queue.Empty:
                break
            if kind == "error":
                ch["label_widget"].setText(f"{ch['label']}: ERROR — {val}")
                continue
            ch["ts"].append(t)
            ch["vals"].append(val)
            if ch["csv"]:
                ch["csv"][1].writerow([f"{t:.6f}", val])
            last = val
        if last is not None:
            ch["label_widget"].setText(f"{ch['label']}: {last:.3g} {ch['unit']}")

    # ── cleanup ───────────────────────────────────────────────────────────────
    def closeEvent(self, event):
        self._stop.set()
        self._timer.stop()
        for ch in self._channels:
            if ch["csv"]:
                ch["csv"][0].flush()
                ch["csv"][0].close()
        event.accept()


# ── SI auto-scaling ───────────────────────────────────────────────────────────
def _choose_unit(vals: list[float], quantity: str) -> tuple[float, str]:
    """
    Raw data is in micro-units (µV or µW).
    Picks the best SI prefix so the displayed numbers stay in [1, 1000).
    Returns (divisor, label_string).
    """
    if not vals:
        return 1.0, f"µ{quantity}"
    max_abs = max(abs(v) for v in vals)
    if max_abs >= 1_000_000:
        return 1_000_000.0, quantity          # V or W
    elif max_abs >= 1_000:
        return 1_000.0, f"m{quantity}"        # mV or mW
    else:
        return 1.0, f"µ{quantity}"            # µV or µW


# ── helpers ───────────────────────────────────────────────────────────────────
def _open_csv(path: str | None, col_name: str):
    """Return (file, writer) or None."""
    if not path:
        return None
    f = open(path, "w", newline="")
    w = csv.writer(f)
    w.writerow(["timestamp_s", col_name])
    return (f, w)


def _channel(key: str, port: str, file: str | None) -> dict:
    name, unit, quantity, color, label = _CHANNELS[key]
    return {
        "label": label,
        "name": name,
        "unit": unit,
        "quantity": quantity,
        "color": color,
        "col": f"{name}_{unit.replace('µ', 'u')}",
        "port": port,
        "file": file,
    }


# ── entry point ───────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Live µV / µW serial monitor with Qt plots. "
                    "Pass one port or both."
    )
    parser.add_argument("--portV", default=None, metavar="PORT",
                        help="Serial port streaming voltage (µV), e.g. COM25")
    parser.add_argument("--portP", default=None, metavar="PORT",
                        help="Serial port streaming power (µW), e.g. COM26")
    parser.add_argument("--fileV", default=None, metavar="FILE",
                        help="Optional CSV file to save voltage data")
    parser.add_argument("--fileP", default=None, metavar="FILE",
                        help="Optional CSV file to save power data")
    args = parser.parse_args()

    if not args.portV and not args.portP:
        parser.error("specify at least one of --portV or --portP")
    if args.fileV and not args.portV:
        parser.error("--fileV requires --portV")
    if args.fileP and not args.portP:
        parser.error("--fileP requires --portP")

    channels = []
    if args.portV:
        channels.append(_channel("V", args.portV, args.fileV))
    if args.portP:
        channels.append(_channel("P", args.portP, args.fileP))

    app = QtWidgets.QApplication(sys.argv)
    win = Monitor(channels)
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
