/*
 * AlphaX — STM32 Session Controller
 * Board: NUCLEO-F303RE   (Arduino core for STM32: "STM32 boards" package,
 *                         Board part number = "Nucleo F303RE")
 * Team AlphaX, BMED 2310
 *
 * ROLE
 *   This is the brain. It owns the session state machine, the safety limits,
 *   and the physiological measurement. It talks to the ESP32 over UART; the
 *   ESP32 just relays to the phone.
 *
 * WIRING
 *   D3  (PB3)  -> LT1637 stage control input   (PWM, see note below)
 *   A0  (PA0)  <- pulse sensor analog out       (optional, for HRV)
 *   D8  (PA9)  -> ESP32 GPIO16 (RX2)            UART TX
 *   D2  (PA10) <- ESP32 GPIO17 (TX2)            UART RX
 *   GND        <-> ESP32 GND  AND  <-> 9 V board GND     REQUIRED
 *
 * ABOUT THE OUTPUT PIN
 *   Your LT1637 board runs from the 9 V battery and drives the haptic
 *   transducer. This pin does NOT power the transducer — it tells the op-amp
 *   stage when and how hard to run. How you connect it depends on how that
 *   stage is built:
 *
 *     a) If the op-amp is configured as an amplifier, feed this PWM through an
 *        RC low-pass (e.g. 10k + 1uF) to make a 0-3.3 V analog control voltage
 *        into the op-amp input. Smooth DC in, proportional drive out.
 *
 *     b) If the stage is an oscillator that is simply on or off, use this pin
 *        as a digital enable through a transistor or analog switch.
 *
 *   Either way, the 9 V rail and the 3.3 V logic must share a ground and must
 *   never be tied together on any other node.
 *
 * SAFETY
 *   MAX_INTENSITY and MAX_SESSION_MIN are the software guardrails. They are
 *   not a substitute for a hardware current limit on the output stage. Keep
 *   both, and have the circuit reviewed before anything touches skin.
 */

// ---------------------------------------------------------------- config ---

#define OUTPUT_PIN   PB3     // D3  — control into LT1637 stage
#define PULSE_PIN    PA0     // A0  — analog pulse sensor (optional)

HardwareSerial LinkSerial(PA10, PA9);   // RX, TX  -> to ESP32

const uint8_t  MAX_INTENSITY    = 80;    // percent — hard cap
const uint16_t MAX_SESSION_MIN  = 60;    // minutes — auto shut-off ceiling
const uint16_t RAMP_MS          = 1500;  // soft start / soft stop
const uint16_t STATUS_PERIOD_MS = 500;   // status push rate
const uint16_t SAMPLE_PERIOD_MS = 10;    // 100 Hz pulse sampling

// ----------------------------------------------------------------- state ---

enum SessionState { IDLE, RAMPING_UP, RUNNING, RAMPING_DOWN };

SessionState state           = IDLE;
uint8_t      targetIntensity = 40;
uint8_t      liveIntensity   = 0;
uint16_t     sessionMinutes  = 20;
uint32_t     sessionStartMs  = 0;
uint32_t     rampStartMs     = 0;
uint32_t     lastStatusMs    = 0;
uint32_t     lastSampleMs    = 0;

String       rxLine = "";

// ------------------------------------------------------- pulse  +  HRV -----

const uint8_t IBI_BUFFER = 12;
uint16_t ibiBuf[IBI_BUFFER] = {0};
uint8_t  ibiCount = 0, ibiIndex = 0;

uint32_t lastBeatMs     = 0;
int      signalBaseline = 512;
int      signalPeak     = 512;
bool     aboveThreshold = false;
float    currentBPM     = 0;
float    currentRMSSD   = 0;

void recordBeat(uint32_t nowMs) {
  if (lastBeatMs == 0) { lastBeatMs = nowMs; return; }
  uint32_t ibi = nowMs - lastBeatMs;
  lastBeatMs = nowMs;

  if (ibi < 300 || ibi > 2000) return;     // reject artifacts (200 / 30 bpm)

  ibiBuf[ibiIndex] = (uint16_t)ibi;
  ibiIndex = (ibiIndex + 1) % IBI_BUFFER;
  if (ibiCount < IBI_BUFFER) ibiCount++;

  uint32_t sum = 0;
  for (uint8_t i = 0; i < ibiCount; i++) sum += ibiBuf[i];
  currentBPM = 60000.0f / (sum / (float)ibiCount);

  // RMSSD — root mean square of successive differences. Rises with
  // parasympathetic activity, which is the effect our device claims.
  if (ibiCount >= 3) {
    float sq = 0; uint8_t pairs = 0;
    for (uint8_t i = 1; i < ibiCount; i++) {
      uint8_t a = (ibiIndex + i - 1) % IBI_BUFFER;
      uint8_t b = (ibiIndex + i) % IBI_BUFFER;
      float d = (float)ibiBuf[b] - (float)ibiBuf[a];
      sq += d * d; pairs++;
    }
    if (pairs) currentRMSSD = sqrt(sq / pairs);
  }
}

void samplePulse() {
  int raw = analogRead(PULSE_PIN);          // 0..1023 by default on this core

  signalBaseline = (signalBaseline * 31 + raw) / 32;
  if (raw > signalPeak) signalPeak = raw;
  else signalPeak = (signalPeak * 255 + raw) / 256;

  int threshold = signalBaseline + (signalPeak - signalBaseline) / 2;
  uint32_t now = millis();

  if (!aboveThreshold && raw > threshold && (now - lastBeatMs) > 300) {
    aboveThreshold = true;
    recordBeat(now);
  } else if (aboveThreshold && raw < threshold) {
    aboveThreshold = false;
  }
}

// ---------------------------------------------------------------- output ---
// The ONLY function that touches the output stage. Change the body here if
// the LT1637 board needs a digital enable instead of a proportional level.

void applyOutput(uint8_t percent) {
  if (percent > MAX_INTENSITY) percent = MAX_INTENSITY;
  liveIntensity = percent;
  analogWrite(OUTPUT_PIN, (int)((uint32_t)percent * 255 / 100));
}

void stopOutputNow() {
  analogWrite(OUTPUT_PIN, 0);
  liveIntensity = 0;
}

// -------------------------------------------------------------- sessions ---

void startSession(uint8_t intensity, uint16_t minutes) {
  if (intensity > MAX_INTENSITY) intensity = MAX_INTENSITY;
  if (minutes == 0 || minutes > MAX_SESSION_MIN) minutes = 20;
  targetIntensity = intensity;
  sessionMinutes  = minutes;
  sessionStartMs  = millis();
  rampStartMs     = millis();
  state           = RAMPING_UP;
}

void stopSession() {
  if (state == IDLE) return;
  rampStartMs = millis();
  state = RAMPING_DOWN;
}

void updateSession() {
  uint32_t now = millis();

  switch (state) {
    case RAMPING_UP: {
      uint32_t e = now - rampStartMs;
      if (e >= RAMP_MS) { applyOutput(targetIntensity); state = RUNNING; }
      else applyOutput((uint8_t)((uint32_t)targetIntensity * e / RAMP_MS));
      break;
    }
    case RUNNING:
      if ((now - sessionStartMs) >= (uint32_t)sessionMinutes * 60000UL) stopSession();
      else if (liveIntensity != targetIntensity) applyOutput(targetIntensity);
      break;

    case RAMPING_DOWN: {
      uint32_t e = now - rampStartMs;
      if (e >= RAMP_MS) { stopOutputNow(); state = IDLE; }
      else {
        uint8_t from = liveIntensity ? liveIntensity : targetIntensity;
        applyOutput((uint8_t)((uint32_t)from * (RAMP_MS - e) / RAMP_MS));
      }
      break;
    }
    case IDLE:
    default: break;
  }
}

uint16_t remainingSeconds() {
  if (state == IDLE) return 0;
  uint32_t total = (uint32_t)sessionMinutes * 60000UL;
  uint32_t el    = millis() - sessionStartMs;
  return (el >= total) ? 0 : (uint16_t)((total - el) / 1000);
}

const char *stateName() {
  switch (state) {
    case IDLE: return "idle";
    case RAMPING_UP: return "ramping_up";
    case RUNNING: return "running";
    case RAMPING_DOWN: return "ramping_down";
  }
  return "unknown";
}

void sendStatus() {
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"intensity\":%u,\"target\":%u,\"remaining\":%u,"
    "\"bpm\":%.1f,\"rmssd\":%.1f,\"batt\":255}",
    stateName(), liveIntensity, targetIntensity, remainingSeconds(),
    currentBPM, currentRMSSD);

  LinkSerial.println(buf);     // to the ESP32 -> BLE -> app
  Serial.println(buf);         // to the ST-Link USB serial, for debugging
}

// -------------------------------------------------------------- commands ---
//   START:<intensity>:<minutes>
//   STOP
//   SET:<intensity>
//   HELLO

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  Serial.print("[cmd] "); Serial.println(cmd);

  if (cmd.startsWith("START")) {
    int c1 = cmd.indexOf(':');
    int c2 = cmd.indexOf(':', c1 + 1);
    uint8_t  inten = (c1 > 0) ? cmd.substring(c1 + 1, c2 > 0 ? c2 : cmd.length()).toInt() : 40;
    uint16_t mins  = (c2 > 0) ? cmd.substring(c2 + 1).toInt() : 20;
    startSession(inten, mins);

  } else if (cmd.startsWith("STOP")) {
    stopSession();

  } else if (cmd.startsWith("SET")) {
    int c1 = cmd.indexOf(':');
    if (c1 > 0) {
      uint8_t inten = cmd.substring(c1 + 1).toInt();
      if (inten > MAX_INTENSITY) inten = MAX_INTENSITY;
      targetIntensity = inten;
      if (state == RUNNING) applyOutput(inten);
    }
  }
  sendStatus();
}

// ------------------------------------------------------------------ main ---

void setup() {
  pinMode(OUTPUT_PIN, OUTPUT);
  stopOutputNow();

  pinMode(PULSE_PIN, INPUT_ANALOG);

  Serial.begin(115200);        // USB via ST-Link
  LinkSerial.begin(115200);    // to ESP32
  delay(300);
  Serial.println("\nAlphaX session controller ready (STM32 F303RE)");
}

void loop() {
  uint32_t now = millis();

  while (LinkSerial.available()) {
    char ch = (char)LinkSerial.read();
    if (ch == '\n' || ch == '\r') { handleCommand(rxLine); rxLine = ""; }
    else { rxLine += ch; if (rxLine.length() > 120) rxLine = ""; }
  }

  // Also accept typed commands over USB serial, handy when debugging without
  // the phone connected.
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') { handleCommand(rxLine); rxLine = ""; }
    else { rxLine += ch; if (rxLine.length() > 120) rxLine = ""; }
  }

  if (now - lastSampleMs >= SAMPLE_PERIOD_MS) { lastSampleMs = now; samplePulse(); }

  updateSession();

  if (now - lastStatusMs >= STATUS_PERIOD_MS) { lastStatusMs = now; sendStatus(); }
}
