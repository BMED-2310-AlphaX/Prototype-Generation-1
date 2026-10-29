# AlphaX — setup and test walkthrough

Follow the stages in order. Each one ends with a **checkpoint**. Don't move to
the next stage until the checkpoint passes — that's what keeps a wiring fault
from looking like a software bug.

**First time through: about 45 minutes, most of it downloads.**

### What you need

| Now | Later (circuit stage only) |
|---|---|
| Mac with Xcode (free, App Store) | LT1637 perfboard |
| iPhone + its cable | 9 V battery |
| ESP32 DevKit + micro-USB cable | haptic transducer |
| Arduino IDE (free) | multimeter or scope |
| *optional:* LED + 330 Ω resistor | |

Stages 1–5 need nothing from the right-hand column.

---

# Stage 0 — Laptop only

No board, no cables, not even a phone? You can still finish most of the setup
and catch most of the bugs. **Both halves of this project compile without any
hardware attached**, and the app's entire UI runs in the Simulator.

### 0.1 Compile the firmware with no board

Finish installing **esp32 by Espressif Systems** (Stage 1.2–1.3), then:

- `Tools ▸ Board ▸ esp32 ▸ ESP32 Dev Module` — selecting a board needs no
  hardware, only the port does
- Open `esp32_standalone.ino`
- Click **Verify** (the ✓ checkmark, not the → arrow)

It compiles the whole sketch and reports sketch size. No board required.

This catches a missing or wrong ESP32 core, missing BLE libraries, and any
typo you introduced. The *only* thing it can't tell you is whether the
hardware works.

### 0.2 Build the app with no phone

Do all of Stage 3, but press **⌘B** (Build) instead of ⌘R (Run). Xcode
compiles against the iOS SDK with nothing plugged in.

Do the signing setup (3.5) now anyway — the certificate work is the same
whether or not a phone is attached, and it's annoying to discover a problem
there on demo day.

### 0.3 Run the whole UI in the Simulator

Pick any iPhone simulator from the device dropdown and press **⌘R**.

The app launches. Now turn on **Demo mode** — and everything works: the timer
counts down, the metrics populate, the RMSSD chart draws, the intensity slider
and duration presets respond, Start and Stop behave correctly.

That's because demo mode never touches the radio. You can exercise, screenshot,
and demo the complete interface on a laptop with nothing plugged into it.

Tapping **Connect** in the Simulator logs *"No Bluetooth radio here"* and does
nothing else. That's correct behavior, not a bug — the Simulator has no radio
to simulate, so BLE testing waits for a real phone.

### What Stage 0 leaves undone

| Verified without hardware | Needs hardware |
|---|---|
| Firmware compiles | Firmware actually runs |
| App compiles and signs | App on a real phone |
| Full UI, charts, controls | BLE pairing and commands |
| Session timing logic (via demo) | Real HRV from a sensor |
| | Output stage → transducer |

Four of five rows, on a laptop, tonight. When the board shows up you start at
Stage 1.5 with everything else already proven.

---

# Stage 1 — Firmware on the ESP32

**1.1** Install the Arduino IDE from arduino.cc if you don't have it.

**1.2** **File ▸ Preferences** (macOS: **Arduino IDE ▸ Settings**). In
*Additional boards manager URLs*, paste:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

**1.3** **Tools ▸ Board ▸ Boards Manager**. Search `esp32`. Install
**esp32 by Espressif Systems**. This downloads a few hundred MB — start it and
go do Stage 3 while you wait.

**1.4** Install the USB-serial driver. Look at the small chip next to your
board's USB jack:

- Says **CP2102** → install Silicon Labs' CP210x VCP driver
- Says **CH340** or **CH9102** → install the WCH CH34x driver

**This step is skipped more than any other and causes more lost time than any
other.** If no serial port ever appears, this is why.

**1.5** Plug in the ESP32. Then **Tools ▸ Board ▸ ESP32 Arduino ▸ ESP32 Dev
Module**, and **Tools ▸ Port ▸** whichever port appeared (`/dev/cu.usbserial-…`
or `/dev/cu.wchusbserial…` on a Mac).

**1.6** Open `firmware/esp32_standalone/esp32_standalone.ino` and click
**Upload** (→).

If it says *"Failed to connect… Timed out waiting for packet header"*, hold the
**BOOT** button on the ESP32 while the upload starts, then release. Some boards
need that; most don't.

**1.7** **Tools ▸ Serial Monitor**, set to **115200 baud** (dropdown, bottom
right).

### ✅ Checkpoint 1

```
AlphaX standalone starting
Advertising as AlphaX-01
{"state":"idle","intensity":0,"target":40,"remaining":0,...}
```

A status line every half second. If you see garbled characters, the baud rate
is wrong.

---

# Stage 2 — Session logic, no app, no circuit

The serial monitor is a full controller. Type into its input box and press
enter.

**2.1** Type `START:40:1` — a 1-minute session at 40%.

Watch `state` go `ramping_up` → `running`, `intensity` climb 0 → 40 over about
1.5 s, and `remaining` count down from 60. If GPIO2 has an onboard LED, it
fades up with the ramp.

**2.2** Type `SET:70`. Intensity should move to 70 while the session continues.

**2.3** Type `STOP`. State goes `ramping_down` → `idle`, intensity falls to 0.

**2.4** Now test the safety limits. Type `START:200:999`.

Intensity must clamp to **80**, duration to **20 minutes**. If either value
goes through unclamped, stop and fix it before you ever attach the circuit.

**2.5** Let a 1-minute session run all the way out without touching anything.
It should stop itself at zero.

### ✅ Checkpoint 2

Ramps are smooth, `SET` works live, `STOP` works, out-of-range values clamp,
and the session auto-expires. **Your entire device logic is now verified** —
with no app and no circuit.

> Write these five results down. This is design verification testing, and 2.4
> and 2.5 are exactly the kind of evidence your V&V section needs.

---

# Stage 3 — Build the app

**3.1** Xcode → **File ▸ New ▸ Project** → iOS → **App**.

| Field | Value |
|---|---|
| Product Name | `AlphaX` |
| Interface | SwiftUI |
| Language | Swift |
| Storage | None |
| Testing System | None |

**3.2** In Finder, select all five `.swift` files from `AlphaX/` and drag them
onto the yellow **AlphaX** folder in Xcode's sidebar. In the dialog: check
**Copy items if needed**, and tick **AlphaX** under *Add to targets*.

Xcode made its own `AlphaXApp.swift` and `ContentView.swift` — replace both. If
it won't let you, right-click the originals ▸ **Delete ▸ Move to Trash** first,
then drag the new files in.

**3.3** **The permission key. Do not skip this.**

Click the blue **AlphaX** project icon (top of sidebar) → select the **AlphaX**
target → **Info** tab. Hover any row, click **+**, and add:

| Key | Value |
|---|---|
| `Privacy - Bluetooth Always Usage Description` | `AlphaX connects to your stimulation device over Bluetooth.` |

Start typing "Privacy - Bluetooth" and it autocompletes. Without this, the app
builds fine and then **crashes the instant you tap Connect** — no error
message, no clue.

**3.4** **General** tab → **Minimum Deployments** → iOS **17.0**.

**3.5** **Signing & Capabilities** tab:

- Tick **Automatically manage signing**
- **Team** → *Add an Account…* → sign in with your normal Apple ID

**3.6** Plug the iPhone into the Mac. Unlock it. Tap **Trust This Computer**.

Pick your iPhone from the device dropdown at the top of the Xcode window — make
sure it's the phone, not a simulator. Press **⌘R**.

**3.7** The first launch will fail with *"Untrusted Developer."* On the iPhone:
**Settings ▸ General ▸ VPN & Device Management** → tap your Apple ID →
**Trust**. Press ⌘R again.

### ✅ Checkpoint 3

The AlphaX app opens on your phone showing `--:--`, three empty metric tiles,
and a greyed-out Start button.

> **The simulator has no Bluetooth radio.** Not a setting — there's nothing to
> simulate. CoreBluetooth reports "unsupported" there permanently. Every BLE
> test happens on the physical phone.

---

# Stage 4 — The app by itself

**4.1** Flip **Demo mode** on.

**4.2** Slider to 40%, tap **5 min**, tap **Start session**.

### ✅ Checkpoint 4

Timer counts down from 05:00. State reads STARTING then RUNNING. BPM and RMSSD
populate, intensity shows 40%. The chart begins drawing within a few seconds.

**4.3** Drag the intensity slider and release — the Intensity tile follows.

**4.4** Tap **Stop**. Timer resets, state goes to IDLE.

**4.5** Turn Demo mode **off** before Stage 5. The app ignores real hardware
while demo mode is on.

---

# Stage 5 — App talking to the board

**5.1** ESP32 powered, serial monitor open and printing.

**5.2** iPhone Bluetooth on, Demo mode **off**.

**5.3** Tap **Connect**.

The activity log should read:

```
Scanning for AlphaX…
Found AlphaX-01 — connecting.
Connected. Discovering services…
Ready.
```

**5.4** Tap **Start session**. Now watch both screens at once.

### ✅ Checkpoint 5

The phone's timer and the serial monitor's `remaining` field agree within one
second. Commands you tap appear in the serial monitor as `[cmd] START:40:20`.

**5.4a** If nothing is found after 12 seconds, tap **Browse all devices**
instead of guessing. Every BLE device in range gets listed:

- Your board appears **with** the AlphaX badge → tap it, you're connected
- It appears **without** the badge → it's advertising, but not the service
  UUID. Check `adv->addServiceUUID(SERVICE_UUID)` in the firmware.
- It doesn't appear at all → it isn't advertising. The problem is the board,
  not the app — go back to Checkpoint 1.

**5.5** Test the fail-safe: start a session, then walk away with the phone or
turn its Bluetooth off. The serial monitor should show
`app disconnected -> stopping session` and ramp down on its own.

That one is worth writing down too. "Output terminates on loss of
communication" is a safety requirement, and you just demonstrated it.

---

# Stage 6 — The circuit

Everything above moves bits. This moves current. Do it last, and measure first.

**6.1** With the circuit **not** connected, put a meter or scope on **GPIO25**
and run `START:40:1`. Confirm PWM ramps up over 1.5 s, holds, ramps down.

**6.2** Power the LT1637 board alone. Measure the **idle voltage** at the node
you plan to drive. If it's already sitting at a bias voltage, driving 3.3 V PWM
into it will fight that bias — you need a different injection point or a series
resistor. **Figure this out with a meter, not by connecting it and hoping.**

**6.3** Connect **grounds only**: ESP32 GND ↔ LT1637 GND ↔ 9 V negative. Check
continuity. Nothing else yet.

> **The 9 V rail must never touch the ESP32.** Not through a resistor, not
> "just to check." 9 V on a 3.3 V pin kills the chip instantly.

**6.4** Connect GPIO25 through your chosen interface:

**Proportional** — intensity actually controls amplitude:

```
GPIO25 ──[ 10kΩ ]──┬── op-amp control input
                   │
                 1µF
                   │
                  GND
```

**On/off** — GPIO25 as a digital enable through a small NPN transistor or
analog switch. Simpler, but the intensity slider becomes decorative.

**6.5** Run at **5%** first. Then 10, 20, 40 — watching the transducer and the
op-amp output as you go.

**6.6** Only now bring the app back in.

### ✅ Checkpoint 6

Transducer responds, intensity changes are perceptible, ramps feel gradual
rather than abrupt, and Stop works immediately.

---

# Functional test matrix

Run this end to end before any demo. It doubles as your verification record.

| # | Test | How | Expected | Why it matters |
|---|---|---|---|---|
| 1 | Firmware boots | Power + serial | `Advertising as AlphaX-01` | Baseline |
| 2 | Ramp up | `START:40:1` | 0→40% over ~1.5 s | No startle response |
| 3 | Live adjust | `SET:70` mid-session | Target moves, session continues | Usability |
| 4 | Manual stop | `STOP` | Ramps down to idle | User control |
| 5 | Intensity cap | `START:200:5` | Clamps to 80% | **Safety** |
| 6 | Duration cap | `START:40:999` | Clamps to 20 min | **Safety** |
| 7 | Auto shut-off | Let 1 min expire | Stops unprompted | **Safety** |
| 8 | BLE pairing | Tap Connect | "Ready." in log | Core function |
| 9 | Timer agreement | Compare phone vs serial | Within 1 s | State integrity |
| 10 | Link-loss safety | Disable phone BT mid-session | Device stops itself | **Safety** |
| 11 | HRV capture | Pulse sensor on GPIO34 | BPM 50–100, RMSSD non-zero | Efficacy claim |
| 12 | Output response | Circuit attached, 5→40% | Perceptible, graduated | Core function |

Tests 5, 6, 7, and 10 are your safety case. Tests 1–10 need no circuit at all.

---

# Troubleshooting

| Symptom | Cause |
|---|---|
| No serial port in Arduino IDE | USB driver not installed — Stage 1.4 |
| "Failed to connect… packet header" | Hold BOOT during upload |
| Garbled serial output | Baud rate isn't 115200 |
| App crashes on Connect | Missing Bluetooth privacy key — Stage 3.3 |
| App won't launch, "Untrusted Developer" | Stage 3.7, or signing expired — press ⌘R |
| "No device found" after 12 s | Board not advertising, or Demo mode is still on — tap **Browse all devices** to tell which |
| Connects, but nothing updates | `BLEDevice::setMTU(247)` must come *before* `BLEDevice::init()` |
| "Unreadable status packet" in log | Same MTU issue, or `sendStatus()` changed without updating `DeviceStatus.swift` |
| BPM/RMSSD stuck at `--` | No pulse sensor, or it's on an ADC2 pin — move to GPIO34 |
| Timer runs, transducer silent | Logic is fine. Problem is GPIO25 → op-amp — Stage 6 |

---

# Safety

- `MAX_INTENSITY` 80% and `MAX_SESSION_MIN` 60 stay as they are
- 1.5 s ramps both directions — nothing starts or stops abruptly
- Session stops automatically if BLE drops
- All output flows through `applyOutput()` — one function to audit

**Software limits are not a current limit.** Vibrating a transducer is one
thing. If your output stage ever passes current through a person, that needs
hardware current limiting, galvanic isolation, and Dr. Sulejmani's review
before it touches skin — not a `const uint8_t` and good intentions.
