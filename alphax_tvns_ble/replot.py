"""Re-plot the bench captures as current (mA) with large, plain axes.

Current = voltage across the 500 ohm Rbody resistor / 500. proto_atclips is the
voltage at the electrode clips on skin, divided by the measured 10 kohm skin load.
Run from this folder:  python replot.py   ->  writes PNGs into plots/
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).parent
OUT = HERE / "plots"
R_BODY = 500.0
R_SKIN = 10e3      # measured load between the clips for proto_atclips
DARK = "#3B0A63"
# light -> dark purple, one step per amplitude in the sweep
SWEEP = ["#B497DB", "#8862C0", "#5A2D96", "#3B0A63"]

plt.rcParams.update({
    "font.size": 16, "axes.titlesize": 20, "axes.labelsize": 18,
    "xtick.labelsize": 15, "ytick.labelsize": 15,
    "axes.spines.top": False, "axes.spines.right": False,
    "axes.linewidth": 1.2, "lines.linewidth": 2,
})


def load(name):
    """Return (time s, volts) for Siglent, Tektronix-legacy, or cleaned TEK CSVs."""
    path = HERE / name
    lines = path.read_text(errors="ignore").splitlines()
    if lines[0].startswith("Record Length,Analog"):          # Siglent SDS
        skip = next(i for i, l in enumerate(lines) if l.startswith("Second")) + 1
        d = np.loadtxt(path, delimiter=",", skiprows=skip, usecols=(0, 1))
    elif lines[0].startswith("Record Length,"):              # Tek TBS1072B
        d = np.array([[float(x) for x in l.split(",")[3:5]] for l in lines])
    else:                                                    # tek_clean: TIME,CH1
        d = np.loadtxt(path, delimiter=",", skiprows=1)
    return d[:, 0], d[:, 1]


def smooth(v, n):
    return np.convolve(v, np.ones(n) / n, mode="same") if n > 1 else v


def align(t, v, frac=0.3):
    """Time in us, zero at the first edge (first sample past frac * peak)."""
    i = np.argmax(np.abs(v - np.median(v)) > frac * np.abs(v - np.median(v)).max())
    return (t - t[i]) * 1e6


def finish(ax, title, ylabel, fname):
    ax.set_title(title, pad=12)
    ax.set_xlabel("Time (µs)")
    ax.set_ylabel(ylabel)
    ax.axhline(0, color="#999999", lw=1, zorder=0)
    ax.figure.tight_layout()
    ax.figure.savefig(OUT / fname, dpi=200)
    plt.close(ax.figure)


def main():
    OUT.mkdir(exist_ok=True)

    # 0. No load (Siglent SDS00005 "skin to gnd", block-averaged to 100 ns in no_load.csv).
    # Same mV / 500 ohm scaling as the "Simple body load" panel it was shown beside.
    t, v = load("no_load.csv")
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(align(t, v), v / R_BODY * 1e3, color=DARK)
    ax.set_xlim(-100, 650)
    finish(ax, "No load", "Current (mA)", "0_no_load.png")

    # 0b. Improved no-load waveform: MOCKUP redrawn from a phone photo of the scope
    # (no export exists). Same timing as plot 0: 250 us phases, 50 us gap. The photo
    # had the probe polarity flipped, so it is inverted here to match plot 0
    # (negative phase first); amplitudes ~ -0.245 / +0.255 mA from the photo's step
    # heights; ~2 us edges; small seeded Gaussian noise.
    x = np.arange(-100, 650, 0.1)
    i_ma = np.where((x >= 0) & (x < 250), -0.245, 0.0)
    i_ma = np.where((x >= 300) & (x < 550), 0.255, i_ma)
    i_ma = smooth(i_ma, 20) + np.random.default_rng(0).normal(0, 0.0012, x.size)
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(x, i_ma, color=DARK)
    ax.set_xlim(-100, 650)
    finish(ax, "No load, improved", "Current (mA)", "0b_no_load_improved_MOCKUP.png")

    # 1. Simple body load (Siglent, 10 ns/sample -> 1 us moving average)
    t, v = load("across body.csv")
    v = smooth(v, 100)
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(align(t, v), v / R_BODY * 1e3, color=DARK)
    ax.set_xlim(-100, 800)
    finish(ax, "Simple body load", "Current (mA)", "1_simple_body_load.png")

    # 2. Prototype on skin, at the clips (measured skin load ~10 kohm).
    # Full record, so the absent second phase is visible.
    t, v = load("proto_atclips.CSV")
    x = align(t, v)
    fig, ax = plt.subplots(figsize=(12, 6))
    ax.plot(x, v / R_SKIN * 1e3, color=DARK)
    ax.set_xlim(x[0], x[-1])
    finish(ax, "Prototype on skin: missing positive phase", "Current (mA)",
           "2_proto_on_skin.png")

    # 3. 6 mA into the 500 ohm dummy load
    t, v = load("tek_clean/TEK00005.CSV")
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(align(t, v), v / R_BODY * 1e3, color=DARK)
    ax.set_xlim(-100, 700)
    finish(ax, "6 mA into 500 Ω load", "Current (mA)", "3_6mA_500ohm.png")

    # 4. Amplitude sweep up to 6 mA, smallest to largest (TEK3, 2, 4, 5)
    fig, ax = plt.subplots(figsize=(11, 6.5))
    for n, color in zip([3, 2, 4, 5], SWEEP):
        t, v = load(f"tek_clean/TEK0000{n}.CSV")
        x, i_ma = align(t, v), v / R_BODY * 1e3
        ax.plot(x, i_ma, color=color)
        top = np.median(i_ma[(x > 330) & (x < 520)])
        ax.text(560, top, f"{top:.0f} mA", color="#333333", fontsize=14, va="center")
    ax.set_xlim(-100, 680)
    finish(ax, "Amplitude sweep, 500 Ω load", "Current (mA)", "4_amplitude_sweep.png")


if __name__ == "__main__":
    main()
