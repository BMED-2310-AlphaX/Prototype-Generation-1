# OPA2196 vs NE5532 in the next-gen stimulator (simulated 2026-11-09)

This folder holds the LTspice comparison that led to replacing the NE5532 with the TI OPA2196 (U6; ordered as OPA2196IDGKR, VSSOP-8, same die as the SOIC OPA2196IDR) in `alphax_tvns_ble/STM32G431kbt6`. The 1N4148W servo clamps (D1/D2) were removed at the same time.

## What was simulated

Two copies of the as-built KiCad stimulator, one per op amp, share the same DAC stimulus. Each copy drives its own body model.

| Item | Value |
|---|---|
| Rails | +14.56 / −14.68 V (TPS65131 set points) |
| Howland | R1–R4 150 kΩ, Rsense 100 Ω, so I = (V_PA4 − V_PA5) × 10.0 mA/V |
| Output network | Rbleed 150 kΩ, Cblock 2.2 µF, photoMOS modeled as 50 Ω (TLP175A max on-resistance) |
| Servo | Ri/Rp 1 MΩ, Ci 1 µF, Rinj/Rinj2 10 MΩ; no D1/D2 |
| DACs | DAC_REF held at 1.5 V; DAC_STIM = 1.5 V + biphasic pulse |
| Waveform | 250 µs phases, 50 µs interphase gap, 25 Hz; the 5th pulse is analysed |
| Body | each electrode Rskin ∥ 470 nF, 500 Ω between them |
| OPA2196 model | TI PSpice model SBOMAF9E (`OPAx196.LIB`) |
| NE5532 model | LTspice UniversalOpAmp2: 10 MHz, 9 V/µs, 2 V headroom, 0.5 mV Vos (TI publishes no NE5532 model) |

## Results

**Amplitude sweep (Rskin 10 kΩ, nominal parts)**

| Set | Op amp | Phase 1 / 2 (mA) | Phase-1 sag | Net imbalance | Output peak (headroom to + rail) | Settling to 2% |
|---|---|---|---|---|---|---|
| 2 mA | NE5532 | 1.97 / −2.00 | 1.1% | −0.08% | +3.5 V (11.0 V) | 1.2 µs |
| | OPA2196 | 1.97 / −1.99 | 1.1% | −0.08% | +3.5 V (11.0 V) | 5.5 µs |
| 5 mA | NE5532 | 4.93 / −4.99 | 1.1% | −0.08% | +8.9 V (5.7 V) | 1.3 µs |
| | OPA2196 | 4.92 / −4.98 | 1.1% | −0.08% | +8.8 V (5.8 V) | 5.5 µs |
| 7.5 mA | NE5532 | 7.34 / −7.48 | 11.6% | −0.06% | +12.4 V (2.2 V) | 1.2 µs |
| | OPA2196 | 7.38 / −7.47 | 1.1% | −0.08% | +13.2 V (1.35 V) | 5.6 µs |
| 10 mA | NE5532 | 8.31 / −9.95 | 63% | +1.7% | clipped | — |
| | OPA2196 | 9.20 / −9.94 | 39% | +0.7% | clipped | — |

**Tolerance corners (5 mA):** all four bridge resistors pushed ±0.05% in the worst direction, at Rskin 10 kΩ and 50 kΩ.
- Both op amps are stable in every corner, with imbalance between −0.16% and 0.00%.
- Servo output: OPA2196 −0.14 to +0.06 V; NE5532 −0.20 to 0.00 V.
- No latch-up without D1/D2.

**Supply current:** the TI model draws 0.28 mA for both amplifiers. The NE5532 datasheet gives 8 mA typ (the generic model does not capture this).

**Not op-amp related:** about 1.2% of the phase current flows during the interphase gap. This is the electrode voltage discharging through Rbleed, and it returns in the tail, so net imbalance stays at −0.08%.

**Not simulated:** rail/OUT_EN start-up sequencing and electrode lead-off.

## Re-running

1. Download TI's OPAx196 PSpice model (https://www.ti.com/lit/zip/SBOMAF9) and put `OPAx196.LIB` in this folder. It is not committed.
2. `python make_netlists.py` writes `cmp_amplitude.cir` and `cmp_corners.cir`.
3. Open each `.cir` in LTspice and run. They take about 4 and 10 minutes.
4. Export the signals `I(RBODY_*)`, `V(out_*)`, `V(vs_*)`, `V(drv_*)` and `I(VPOS_*)` from t ≥ 0.16 s to SQLite, then run `python analyze.py <db> 4`.
