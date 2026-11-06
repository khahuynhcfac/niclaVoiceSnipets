#!/usr/bin/env python3
"""
dual_monitor.py — Live voltage (µV) and power (µW) monitor.

Requires:  pip install pyserial pyqtgraph PyQt5

Usage:
    python dual_monitor.py --portV COM25 --portP COM26
    python dual_monitor.py --portV COM25 --portP COM26 --fileV v.csv --fileP p.csv
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
class DualMonitor(QtWidgets.QMainWindow):
    def __init__(self, portV: str, portP: str, fileV: str | None, fileP: str | None):
        super().__init__()
        self.setWindowTitle("ADC Monitor  —  V (µV) | P (µW)")

        self._stop  = threading.Event()
        self._qV    = queue.Queue()
        self._qP    = queue.Queue()
        self._t0    = time.time()          # common time origin
        self._tsV: list[float] = []        # timestamps (s since start)
        self._valV: list[float] = []       # voltage values
        self._tsP: list[float] = []
        self._valP: list[float] = []
        self._csvV = _open_csv(fileV, "voltage_uV")
        self._csvP = _open_csv(fileP, "power_uW")

        # ── layout ────────────────────────────────────────────────────────
        pg.setConfigOption("background", "#1a1a1a")
        pg.setConfigOption("foreground", "#dddddd")

        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        hbox = QtWidgets.QHBoxLayout(central)
        hbox.setContentsMargins(6, 6, 6, 6)
        hbox.setSpacing(8)

        self._pwV = pg.PlotWidget(title=f"Voltage  [{portV}]  (µV)")
        self._pwP = pg.PlotWidget(title=f"Power    [{portP}]  (µW)")

        for pw, ylabel in ((self._pwV, "µV"), (self._pwP, "µW")):
            pw.showGrid(x=True, y=True, alpha=0.25)
            pw.setLabel("left",   ylabel)
            pw.setLabel("bottom", "time (s)")
            pw.setMinimumWidth(480)
            hbox.addWidget(pw)

        # lock both plots to the same x-axis so they scroll together
        self._pwP.setXLink(self._pwV)

        self._curveV = self._pwV.plot(pen=pg.mkPen("#00d4ff", width=1))
        self._curveP = self._pwP.plot(pen=pg.mkPen("#ffd700", width=1))

        # ── status bar ────────────────────────────────────────────────────
        self._lblV = QtWidgets.QLabel("V: waiting…")
        self._lblP = QtWidgets.QLabel("P: waiting…")
        for lbl in (self._lblV, self._lblP):
            lbl.setStyleSheet("padding: 0 8px;")
        self.statusBar().addPermanentWidget(self._lblV)
        self.statusBar().addPermanentWidget(QtWidgets.QLabel("|"))
        self.statusBar().addPermanentWidget(self._lblP)

        # ── serial threads ────────────────────────────────────────────────
        threading.Thread(
            target=serial_reader,
            args=(portV, BAUD, self._qV, self._stop, self._t0),
            daemon=True,
            name="reader-V",
        ).start()
        threading.Thread(
            target=serial_reader,
            args=(portP, BAUD, self._qP, self._stop, self._t0),
            daemon=True,
            name="reader-P",
        ).start()

        # ── refresh timer ─────────────────────────────────────────────────
        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._update)
        self._timer.start(UPDATE_MS)

        self.resize(1280, 520)

    # ── update slot (runs in GUI thread) ─────────────────────────────────────
    def _update(self):
        self._drain(self._qV, self._tsV, self._valV, self._csvV, self._lblV, "V", "µV")
        self._drain(self._qP, self._tsP, self._valP, self._csvP, self._lblP, "P", "µW")

        if self._tsV:
            div, lbl = _choose_unit(self._valV, "V")
            self._pwV.setLabel("left", lbl)
            self._curveV.setData(self._tsV, [v / div for v in self._valV])

        if self._tsP:
            div, lbl = _choose_unit(self._valP, "W")
            self._pwP.setLabel("left", lbl)
            self._curveP.setData(self._tsP, [v / div for v in self._valP])

    def _drain(self, q, ts, vals, csv_handle, label, prefix, unit):
        last = None
        while True:
            try:
                kind, t, val = q.get_nowait()
            except queue.Empty:
                break
            if kind == "error":
                label.setText(f"{prefix}: ERROR — {val}")
                continue
            ts.append(t)
            vals.append(val)
            if csv_handle:
                csv_handle[1].writerow([f"{t:.6f}", val])
            last = val
        if last is not None:
            label.setText(f"{prefix}: {last:.3g} {unit}")

    # ── cleanup ───────────────────────────────────────────────────────────────
    def closeEvent(self, event):
        self._stop.set()
        self._timer.stop()
        for handle in (self._csvV, self._csvP):
            if handle:
                handle[0].flush()
                handle[0].close()
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


# ── entry point ───────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Live µV / µW dual serial monitor with Qt plots"
    )
    parser.add_argument("--portV", required=True, metavar="PORT",
                        help="Serial port streaming voltage (µV), e.g. COM25")
    parser.add_argument("--portP", required=True, metavar="PORT",
                        help="Serial port streaming power (µW), e.g. COM26")
    parser.add_argument("--fileV", default=None, metavar="FILE",
                        help="Optional CSV file to save voltage data")
    parser.add_argument("--fileP", default=None, metavar="FILE",
                        help="Optional CSV file to save power data")
    args = parser.parse_args()

    app = QtWidgets.QApplication(sys.argv)
    win = DualMonitor(args.portV, args.portP, args.fileV, args.fileP)
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
