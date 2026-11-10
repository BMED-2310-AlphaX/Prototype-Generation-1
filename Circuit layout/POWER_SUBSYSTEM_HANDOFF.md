# Power subsystem handoff for LTspice integration (Rev C)

Source: "Stimulator Power Architecture" page, Rev C (https://claude.ai/artifact/Li92LWxBFjbQxgQaCecG3v), written in a separate Claude session on 2026-11-07. This file is the complete spec for that page; the link is only a reference.

Status of decisions:
- Converter is the TI TPS65131 (dual ±rail), assembled with stencil + reflow at the GT HIVE. A TPS61040 alternative was considered and rejected by the team.
- Rev C corrects Rev A, which left out the TPS65131's two rectifier diodes (D1, D2) and the BSW-driven P-MOSFET load switch (Q1). Both are required.
- The stimulus stage is an improved Howland current pump plus DC servo on one **TI OPA2196IDGKR** dual op amp (VSSOP-8) (U6A Howland, U6B servo), driven by two DAC channels. Decided 2026-11-09; this replaces the LT1637 assumed in Rev C and the NE5532 used on the bench. See §6, item 1.

**Authoritative design (2026-11-09):** the KiCad schematic `alphax_tvns_ble/STM32G431kbt6/STM32G431kbt6.kicad_sch` is now the reference. Where this file and the schematic disagree, the schematic wins. Known differences in the power blocks are listed under block 7 and in §6. The main ones:
- TPS65131 VREF cap is 220 nF and the VIN-pin filter is 100 Ω + 100 nF (per TI Fig. 8-1).
- Parts changed: Q1 AO3401A, L1/L2 TDK TFM252012ALMA4R7MTAA, photoMOS Toshiba TLP175A.
- Added after the 2026-11-09 design review:
  - F1 is now MF-NSMF110-2 (1.1 A hold, 0.06–0.20 Ω, against 0.15–0.75 Ω for the MF-NSMF050).
  - C34/C35 (2 × 10 µF 50 V 1206) are VBAT bulk next to U3. Together with F1 they keep VBAT above the TPS65131 UVLO (2.1–2.7 V) during the combined soft-start of both converters.
  - D5 SMF18CA bidirectional TVS at the electrode pad J4.
  - Status LED D6 (LTST-C191KGKT) with R34 390 Ω on PA3.

**Read §6 first if you are working on `Circuit layout/tvns_stim.asc`.** That schematic is the bench prototype and differs from the design this power subsystem was sized for (op-amp, DAC source, Rbleed). §6 lists the mapping and the mismatches.

## 1. Where the power subsystem meets the existing stimulator schematic

Only three interfaces touch the stimulus circuit:

| Net | Value | Connects to in the stim schematic |
|---|---|---|
| `VPOS` | +14.5 V regulated (set +14.56 V, see §3) | OPA2196 V+ (U6 pin 8), 100 nF at the pin |
| `VNEG` | −14.5 V regulated (set −14.68 V, see §3) | OPA2196 V− (U6 pin 4), 100 nF at the pin. The OPA2196 has no shutdown pin; it is off whenever the rails are off. |
| `VDD_3V0` | 3.0 V | MCU supply. In the sim, the two DAC sources (PA4 → R3, PA5 → R1) are 0 to 3.0 V sources referenced to GND |

Control signals from the MCU: `EN_RAIL` (both ENP and ENN of the TPS65131) and `OUT_EN` (photoMOS LED in the output network). Nothing in the stimulus path runs from VBAT directly. All grounds are one net at the cell's negative terminal.

## 2. Net list

| Net | Description |
|---|---|
| `VBUS` | USB-C 5 V, present only while charging |
| `VBAT` | Cell, 3.0 to 4.2 V (3.7 V nominal) |
| `VBAT_SW` | VBAT after Q1, feeds the boost inductor L1 only |
| `LX_P` | TPS65131 boost switch node (L1 to D1 anode) |
| `LX_N` | TPS65131 inverting switch node (L2 top, D2 cathode) |
| `VPOS`, `VNEG` | ±14.5 V rails |
| `VREF_TPS` | TPS65131 reference pin, 0.1 µF to GND, top of the negative feedback divider |
| `VDD_3V0` | LDO output to MCU |
| `EN_RAIL` | ENP + ENN, driven by the MCU through 10 kΩ, pulled low by the interlock |
| `OUT_EN` | MCU GPIO to the photoMOS LED |
| `VBAT_SENSE`, `VBUS_SENSE`, `STAT`, `I_SENSE` | MCU inputs |

## 3. Components by block (numbers match the page)

**1. USB-C input.** J1 GCT USB4105-GF-A. CC1 and CC2 each have 5.1 kΩ to GND (makes chargers supply 5 V). Optional USBLC6-2SC6 ESD only if the data lines are used. Output: `VBUS`.

**2. Charger.** U1 MCP73831T-2ACI/OT. VDD = `VBUS`, VBAT pin = `VBAT`, PROG has 4.7 kΩ to GND (I_charge = 1000 V / R_PROG = 213 mA, about 0.43C), 4.7 µF on VDD and on VBAT. STAT goes to an MCU GPIO with a pull-up.

**3. Cell.** BT1 500 mAh LiPo (Adafruit 1578, 29 × 36 × 4.75 mm, built-in protection that cuts at 3.0 V and on short circuit), JST S2B-PH-SM4-TB connector, optional 0.5 A PTC in series. Sized for ≥50 twenty-minute sessions at worst-case draw.

**4. Charging interlock.** Q2 BSS138. Gate from `VBUS` through 10 kΩ, 100 kΩ gate to GND, source to GND, drain on `EN_RAIL`. While USB is present, Q2 holds ENP/ENN low, so the rails cannot start and the patient is never connected through a mains charger. The MCU drives `EN_RAIL` through a 10 kΩ series resistor so Q2 can override it without contention. Since BSW follows ENP, the interlock also opens Q1. Separate 100 kΩ / 100 kΩ divider from `VBUS` to `VBUS_SENSE`.

**5. Battery sense.** 1 MΩ / 1 MΩ divider from `VBAT` to GND, midpoint `VBAT_SENSE` with 100 nF to GND. About 1.9 µA standby drain. Firmware refuses to start a session below 3.45 V and ends one at 3.3 V.

**6. MCU supply.** U2 MCP1700T-3002E/TT, VIN = `VBAT`, VOUT = `VDD_3V0`, 1 µF in and out, 1.6 µA quiescent.

**7. Dual-rail converter.** U3 TPS65131RGER (VQFN-24, 4 × 4 mm, 0.5 mm pitch, exposed pad soldered to the ground pour).
- VIN and VINA on `VBAT` (the IC itself draws 0.2 µA typ in shutdown). Input caps 2 × 4.7 µF (datasheet C1, C2). R7 100 Ω + C8 220 nF filter on VINA per datasheet Fig. 8-1.
- ENP and ENN both on `EN_RAIL`. PSP and PSN tied to VIN (power-save mode on).
- Boost (positive): Q1 P-MOSFET Si2323DS between `VBAT` and `VBAT_SW`, gate on BSW (BSW goes low when ENP is high, turning Q1 on). L1 4.7 µH shielded (datasheet: EPCOS B82462-G4472) from `VBAT_SW` to `LX_P`. D1 MBRM140 (1 A, 40 V Schottky), anode `LX_P`, cathode `VPOS`.
- Inverting (negative): L2 4.7 µH shielded from `LX_N` to GND. D2 MBRM140, anode `VNEG`, cathode `LX_N`. The negative side disconnects internally in shutdown; the positive side does not, which is why Q1 exists. Without Q1, VBAT leaks through L1 and D1 to `VPOS` and keeps the op amp powered between sessions.
- TI's reference uses 20 V MBRM120 diodes. D2 sees about VBAT + 14.5 V ≈ 19 V reverse, so the 40 V MBRM140 in the same footprint is specified instead.
- Output caps: 2 × 10 µF 50 V X7R 1206 on each rail.
- Positive feedback: VPOS = 1.213 V × (1 + R1/R2). R1 = 976 kΩ, R2 = 88.7 kΩ → +14.56 V. Feed-forward cap across R1: 6.8 pF (datasheet C9).
- Negative feedback: FBN regulates to 0 V between `VREF_TPS` and `VNEG`, so VNEG = −1.213 V × R3/R4. R3 = 1.21 MΩ (FBN to `VNEG`), R4 = 100 kΩ (`VREF_TPS` to FBN) → −14.68 V. Feed-forward cap across R3: 7.5 pF (datasheet C10). VREF pin: 0.1 µF to GND (C3).
- Remaining compensation parts (datasheet C6 10 nF, C7 4.7 nF C0G): take pin placement from TPS65131 datasheet Fig. 8-1 / Table 8-2.
- 14.5 V keeps divider tolerance under the part's 15 V recommended limit (17 V abs max). Datasheet: 300 µA typ / 500 µA max quiescent in power-save, 0.2 µA typ shutdown.

**8. Microcontroller.** U4 STM32G431KB at 3.0 V, 16 MHz in session, Standby between sessions. PA4 = DAC1_OUT1 stimulus command (into Howland R3). PA5 = DAC1_OUT2 bridge reference, mid-code (into Howland R1). GPIOs: `EN_RAIL` (through 10 kΩ), `OUT_EN`. ADC: `VBAT_SENSE`, `I_SENSE`. Inputs: `STAT`, `VBUS_SENSE`, wake button.

**9. Wake button.** 3 × 4 mm SMD tactile switch on PA0 / WKUP.

**10. Programming.** SWD header (Voltera/proto) or Tag-Connect TC2030 footprint (fab); ST-LINK from the Nucleo with its CN2 jumpers removed.

**11. Howland current source + DC servo.** U6 TI OPA2196IDGKR (VSSOP-8, 3 × 3 mm, 0.65 mm pitch; chosen over the SOIC-8 OPA2196IDR for stock. Same die and pinout). A = Howland, B = DC servo.
- **Supply:** V+ = `VPOS`, V− = `VNEG`. Quiescent current 0.28 mA typ, 0.40 mA max for both amplifiers.
- **Howland:** R11–R14 150 kΩ 0.05% (Susumu RG), Rsense 100 Ω 0.1%. I = (V_PA4 − V_PA5) × (1/Rsense + 1/150k) ≈ 10.0 mA/V.
- **Servo:** Ri/Rp 1 MΩ, Ci 1 µF, Rinj/Rinj2 10 MΩ.
- **No input clamps:** the OPA2196 has rail-to-rail inputs, no phase reversal, and tolerates differential input up to the supply. The 1N4148W clamps (D1/D2) that the NE5532 bench build needed were removed on 2026-11-09.

**12. Output network (existing).** Rbleed 150 kΩ (DC return), Cblock 2.2 µF 50 V X7R 1206 (no net DC to skin), 60 V photoMOS in series, open unless firmware asserts `OUT_EN` after the rails settle.

**13. Electrodes.** Type BF applied part. Bench mock body: each electrode 10 kΩ ∥ 470 nF, body 500 Ω.

**14. Optional current sense.** 10 Ω 0.1% in the return leg, 10 kΩ / 10 kΩ offset divider into an ADC or comparator input for over-current trip.

## 4. Operating states (for transient sequencing)

| State | `EN_RAIL` | Rails | PhotoMOS | Battery draw |
|---|---|---|---|---|
| Standby | Low | Off; Q1 open, so `VPOS` ≈ 0 V and `VNEG` ≈ 0 V | Open | About 10 µA |
| Session | High (MCU) | On after soft start | Closed once rails settle | 36 mW nominal, 53 mW worst case (sized for 0.23–0.37 mA of op-amp supply current; the OPA2196 is 0.28 typ / 0.40 max, so the budget holds) |
| Charging (USB in) | Forced low by Q2 | Off | Open | Charger supplies ~213 mA to the cell |

Session sequence: assert `EN_RAIL` → wait for both rails to settle → assert `OUT_EN` → start DAC waveform (250 µs phases, 50 µs interphase gap, 25 Hz baseline, 5 mA). Ending: stop waveform → release `OUT_EN` → release `EN_RAIL`. This sequence is how the design meets IEC 60601-2-10's "output blocked at power-up".

## 5. Modeling notes for LTspice

These are suggestions, not decisions; check model availability before relying on them.
- LTspice's built-in library is Analog Devices parts. As far as I know it does not include the TPS65131, MCP73831 or MCP1700. If TI offers a TPS65131 SPICE model it would be on the product page; unverified. Absent a model, represent block 7 behaviorally: `VPOS` and `VNEG` as voltage sources with a few hundred mΩ series resistance into 20 µF per rail, enabled by a PWL or behavioral ramp from `EN_RAIL` (soft-start time from the datasheet).
- The cell can be a 3.7 V source with roughly 0.15 Ω series resistance (assumed; sweep 3.3 to 4.2 V). The LDO can be an ideal 3.0 V source. USB, charger and interlock do not touch the stimulus path and can be omitted from the stimulus sim, or modeled as a voltage-controlled switch on `EN_RAIL` if the interlock behavior should be shown.
- Useful checks once integrated:
  - Compliance at ±14.5 V on the mock body. The first phase needs about 1.63 kΩ × I. **Simulated 2026-11-09 with TI's OPA2196 model** (see `Circuit layout/sim_opa2196/`):
    - **To 7.5 mA:** clean, with the op amp output within 1.35 V of the + rail. Phase sag is 1.1%, interphase-gap current about 1.2% (it flows through Rbleed), and net charge imbalance −0.08%.
    - **At 10 mA:** phase 1 clips (39% sag).
    - **Bridge tolerance corners (±0.05%):** stable, with imbalance between −0.16% and 0.00%.
  - Rail droop per pulse: 5 mA × 250 µs / 20 µF ≈ 63 mV.
  - Startup: output current stays zero until `OUT_EN`, even while the rails ramp.
  - Current is insensitive to rail value: sweep `VPOS`/`VNEG` by ±10% and confirm the delivered current changes far less than the ±20% accuracy limit.

## 5b. Recorded design decisions and limits (2026-11-09 review)

These were reviewed and accepted. They are not open issues.

1. **Firmware-fault current is not limited in hardware.** Rsense stays 100 Ω. The buffered DAC spans 0.2–2.8 V, so a firmware fault that drives DAC_STIM and DAC_REF to opposite extremes commands about ±26 mA. Into a low-impedance load, the ±14.5 V rails can deliver it. Normal firmware keeps DAC_REF at mid-scale (±13 mA reachable), limits amplitude in software, and uses the watchdog. Raising Rsense would cap this in hardware; it was declined to keep resolution and compliance.
2. **Compliance depends on the real electrode impedance.** At ±14.6 V rails, 5 mA stays clean only into an effective load below about 2.4 kΩ. The mock body needs ~1.63 kΩ × I. Measure the ear-electrode impedance at 250 µs pulses before relying on 5 mA or more.
3. **There is no power path.** Without a cell, the MCP73831 output cannot run the board reliably. Always program and debug with the cell connected. With USB present, keep system load below the charger's termination current (~11–16 mA at R_PROG 4.7 kΩ), or the MCP73831 never terminates. It has no safety timer.
4. **Rails may sit slightly above the TPS65131's ±15 V recommended maximum** on worst-case parts: about +15.1 / −15.4 V from 3% total accuracy and the ±24 mV FBN offset. This is well under the 17 V absolute maximum. Accepted. If wanted, R22 → 931 kΩ and R24 → 1.15 MΩ give ±14.0 V nominal at a cost of about 0.5 V of compliance.

## 6. Mapping onto `Circuit layout/tvns_stim.asc`, and mismatches to resolve

Read on 2026-11-07. Note that the .asc is not UTF-8 (the µ in `Cblock` and `Ci` values is a legacy-encoded byte), so edit it in a way that preserves its encoding.

Mapping:
- Rail nets in the .asc are the flags `+V` and `-V`, driven by ideal sources `Vpos` = `{Vrail}` and `Vneg` = `{-Vrail}`, with `.param Vrail=9` and `.step param Vrail list 9 18`. `VPOS` → `+V`, `VNEG` → `-V`. Minimal integration: set Vrail to 14.5 (or use +14.56 / −14.68 V asymmetric), then replace each ideal source with the §5 behavioral model (series R, 2 × 10 µF, enable ramp).
- Both op-amp instances (U1 Howland, U2 DC servo) already take `+V`/`-V`, so the servo is also powered from the TPS65131 rails.
- Stimulus drive: the .asc uses V1 + V2 stacked PULSE sources on `vin`, with R1's far end grounded. The power design instead drives R3 from PA4 and R1 from PA5 (mid-code reference) on an STM32G431. The bench note says `Vin = GPIO25 - GPIO26`, i.e. ESP32 DACs. The power subsystem does not care which, but the 3.0 V LDO and battery budget were sized for the STM32G431.
- Output enable: there is no photoMOS in the .asc. If startup blocking should be shown, add a voltage-controlled switch (e.g. `SW` with Ron ≈ 20 Ω, Roff ≈ 1 GΩ) in series between `drv` and `Cblock`, controlled by an `OUT_EN` PWL that asserts after the rails settle.

Mismatches. All four were resolved by 2026-11-09; the original notes are kept below each resolution.
1. **Op-amp. Resolved:** OPA2196IDGKR (VSSOP-8).
   - **Supply current:** 0.28 mA typ for both amplifiers, so the original power budget holds.
   - **Compliance:** about 0.8 V more than the NE5532. Simulation: clean to 7.5 mA, where the NE5532 sags 12% in phase 1.
   - **Clamps:** none needed (D1/D2 removed).
   - **Unchanged:** delivered charge, balance and stability match the NE5532 in simulation.

   The bench `.asc` still models the NE5532 bench build. Original note: The .asc labels the Howland and servo as NE5532 halves (modeled as UniversalOpAmp2, Rail=1). The power budget (36 mW nominal / 53 mW worst case, ≥50 sessions on 500 mAh) assumed an LT1637 at about 0.23 to 0.37 mA. As far as I know the NE5532 draws about 8 mA typical (16 mA max) for both halves. At ±14.5 V that is roughly 230 mW from the rails, which would cut the 500 mAh cell to roughly a dozen 20-minute sessions instead of 50+. The NE5532 also has no SHDN pin (the §1 SHDN note applies only to the LT1637). Its ±22 V maximum supply is fine at ±14.5 V.
2. **Rbleed. Resolved:** 150 kΩ 1% (Panasonic ERJ-3EKF1503V; Rbleed needs no precision). Original note: the .asc has 100 kΩ; the power page and bench build use 150 kΩ.
3. **Howland resistors. Resolved:** 4 × 150 kΩ 0.05%. R12 is 150 kΩ, not the ideal 150.1 kΩ, which puts the bridge slightly low, the safe side. Original note: the .asc uses Rset = 150 kΩ with R2 = Rset + Rsense; the final-PCB parts list uses 100 kΩ 0.05%.
4. **Rails. Resolved:** ±14.56 / −14.68 V from the TPS65131. Original note: the .asc steps ±9 V and ±18 V, and the ±18 V step is not reachable with this converter.
