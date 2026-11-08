# Power subsystem handoff for LTspice integration (Rev C)

Source: "Stimulator Power Architecture" page, Rev C (https://claude.ai/artifact/Li92LWxBFjbQxgQaCecG3v), written in a separate Claude session on 2026-11-07. This file is the complete spec for that page; the link is only a reference.

Status of decisions:
- Converter is the TI TPS65131 (dual ±rail), assembled with stencil + reflow at the GT HIVE. A TPS61040 alternative was considered and rejected by the team.
- Rev C corrects Rev A, which left out the TPS65131's two rectifier diodes (D1, D2) and the BSW-driven P-MOSFET load switch (Q1). Both are required.
- The stimulus stage is unchanged: improved Howland on an LT1637, driven by two DAC channels.

**Read §6 first if you are working on `Circuit layout/tvns_stim.asc`.** That schematic is the bench prototype and differs from the design this power subsystem was sized for (op-amp, DAC source, Rbleed). §6 lists the mapping and the mismatches.

## 1. Where the power subsystem meets the existing stimulator schematic

Only three interfaces touch the stimulus circuit:

| Net | Value | Connects to in the stim schematic |
|---|---|---|
| `VPOS` | +14.5 V regulated (set +14.56 V, see §3) | LT1637 V+ |
| `VNEG` | −14.5 V regulated (set −14.68 V, see §3) | LT1637 V−, and LT1637 SHDN (tied to V−; SHDN shuts the part down whenever it is ≥ V− + 1.2 V, so it must not float or go to GND) |
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
- Inverting (negative): L2 4.7 µH shielded from `LX_N` to GND. D2 MBRM140, anode `VNEG`, cathode `LX_N`. The negative side disconnects internally in shutdown; the positive side does not, which is why Q1 exists. Without Q1, VBAT leaks through L1 and D1 to `VPOS` and keeps the LT1637 powered between sessions.
- TI's reference uses 20 V MBRM120 diodes. D2 sees about VBAT + 14.5 V ≈ 19 V reverse, so the 40 V MBRM140 in the same footprint is specified instead.
- Output caps: 2 × 10 µF 50 V X7R 1206 on each rail.
- Positive feedback: VPOS = 1.213 V × (1 + R1/R2). R1 = 976 kΩ, R2 = 88.7 kΩ → +14.56 V. Feed-forward cap across R1: 6.8 pF (datasheet C9).
- Negative feedback: FBN regulates to 0 V between `VREF_TPS` and `VNEG`, so VNEG = −1.213 V × R3/R4. R3 = 1.21 MΩ (FBN to `VNEG`), R4 = 100 kΩ (`VREF_TPS` to FBN) → −14.68 V. Feed-forward cap across R3: 7.5 pF (datasheet C10). VREF pin: 0.1 µF to GND (C3).
- Remaining compensation parts (datasheet C6 10 nF, C7 4.7 nF C0G): take pin placement from TPS65131 datasheet Fig. 8-1 / Table 8-2.
- 14.5 V keeps divider tolerance under the part's 15 V recommended limit (17 V abs max). Datasheet: 300 µA typ / 500 µA max quiescent in power-save, 0.2 µA typ shutdown.

**8. Microcontroller.** U4 STM32G431KB at 3.0 V, 16 MHz in session, Standby between sessions. PA4 = DAC1_OUT1 stimulus command (into Howland R3). PA5 = DAC1_OUT2 bridge reference, mid-code (into Howland R1). GPIOs: `EN_RAIL` (through 10 kΩ), `OUT_EN`. ADC: `VBAT_SENSE`, `I_SENSE`. Inputs: `STAT`, `VBUS_SENSE`, wake button.

**9. Wake button.** 3 × 4 mm SMD tactile switch on PA0 / WKUP.

**10. Programming.** SWD header (Voltera/proto) or Tag-Connect TC2030 footprint (fab); ST-LINK from the Nucleo with its CN2 jumpers removed.

**11. Howland current source (existing).** LT1637, V+ = `VPOS`, V− = `VNEG`, SHDN = `VNEG`. R1 to R4 100 kΩ 0.05%, Rsense 100 Ω. I ≈ (V_PA4 − V_PA5) / Rsense.

**12. Output network (existing).** Rbleed 150 kΩ (DC return), Cblock 2.2 µF 50 V X7R 1206 (no net DC to skin), 60 V photoMOS in series, open unless firmware asserts `OUT_EN` after the rails settle.

**13. Electrodes.** Type BF applied part. Bench mock body: each electrode 10 kΩ ∥ 470 nF, body 500 Ω.

**14. Optional current sense.** 10 Ω 0.1% in the return leg, 10 kΩ / 10 kΩ offset divider into an ADC or comparator input for over-current trip.

## 4. Operating states (for transient sequencing)

| State | `EN_RAIL` | Rails | PhotoMOS | Battery draw |
|---|---|---|---|---|
| Standby | Low | Off; Q1 open, so `VPOS` ≈ 0 V and `VNEG` ≈ 0 V | Open | About 10 µA |
| Session | High (MCU) | On after soft start | Closed once rails settle | 36 mW nominal, 53 mW worst case |
| Charging (USB in) | Forced low by Q2 | Off | Open | Charger supplies ~213 mA to the cell |

Session sequence: assert `EN_RAIL` → wait for both rails to settle → assert `OUT_EN` → start DAC waveform (250 µs phases, 50 µs interphase gap, 25 Hz baseline, 5 mA). Ending: stop waveform → release `OUT_EN` → release `EN_RAIL`. This sequence is how the design meets IEC 60601-2-10's "output blocked at power-up".

## 5. Modeling notes for LTspice

These are suggestions, not decisions; check model availability before relying on them.
- LTspice's built-in library is Analog Devices parts. As far as I know it does not include the TPS65131, MCP73831 or MCP1700. If TI offers a TPS65131 SPICE model it would be on the product page; unverified. Absent a model, represent block 7 behaviorally: `VPOS` and `VNEG` as voltage sources with a few hundred mΩ series resistance into 20 µF per rail, enabled by a PWL or behavioral ramp from `EN_RAIL` (soft-start time from the datasheet).
- The cell can be a 3.7 V source with roughly 0.15 Ω series resistance (assumed; sweep 3.3 to 4.2 V). The LDO can be an ideal 3.0 V source. USB, charger and interlock do not touch the stimulus path and can be omitted from the stimulus sim, or modeled as a voltage-controlled switch on `EN_RAIL` if the interlock behavior should be shown.
- Useful checks once integrated:
  - Compliance at ±14.5 V on the mock body. The first phase needs about 1.63 kΩ × I (confirmed in a separate simulation). With about 1 V of LT1637 output headroom (assumed, not checked against the datasheet) plus the I × 100 Ω Rsense drop, expect the output to hold up to roughly 7.5 mA and clip above that.
  - Rail droop per pulse: 5 mA × 250 µs / 20 µF ≈ 63 mV.
  - Startup: output current stays zero until `OUT_EN`, even while the rails ramp.
  - Current is insensitive to rail value: sweep `VPOS`/`VNEG` by ±10% and confirm the delivered current changes far less than the ±20% accuracy limit.

## 6. Mapping onto `Circuit layout/tvns_stim.asc`, and mismatches to resolve

Read on 2026-11-07. Note that the .asc is not UTF-8 (the µ in `Cblock` and `Ci` values is a legacy-encoded byte), so edit it in a way that preserves its encoding.

Mapping:
- Rail nets in the .asc are the flags `+V` and `-V`, driven by ideal sources `Vpos` = `{Vrail}` and `Vneg` = `{-Vrail}`, with `.param Vrail=9` and `.step param Vrail list 9 18`. `VPOS` → `+V`, `VNEG` → `-V`. Minimal integration: set Vrail to 14.5 (or use +14.56 / −14.68 V asymmetric), then replace each ideal source with the §5 behavioral model (series R, 2 × 10 µF, enable ramp).
- Both op-amp instances (U1 Howland, U2 DC servo) already take `+V`/`-V`, so the servo is also powered from the TPS65131 rails.
- Stimulus drive: the .asc uses V1 + V2 stacked PULSE sources on `vin`, with R1's far end grounded. The power design instead drives R3 from PA4 and R1 from PA5 (mid-code reference) on an STM32G431. The bench note says `Vin = GPIO25 - GPIO26`, i.e. ESP32 DACs. The power subsystem does not care which, but the 3.0 V LDO and battery budget were sized for the STM32G431.
- Output enable: there is no photoMOS in the .asc. If startup blocking should be shown, add a voltage-controlled switch (e.g. `SW` with Ron ≈ 20 Ω, Roff ≈ 1 GΩ) in series between `drv` and `Cblock`, controlled by an `OUT_EN` PWL that asserts after the rails settle.

Mismatches (decide with the team; do not silently pick):
1. **Op-amp.** The .asc labels the Howland and servo as NE5532 halves (modeled as UniversalOpAmp2, Rail=1). The power budget (36 mW nominal / 53 mW worst case, ≥50 sessions on 500 mAh) assumed an LT1637 at about 0.23 to 0.37 mA. As far as I know the NE5532 draws about 8 mA typical (16 mA max) for both halves. At ±14.5 V that is roughly 230 mW from the rails, which would cut the 500 mAh cell to roughly a dozen 20-minute sessions instead of 50+. The NE5532 also has no SHDN pin (the §1 SHDN note applies only to the LT1637). Its ±22 V maximum supply is fine at ±14.5 V.
2. **Rbleed.** The .asc has 100 kΩ; the power page and bench build use 150 kΩ.
3. **Howland resistors.** The .asc uses Rset = 150 kΩ with R2 = Rset + Rsense; the final-PCB parts list uses 100 kΩ 0.05%. The power subsystem does not constrain this.
4. **Rails.** The .asc steps ±9 V and ±18 V. The TPS65131 gives ±14.5 V (15 V recommended maximum), so the ±18 V step is not reachable with this converter.
