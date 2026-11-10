#!/usr/bin/env python3
"""
Live plotter / analyzer for the SDP810 differential pressure sensor (ESP32-C3).

The firmware prints one line per sample over UART:
    D,<device_ms>,<pressure_pa>,<temperature_c>[,<state>,<reason>]
Any other line (ESP log output) is echoed to the console.

Usage:
    python sdp_plotter.py                       # auto-detect port, live view
    python sdp_plotter.py -p /dev/cu.usbserial-120 -w 60
    python sdp_plotter.py --file logs/sdp_20261020_120000.csv   # analyze a saved log

Keys in the plot window:
    space  pause / resume the display (logging continues)
    z      zero: use the current window mean as pressure offset
    x      clear the zero offset
    c      clear plot buffers and session stats
    s      save a PNG screenshot
    1-4    label what you are doing now: 1 inhale, 2 blow, 3 wind, 4 other
    0      clear the label
Labels go into the CSV, so tools/replay.sh can score the detector per label.
"""

import argparse
import collections
import csv
import datetime
import math
import os
import sys
import threading
import time

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.patches import Patch
from matplotlib.ticker import MaxNLocator
import serial
import serial.tools.list_ports

STATE_NAMES = ["IDLE", "CANDIDATE", "ACTIVE", "LOCKOUT"]
STATE_ACTIVE = 2
REASON_NAMES = ["none", "released", "reverse", "dip", "rough", "weak", "max_time", "data_gap", "slew"]
LABEL_KEYS = {"1": "inhale", "2": "blow", "3": "wind", "4": "other", "0": ""}

# ---- Look & feel -----------------------------------------------------------
SURFACE = "#fcfcfb"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID = "#e4e3df"
BLUE = "#2a78d6"        # pressure (smoothed)
BLUE_LIGHT = "#86b6ef"  # pressure (raw)
ORANGE = "#eb6834"      # temperature
SHADE_ALPHA = 0.15

# Keys used by this tool must not also trigger matplotlib's own shortcuts
for _k in ("keymap.save", "keymap.back", "keymap.pan", "keymap.zoom"):
    plt.rcParams[_k] = []

plt.rcParams.update({
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "axes.edgecolor": GRID,
    "axes.labelcolor": TEXT_SECONDARY,
    "axes.titlecolor": TEXT_PRIMARY,
    "axes.titleweight": "bold",
    "axes.titlesize": 11,
    "axes.titlelocation": "left",
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.8,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "xtick.color": TEXT_SECONDARY,
    "ytick.color": TEXT_SECONDARY,
    "legend.frameon": False,
    "legend.labelcolor": TEXT_SECONDARY,
    "font.size": 9,
})


# ---- Data source -----------------------------------------------------------
class SerialReader(threading.Thread):
    """Reads D,<ms>,<pa>,<temp> lines, stores them and logs them to CSV."""

    def __init__(self, port, baud, log_path, max_samples):
        super().__init__(daemon=True)
        self.lock = threading.Lock()
        self.t = collections.deque(maxlen=max_samples)
        self.p = collections.deque(maxlen=max_samples)
        self.temp = collections.deque(maxlen=max_samples)
        self.state = collections.deque(maxlen=max_samples)
        self.reason = 0
        self.label = ""
        self.total = 0
        self.bad_lines = 0
        self.t0 = None
        self.running = True
        # Session statistics (Welford), independent of the plot window
        self.s_n = 0
        self.s_mean = 0.0
        self.s_m2 = 0.0
        self.s_min = math.inf
        self.s_max = -math.inf

        # This order keeps the ESP32 from resetting: RTS released before open, DTR after
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.timeout = 0.5
        self.ser.rts = False
        self.ser.open()
        self.ser.dtr = False

        os.makedirs(os.path.dirname(log_path) or ".", exist_ok=True)
        self.log_file = open(log_path, "w", newline="")
        self.log = csv.writer(self.log_file)
        self.log.writerow(["host_time_iso", "device_ms", "pressure_pa", "temperature_c",
                           "state", "reason", "label"])

    def clear(self):
        with self.lock:
            self.t.clear()
            self.p.clear()
            self.temp.clear()
            self.state.clear()
            self.t0 = None
            self.s_n, self.s_mean, self.s_m2 = 0, 0.0, 0.0
            self.s_min, self.s_max = math.inf, -math.inf

    def run(self):
        last_flush = time.time()
        while self.running:
            try:
                raw = self.ser.readline()
            except (serial.SerialException, OSError, TypeError) as e:
                if self.running:
                    print(f"[serial] {e}", file=sys.stderr)
                break
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            if not line.startswith("D,"):
                if line:
                    print(f"[esp] {line}")
                continue
            parts = line.split(",")
            try:
                ms, pa, tc = int(parts[1]), float(parts[2]), float(parts[3])
                state = int(parts[4]) if len(parts) > 4 else 0
                reason = int(parts[5]) if len(parts) > 5 else 0
            except (IndexError, ValueError):
                self.bad_lines += 1
                continue

            self.log.writerow([datetime.datetime.now().isoformat(timespec="milliseconds"), ms, pa, tc,
                               STATE_NAMES[state] if 0 <= state < len(STATE_NAMES) else state,
                               REASON_NAMES[reason] if 0 <= reason < len(REASON_NAMES) else reason,
                               self.label])
            with self.lock:
                if self.t0 is None or ms / 1000.0 < (self.t[-1] + self.t0 if self.t else 0):
                    # first sample, or the device rebooted (timestamp went backwards)
                    self.t0 = ms / 1000.0
                    self.t.clear(); self.p.clear(); self.temp.clear(); self.state.clear()
                self.t.append(ms / 1000.0 - self.t0)
                self.p.append(pa)
                self.temp.append(tc)
                self.state.append(state)
                self.reason = reason
                self.total += 1
                self.s_n += 1
                d = pa - self.s_mean
                self.s_mean += d / self.s_n
                self.s_m2 += d * (pa - self.s_mean)
                self.s_min = min(self.s_min, pa)
                self.s_max = max(self.s_max, pa)

            if time.time() - last_flush > 1.0:
                self.log_file.flush()
                last_flush = time.time()

    def snapshot(self):
        with self.lock:
            return (np.fromiter(self.t, float), np.fromiter(self.p, float),
                    np.fromiter(self.temp, float), np.fromiter(self.state, int))

    def session_stats(self):
        with self.lock:
            n = self.s_n
            std = math.sqrt(self.s_m2 / (n - 1)) if n > 1 else 0.0
            return n, self.s_mean, std, self.s_min, self.s_max

    def stop(self):
        self.running = False
        try:
            self.ser.close()
        finally:
            self.log_file.close()


def auto_port():
    ports = list(serial.tools.list_ports.comports())
    preferred = [p.device for p in ports
                 if any(k in (p.device + " " + (p.description or "")).lower()
                        for k in ("usbserial", "usbmodem", "slab", "cp210", "ch34", "ttyusb", "ttyacm"))]
    # On macOS prefer cu.* over tty.*
    preferred.sort(key=lambda d: (not d.startswith("/dev/cu."), "SLAB" in d))
    if preferred:
        return preferred[0]
    sys.exit("No serial port found - pass one with -p")


# ---- Analysis helpers ------------------------------------------------------
def moving_average(y, n):
    if n <= 1 or len(y) < n:
        return y
    c = np.cumsum(np.insert(y, 0, 0.0))
    ma = (c[n:] - c[:-n]) / n
    return np.concatenate([np.full(n - 1, np.nan), ma])


def window_stats(t, p):
    if len(p) == 0:
        return None
    dur = t[-1] - t[0] if len(t) > 1 else 0.0
    return {
        "n": len(p),
        "rate": (len(p) - 1) / dur if dur > 0 else 0.0,
        "cur": p[-1],
        "mean": float(np.mean(p)),
        "std": float(np.std(p, ddof=1)) if len(p) > 1 else 0.0,
        "min": float(np.min(p)),
        "max": float(np.max(p)),
        "rms": float(np.sqrt(np.mean(p ** 2))),
    }


# ---- Figure ----------------------------------------------------------------
class Dashboard:
    def __init__(self, title, window_s, avg_s, live):
        self.window_s = window_s
        self.avg_s = avg_s
        self.live = live
        self.offset = 0.0
        self.paused = False
        self.active_shade = None

        self.fig = plt.figure(figsize=(13, 7.5))
        self.fig.canvas.manager.set_window_title(title)
        gs = self.fig.add_gridspec(2, 3, width_ratios=[1, 1, 0.8], hspace=0.35, wspace=0.3,
                                   left=0.06, right=0.97, top=0.9, bottom=0.08)
        self.ax_p = self.fig.add_subplot(gs[0, :2])
        self.ax_t = self.fig.add_subplot(gs[1, :2], sharex=self.ax_p)
        self.ax_h = self.fig.add_subplot(gs[0, 2])
        self.ax_s = self.fig.add_subplot(gs[1, 2])
        self.ax_s.axis("off")

        self.fig.suptitle(title, x=0.06, ha="left", fontsize=13, fontweight="bold", color=TEXT_PRIMARY)
        self.status = self.fig.text(0.97, 0.955, "", ha="right", va="center", color=TEXT_SECONDARY)

        self.ax_p.set_title("Differential pressure")
        self.ax_p.set_ylabel("Pa")
        (self.l_raw,) = self.ax_p.plot([], [], color=BLUE_LIGHT, lw=1.0, label="raw")
        (self.l_ma,) = self.ax_p.plot([], [], color=BLUE, lw=2.0, label=f"moving avg ({avg_s:g} s)")
        self.ax_p.axhline(0, color=TEXT_SECONDARY, lw=0.8, alpha=0.6)
        self.ax_p.legend(handles=[self.l_raw, self.l_ma,
                                  Patch(color=BLUE, alpha=SHADE_ALPHA, label="detected inhale")],
                         loc="upper left", ncol=3)

        self.ax_t.set_title("Sensor temperature")
        self.ax_t.set_ylabel("°C")
        self.ax_t.set_xlabel("time (s)")
        (self.l_temp,) = self.ax_t.plot([], [], color=ORANGE, lw=2.0)

        self.ax_h.set_title("Pressure distribution (window)")
        self.ax_h.set_xlabel("Pa")
        self.ax_h.set_ylabel("samples")

        self.stats_text = self.ax_s.text(0.0, 1.0, "", va="top", ha="left", family="monospace",
                                         fontsize=9.5, color=TEXT_PRIMARY, transform=self.ax_s.transAxes)

    def draw(self, t, p, temp, state=None, session=None, extra_status=""):
        if len(t) == 0:
            self.status.set_text("waiting for data…" + extra_status)
            return
        p = p - self.offset

        if self.live:
            mask = t >= t[-1] - self.window_s
            t, p, temp = t[mask], p[mask], temp[mask]
            state = state[mask] if state is not None else None

        rate = window_stats(t, p)["rate"] or 20.0
        n_avg = max(1, int(round(self.avg_s * rate)))
        ma = moving_average(p, n_avg)

        self.l_raw.set_data(t, p)
        self.l_ma.set_data(t, ma)
        self.l_temp.set_data(t, temp)

        if self.active_shade is not None:
            self.active_shade.remove()
            self.active_shade = None
        if state is not None and np.any(state == STATE_ACTIVE):
            self.active_shade = self.ax_p.fill_between(
                t, 0, 1, where=state == STATE_ACTIVE, step="post", color=BLUE, alpha=SHADE_ALPHA,
                linewidth=0, transform=self.ax_p.get_xaxis_transform())

        if self.live:
            x0 = max(t[0], t[-1] - self.window_s)
            self.ax_p.set_xlim(x0, x0 + self.window_s)
        else:
            self.ax_p.set_xlim(t[0], max(t[-1], t[0] + 1))
        for ax, y in ((self.ax_p, p), (self.ax_t, temp)):
            lo, hi = float(np.min(y)), float(np.max(y))
            pad = max((hi - lo) * 0.1, 0.05)
            ax.set_ylim(lo - pad, hi + pad)

        self.ax_h.cla()
        self.ax_h.set_title("Pressure distribution (window)")
        self.ax_h.set_xlabel("Pa")
        self.ax_h.set_ylabel("samples")
        bins = min(40, max(5, len(np.unique(p))))
        self.ax_h.hist(p, bins=bins, color=BLUE, edgecolor=SURFACE, linewidth=1.5)
        self.ax_h.xaxis.set_major_locator(MaxNLocator(5))
        self.ax_h.axvline(np.mean(p), color=TEXT_PRIMARY, lw=1.2, ls="--")

        w = window_stats(t, p)
        lines = [
            "WINDOW" + (f" (last {self.window_s:g} s)" if self.live else " (whole file)"),
            f"  samples     {w['n']:>10d}",
            f"  rate        {w['rate']:>10.1f} Hz",
            f"  current     {w['cur']:>10.3f} Pa",
            f"  mean        {w['mean']:>10.3f} Pa",
            f"  std dev     {w['std']:>10.3f} Pa",
            f"  min         {w['min']:>10.3f} Pa",
            f"  max         {w['max']:>10.3f} Pa",
            f"  peak-peak   {w['max'] - w['min']:>10.3f} Pa",
            f"  RMS         {w['rms']:>10.3f} Pa",
            f"  temperature {temp[-1]:>10.2f} °C",
        ]
        if session is not None:
            n, mean, std, mn, mx = session
            lines += [
                "",
                "SESSION (since start / clear)",
                f"  samples     {n:>10d}",
                f"  mean        {mean - self.offset:>10.3f} Pa",
                f"  std dev     {std:>10.3f} Pa",
                f"  min / max   {mn - self.offset:>7.2f} / {mx - self.offset:.2f} Pa",
            ]
        if self.offset:
            lines += ["", f"zero offset {self.offset:+.3f} Pa (x to clear)"]
        self.stats_text.set_text("\n".join(lines))
        self.status.set_text(("PAUSED  " if self.paused else "") + extra_status)


# ---- Modes -----------------------------------------------------------------
def run_live(args):
    port = args.port or auto_port()
    log_path = args.log or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "logs",
        datetime.datetime.now().strftime("sdp_%Y%m%d_%H%M%S.csv"))
    max_samples = int(args.window * 100) + 100   # enough for up to 100 Hz

    reader = SerialReader(port, args.baud, log_path, max_samples)
    reader.start()
    print(f"Reading {port} @ {args.baud}, logging to {log_path}")

    dash = Dashboard(f"SDP810 live — {port}", args.window, args.avg, live=True)

    def on_key(ev):
        if ev.key == " ":
            dash.paused = not dash.paused
        elif ev.key == "z":
            t, p, _, _ = reader.snapshot()
            if len(p):
                dash.offset = float(np.mean(p[t >= t[-1] - args.window]))
        elif ev.key == "x":
            dash.offset = 0.0
        elif ev.key == "c":
            reader.clear()
        elif ev.key in LABEL_KEYS:
            reader.label = LABEL_KEYS[ev.key]
        elif ev.key == "s":
            name = datetime.datetime.now().strftime("sdp_%Y%m%d_%H%M%S.png")
            dash.fig.savefig(os.path.join(os.path.dirname(log_path), name), dpi=150)
            print(f"saved {name}")

    dash.fig.canvas.mpl_connect("key_press_event", on_key)

    def update(_):
        if dash.paused:
            return
        t, p, temp, state = reader.snapshot()
        now = STATE_NAMES[state[-1]] if len(state) and 0 <= state[-1] < len(STATE_NAMES) else "-"
        dash.draw(t, p, temp, state, reader.session_stats(),
                  f"{now} (last: {REASON_NAMES[reader.reason]}) · label: {reader.label or 'none'} · "
                  f"{port} · {reader.total} samples · log: {os.path.basename(log_path)}")

    anim = FuncAnimation(dash.fig, update, interval=100, cache_frame_data=False)  # noqa: F841
    try:
        plt.show()
    finally:
        reader.stop()
        print(f"Log saved: {log_path}")


def run_file(args):
    t, p, temp, state = [], [], [], []
    with open(args.file) as f:
        for row in csv.DictReader(f):
            t.append(int(row["device_ms"]) / 1000.0)
            p.append(float(row["pressure_pa"]))
            temp.append(float(row["temperature_c"]))
            state.append(STATE_ACTIVE if row.get("state") == "ACTIVE" else 0)
    if not t:
        sys.exit("No samples in file")
    t = np.array(t) - t[0]
    dash = Dashboard(f"SDP810 log — {os.path.basename(args.file)}", args.window, args.avg, live=False)
    dash.draw(t, np.array(p), np.array(temp), np.array(state))
    plt.show()


def main():
    ap = argparse.ArgumentParser(description="SDP810 live UART plotter")
    ap.add_argument("-p", "--port", help="serial port (default: auto-detect)")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-w", "--window", type=float, default=30.0, help="visible time window in seconds")
    ap.add_argument("-a", "--avg", type=float, default=1.0, help="moving average length in seconds")
    ap.add_argument("-l", "--log", help="CSV log path (default: tools/logs/sdp_<timestamp>.csv)")
    ap.add_argument("-f", "--file", help="analyze a saved CSV log instead of reading UART")
    args = ap.parse_args()
    run_file(args) if args.file else run_live(args)


if __name__ == "__main__":
    main()
