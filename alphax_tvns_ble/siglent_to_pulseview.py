"""Convert Siglent SDS CSV exports into a plain CSV that PulseView can import.

Siglent files start with ~11 lines of scope settings (with ragged column counts),
which PulseView's CSV importer rejects. This script drops that preamble, keeps the
voltage columns (one per channel), and optionally averages blocks of samples to cut the file size.

Usage:  python siglent_to_pulseview.py [--decimate N] file1.csv [file2.csv ...]
Output: <name>_pv.csv next to each input, one analog column per channel (CH1, CH2...).
In PulseView: Import > CSV, column format "a" per channel (e.g. "aa"), first line is header,
and set the
sample rate to the value this script prints.
"""
import argparse
import sys
from pathlib import Path

import numpy as np


def read_siglent(path):
    meta = {}
    with open(path, "r", newline="") as f:
        for line_no, line in enumerate(f, 1):
            parts = [p for p in line.strip().split(",") if p]
            if parts and parts[0] == "Second":
                header_lines = line_no
                break
            if len(parts) >= 2:
                meta[parts[0]] = parts[1:]
        else:
            raise ValueError("no 'Second,Volt' line found; not a Siglent CSV?")
    channels = meta.get("Source", ["CH1"])
    cols = tuple(range(1, len(channels) + 1))
    volts = np.loadtxt(path, delimiter=",", skiprows=header_lines, usecols=cols,
                       dtype=np.float32, ndmin=2)
    interval = float(meta["Sample Interval"][0].split(":")[-1])
    expected = int(meta["Record Length"][0].split(":")[-1])
    return volts, channels, interval, expected


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decimate", type=int, default=100,
                    help="average every N samples (default 100; 1 = keep all)")
    ap.add_argument("files", nargs="+", type=Path)
    args = ap.parse_args()

    for path in args.files:
        volts, channels, interval, expected = read_siglent(path)
        if len(volts) < expected:
            print(f"WARNING {path.name}: only {len(volts):,} of {expected:,} samples; "
                  "the export looks truncated", file=sys.stderr)
        n = args.decimate
        if n > 1:
            volts = volts[: len(volts) // n * n].reshape(-1, n, volts.shape[1]).mean(axis=1)
        rate = 1.0 / (interval * n)
        out = path.with_name(path.stem + "_pv.csv")
        np.savetxt(out, volts, fmt="%.5f", delimiter=",", header=",".join(channels), comments="")
        ranges = ", ".join(f"{c} {volts[:, i].min():+.3f} to {volts[:, i].max():+.3f} V"
                           for i, c in enumerate(channels))
        print(f"{path.name} -> {out.name}: {len(volts):,} samples, "
              f"sample rate {rate / 1e6:g} MHz, {ranges}")


if __name__ == "__main__":
    main()
