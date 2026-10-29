# AlphaX — Prototype Generation 1

Non-pharmacological anxiety management device. BMED 2310 Junior Design, Team AlphaX.

Transcutaneous vagus nerve stimulation with adjustable treatment intensity,
controlled from a mobile app over Bluetooth Low Energy.

## Why adjustable intensity

PRN medication commits the user to a dose chosen before they know how bad an
episode will be. Onset is delayed, the dose can't be reduced once taken, and
repeated use carries dependency risk.

AlphaX inverts that. The user starts low, feels the effect in seconds, and
adjusts either direction continuously. A dose that's too strong is corrected in
about a second instead of metabolized over hours.

## Repository layout

```
ios-app/
  AlphaX/        13 Swift source files — drag into a new Xcode iOS App project
  SETUP.md       step-by-step build and test guide

firmware/
  alphax_tvns_ble/     MAIN BUILD — stim engine + Bluetooth control
  esp32_standalone/    Bluetooth only, no stimulation (bring-up / demo)
  esp32_howland_dctest/DC bench test into a dummy load
  esp32_ble_bridge/    ESP32 as radio, paired with the STM32
  stm32_session_controller/  Nucleo-F303RE session logic
  uno_bench_demo/      Arduino Uno, no radio
  nano_session_controller/   Arduino Nano, no radio

bridge/          USB fallback — browser UI when no BLE radio is available
docs/
```

Circuit files (LTspice, STM32 CubeIDE project) live in their own folders at the
repository root.

## Main build

`firmware/alphax_tvns_ble/` merges two pieces of work:

| File | Author |
|---|---|
| `stim.cpp`, `stim.h` | Max — biphasic stimulation engine, unchanged |
| `alphax_tvns_ble.ino` | Joy — BLE service and session logic |
| `WIRING.txt` | Max |

The stimulation engine owns the waveform, amplitude ramp, parameter limits,
e-stop and timer watchdog. The BLE layer adds a radio and a session clock on
top. Nothing in the BLE layer touches the DAC directly — every output change
goes through `stim_set()` / `stim_start()` / `stim_stop()`, so the engine's
safety checks cannot be bypassed by a command from the phone.

Put all three files in one folder named `alphax_tvns_ble` (Arduino requires the
folder name to match the `.ino`), board **DOIT ESP32 DEVKIT V1**, core
**esp32 by Espressif 3.x**.

## Stimulation parameters

| | |
|---|---|
| Waveform | biphasic, charge-balanced |
| Amplitude | 5 mA at the app's 80% ceiling |
| Pulse width | 250 µs per phase |
| Interphase gap | 50 µs |
| Frequency | 25 Hz (40 ms period, 39.45 ms rest) |
| Resolution | 8-bit DAC, ~129 µA per step |

Simulated charge imbalance is 0.06% of one phase's charge at 5 mA.

## Wiring

| ESP32 | Goes to |
|---|---|
| GPIO25 (DAC1) | free end of R3 — Howland `vin` |
| GPIO26 (DAC2) | free end of R1, **lifted off ground** — Howland `vref` |
| GND | battery midpoint |
| 100 kΩ | across GPIO25 and GPIO26 |
| GPIO27 | analog-side enable, optional |

Driving R1 instead of grounding it makes the Howland input differential, so
`Iout ∝ (GPIO25 − GPIO26)`. That produces a bipolar command from two unipolar
DACs with no level-shifting stage.

**Never connect ±9 V to the ESP32.** Power it from USB. Order: USB in and
booted → batteries on → `on`. Shut down in reverse.

## Architecture

```
  iPhone  ──BLE──►  ESP32  ──DAC──►  Howland current pump  ──►  electrodes
   app              firmware            analog stage
```

**The device owns all session state and every safety limit.** The phone issues
commands and displays status; it never runs the session clock. Closing the app
mid-session doesn't interrupt treatment, and a crashed or disconnected phone
cannot produce an unsafe output.

## BLE protocol

Service `6E5A0001-B5A3-F393-E0A9-E50E24DCCA9E`

| Characteristic | UUID | Direction | Payload |
|---|---|---|---|
| Command | `…0002` | write | `START:<intensity>:<minutes>` · `STOP` · `SET:<intensity>` |
| Status | `…0003` | notify | JSON at 2 Hz |

```json
{"state":"running","intensity":40,"target":40,"remaining":1183,
 "bpm":0.0,"rmssd":0.0,"batt":255,"amp_uA":2452}
```

`amp_uA` is measured amplitude after ramp and DAC rounding, not the commanded
value. Commands are plain text, so the firmware can be driven from a serial
terminal with no app involved.

`BLEDevice::setMTU(247)` must be called before `BLEDevice::init()` — the
23-byte default truncates the telemetry packet.

## Two interfaces

Patients see a breathing pacer and session controls. Heart rate and HRV are
restricted to a clinician view reached by a non-obvious gesture.

This is deliberate. Evidence on wearable heart-rate monitoring in anxious
populations indicates that continuous cardiac display can reinforce the
symptom-monitoring behaviour the device is meant to interrupt.

## Status

| | |
|---|---|
| iOS app | complete, verified on hardware |
| BLE link | firmware written, bring-up in progress |
| Stimulation engine | written, simulated, bench test pending |
| Analog stage | built, waveform verified on scope |
| HRV sensing | implemented in firmware, no sensor yet |

## Known issues

- **Compliance margin at 5 mA.** Simulation gives `Vmax` = 8.14 V against ±9 V
  rails. Test at 2 mA first and watch for current droop at the pulse tail.
- **Current leakage** identified in Thread 1 bench testing; bleed resistor to
  ground before electrode 1 is the proposed fix.
- **Design Inputs Worksheet** still specifies a cranial electrotherapy
  predicate (Alpha-Stim AID, 0–500 µA, 0.5 Hz, ear clips). The built device is
  tVNS at 5 mA, 25 Hz, neck electrodes. EA-2 current density and EA-3 timing
  need redoing against the actual parameters.

## Safety

- Amplitude and duration limits enforced in firmware, not in the app
- Amplitude ramps rather than stepping
- Output stops automatically if the Bluetooth link drops
- BOOT button is a hardware e-stop
- Task watchdog and stimulation-timer watchdog both force output to zero on stall

**Test into a resistive dummy load first:**

```
drv ──[ 10k ∥ 470nF ]──[ 500Ω ]──[ 10k ∥ 470nF ]── GND
```

Scope the current through the 500 Ω and confirm charge balance before any
electrode touches skin. Instructor review required before human contact.
