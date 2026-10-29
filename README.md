# AlphaX — Prototype Generation 1

Non-pharmacological anxiety management device. BMED 2310 Junior Design, Team AlphaX.

An on-demand neurostimulation device with adjustable treatment intensity,
controlled from a mobile app over Bluetooth Low Energy.

## Why adjustable intensity

PRN medication commits the user to a dose chosen before they know how bad an
episode will be. Onset is delayed, the dose can't be reduced once taken, and
repeated use carries dependency risk.

AlphaX inverts that. The user starts low, feels the effect in seconds, and
adjusts either direction continuously. A dose that's too strong is corrected in
1.5 seconds instead of metabolized over hours.

## Repository layout

```
ios-app/        Swift / SwiftUI app — 13 source files
  AlphaX/       drag these into a new Xcode iOS App project
  SETUP.md      step-by-step build and test guide

firmware/
  esp32_standalone/        ESP32 alone — start here
  esp32_howland_dctest/    DC bench test into a dummy load
  esp32_ble_bridge/        ESP32 as radio, paired with the STM32
  stm32_session_controller/ Nucleo-F303RE session logic
  uno_bench_demo/          Arduino Uno, no radio
  nano_session_controller/ Arduino Nano, no radio

bridge/         USB fallback — browser UI when no BLE radio is available
docs/
```

## Quick start

**App:** create a new Xcode iOS App project (SwiftUI, Storage: None), drag in
the 13 files from `ios-app/AlphaX/`, add the `Privacy - Bluetooth Always Usage
Description` key under the target's Info tab, set deployment target to iOS
16.6, and run on a physical iPhone. The Simulator has no Bluetooth radio.

**Firmware:** flash `firmware/esp32_standalone/` to an ESP32 DevKit V1 using
the *esp32 by Espressif Systems* board package. Serial Monitor at 115200
should print `Advertising as AlphaX-01`.

Full instructions: [`ios-app/SETUP.md`](ios-app/SETUP.md)

## Architecture

```
  iPhone  ──BLE──►  ESP32 / STM32  ──►  Howland current pump  ──►  electrodes
   app                 firmware              analog stage
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
 "bpm":72.4,"rmssd":41.2,"batt":255}
```

Commands are plain text, so the firmware can be driven from a serial terminal
with no app involved.

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
| Analog stage | simulated (LTspice), bench build in progress |
| HRV sensing | implemented in firmware, no sensor yet |

## Safety

- Intensity capped at 80%, duration at 60 minutes — enforced in firmware
- 1.5 s ramps in both directions
- Output stops automatically if the Bluetooth link drops
- All output flows through a single function, `applyOutput()`

**Software limits are not a current limit.** Anything that passes current
through a person requires hardware current limiting, isolation, and instructor
review. The DC bench-test firmware is for resistive dummy loads only.
