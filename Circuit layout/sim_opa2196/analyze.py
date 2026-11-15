"""Analyse an exported comparison run (SQLite from export_raw_waveforms).
Usage: python analyze.py <db> [pulse_index]"""
import sqlite3
import sys

import numpy as np

db = sys.argv[1]
k = int(sys.argv[2]) if len(sys.argv) > 2 else 4          # 0-based pulse index (4 = 5th pulse)
con = sqlite3.connect(db)
cols = [r[1] for r in con.execute("PRAGMA table_info(samples)")]
steps = [r[0] for r in con.execute("select distinct step from samples order by step")]
params = {}
for s, n, v in con.execute("select step,name,value from step_params"):
    params.setdefault(s, {})[n] = v

T, PW, IPG = 0.04, 250e-6, 50e-6
t0 = 1e-3 + k * T


def col(name):
    for c in cols:
        if c.lower() == name.lower():
            return c
    raise KeyError(name)


def integ(t, y, a, b):
    m = (t >= a) & (t <= b)
    return np.trapezoid(y[m], t[m]) if m.sum() > 1 else 0.0


def mean(t, y, a, b):
    m = (t >= a) & (t <= b)
    return integ(t, y, a, b) / (t[m][-1] - t[m][0])


rows = []
for s in steps:
    data = np.array(con.execute(f'select time,* from samples where step={s} order by point').fetchall(),
                    dtype=object)
    names = ["time_"] + cols
    d = {n: np.array(data[:, i], dtype=float) for i, n in enumerate(names) if n not in ("step", "point")}
    t = d["time_"]
    for amp in ("ne", "op"):
        i = d[col(f"I(Rbody_{amp})")]
        out = d[col(f"V(out_{amp})")]
        vs = d[col(f"V(vs_{amp})")]
        drv = d[col(f"V(drv_{amp})")]
        ip = d[col(f"I(Vpos_{amp})")]
        a1, b1 = t0 + 25e-6, t0 + PW - 5e-6
        a2, b2 = t0 + PW + IPG + 25e-6, t0 + 2 * PW + IPG - 5e-6
        I1, I2 = mean(t, i, a1, b1), mean(t, i, a2, b2)
        m1 = (t >= t0 + 20e-6) & (t <= t0 + 30e-6)
        m1e = (t >= b1 - 10e-6) & (t <= b1)
        sag1 = (np.mean(np.abs(i[m1])) - np.mean(np.abs(i[m1e]))) / abs(I1) * 100
        m2 = (t >= a2 - 5e-6) & (t <= a2 + 5e-6)
        m2e = (t >= b2 - 10e-6) & (t <= b2)
        sag2 = (np.mean(np.abs(i[m2])) - np.mean(np.abs(i[m2e]))) / abs(I2) * 100
        Q1 = integ(t, i, t0, t0 + PW + 10e-6)
        Q2 = integ(t, i, t0 + PW + IPG, t0 + 2 * PW + IPG + 10e-6)
        Igap = mean(t, i, t0 + PW + 15e-6, t0 + PW + IPG - 2e-6)
        Qnet = integ(t, i, t0, t0 + T - 1e-6)
        # settling of phase-1 edge: last time |i-I1| > 2% |I1| within first 100 us
        w = (t >= t0) & (t <= t0 + 100e-6)
        bad = t[w][np.abs(i[w] - I1) > 0.02 * abs(I1)]
        settle = (bad[-1] - t0) * 1e6 if len(bad) else 0.0
        over = (np.max(np.abs(i[(t >= t0) & (t <= t0 + PW)])) / abs(I1) - 1) * 100
        pw = (t >= t0) & (t <= t0 + 2 * PW + IPG + 20e-6)
        hp, hn = 14.56 - out[pw].max(), out[pw].min() + 14.68
        quiet = (t >= t0 + 5e-3) & (t <= t0 + 35e-3)
        Iq = -np.mean(ip[quiet])
        rows.append(dict(step=s, **params.get(s, {}), amp=amp, I1=I1 * 1e3, I2=I2 * 1e3, sag1=sag1, sag2=sag2,
                         Q1=Q1 * 1e9, Q2=Q2 * 1e9, Igap=Igap * 1e6, Qnet=Qnet * 1e9,
                         imb=Qnet / abs(Q1) * 100, settle=settle, over=over, head_pos=hp, head_neg=hn,
                         out_max=out[pw].max(), out_min=out[pw].min(), vs=np.mean(vs[quiet]),
                         drv=np.mean(drv[quiet]) * 1e3, Iq=Iq * 1e3))

hdr = ("amp", "I1", "I2", "sag1", "sag2", "settle", "over", "Igap", "Qnet", "imb", "out_max", "out_min",
       "head_pos", "head_neg", "vs", "drv", "Iq")
fmt = {"I1": "{:+.3f}", "I2": "{:+.3f}", "sag1": "{:.1f}", "sag2": "{:.1f}", "settle": "{:.1f}", "over": "{:.1f}",
       "Igap": "{:+.1f}", "Qnet": "{:+.2f}", "imb": "{:+.2f}", "out_max": "{:+.2f}", "out_min": "{:+.2f}",
       "head_pos": "{:.2f}", "head_neg": "{:.2f}", "vs": "{:+.3f}", "drv": "{:+.2f}", "Iq": "{:.3f}"}
pk = [k for k in rows[0] if k not in hdr and k != "step"]
print("units: I mA, sag %, settle us (to 2%), over %, Igap uA, Qnet nC, imb % of phase charge, V, drv mV, Iq mA (+rail, between pulses)")
print(" ".join(f"{p:>8}" for p in pk + ["step"]) + " " + " ".join(f"{h:>8}" for h in hdr))
for r in rows:
    print(" ".join(f"{r[p]:>8.4g}" for p in pk) + f" {r['step']:>8}" + " " +
          " ".join(f"{(fmt[h].format(r[h]) if h in fmt else r[h]):>8}" for h in hdr))
