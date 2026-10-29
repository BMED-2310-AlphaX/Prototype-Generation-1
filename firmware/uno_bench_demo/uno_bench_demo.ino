/*
 * AlphaX — Uno R3 bench demo
 * Team AlphaX, BMED 2310
 *
 * Built entirely from the LAFVIN Basic Starter Kit. No Bluetooth, no purchases.
 *
 * WHAT THIS GIVES YOU
 *   A physical prototype someone can hold and operate: a knob that sets
 *   intensity, a button that starts and stops, an LED that ramps so you can
 *   SEE the soft start, an RGB LED showing session state, and a chirp at the
 *   start and end of a session.
 *
 *   The session logic, timing, and safety caps are identical to the ESP32
 *   firmware. Only the output stage differs — an LED instead of the LT1637
 *   board. When the real circuit arrives, you move one wire from the LED to
 *   the op-amp control input and change nothing in software.
 *
 *   The serial protocol is unchanged too, so bridge.py still drives it from
 *   the browser.
 *
 * WIRING  (all parts from the kit)
 *
 *   OUTPUT — the stand-in for the LT1637 stage
 *     D9  ──▶|── [220Ω] ── GND          red LED, fades with intensity
 *
 *   STATE — RGB LED, common cathode (longest leg to GND)
 *     D3  ── [220Ω] ── R leg
 *     D5  ── [220Ω] ── G leg
 *     D6  ── [220Ω] ── B leg
 *     GND ── longest leg
 *     (If yours is common anode, set RGB_COMMON_ANODE to 1 below.)
 *
 *   CONTROLS
 *     A1  ── potentiometer wiper   (outer legs to 5V and GND)
 *     D2  ── button ── GND         (other side; uses the internal pull-up)
 *     D8  ── active buzzer +       (− to GND)
 *
 *   OPTIONAL
 *     A0  ── pulse sensor out, if you ever get one
 *     D13 ── built-in LED, lit whenever a session is running. No wiring.
 *
 *   Nothing here is required. With zero parts connected, D13 and the serial
 *   monitor still show the whole session running.
 */

#include <stdlib.h>          // dtostrf

// ---------------------------------------------------------------- config ---

#define OUTPUT_PIN    9      // PWM — LED now, op-amp control input later
#define RGB_R         3
#define RGB_G         5
#define RGB_B         6
#define POT_PIN      A1
#define BUTTON_PIN    2
#define BUZZER_PIN    8
#define PULSE_PIN    A0
#define STATUS_LED   13

#define RGB_COMMON_ANODE 0   // set to 1 if the RGB LED's long leg goes to 5V

const uint8_t  MAX_INTENSITY    = 80;     // percent — hard cap
const uint16_t MAX_SESSION_MIN  = 60;     // minutes — auto shut-off ceiling
const uint16_t RAMP_MS          = 1500;   // soft start / soft stop
const uint16_t STATUS_PERIOD_MS = 500;
const uint8_t  SAMPLE_PERIOD_MS = 10;
const uint16_t CHIRP_MS         = 90;
const uint8_t  POT_DEADBAND     = 3;      // percent the knob must move to take over
const uint16_t DEBOUNCE_MS      = 40;

const uint16_t DEFAULT_MINUTES  = 5;      // button-started sessions

// ----------------------------------------------------------------- state ---

enum SessionState { IDLE, RAMPING_UP, RUNNING, RAMPING_DOWN };

SessionState state           = IDLE;
uint8_t      targetIntensity = 40;
uint8_t      liveIntensity   = 0;
uint16_t     sessionMinutes  = DEFAULT_MINUTES;
uint32_t     sessionStartMs  = 0;
uint32_t     rampStartMs     = 0;
uint32_t     lastStatusMs    = 0;
uint32_t     lastSampleMs    = 0;

char    rxBuf[40];
uint8_t rxLen = 0;

uint32_t chirpUntilMs  = 0;
uint8_t  lastPotPct    = 255;        // 255 = not yet read
bool     lastButton    = HIGH;
uint32_t lastButtonMs  = 0;

// -------------------------------------------------------- pulse  +  HRV ----

const uint8_t IBI_BUFFER = 12;
uint16_t ibiBuf[IBI_BUFFER];
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
  if (ibi < 300 || ibi > 2000) return;

  ibiBuf[ibiIndex] = (uint16_t)ibi;
  ibiIndex = (ibiIndex + 1) % IBI_BUFFER;
  if (ibiCount < IBI_BUFFER) ibiCount++;

  uint32_t sum = 0;
  for (uint8_t i = 0; i < ibiCount; i++) sum += ibiBuf[i];
  currentBPM = 60000.0 / (sum / (float)ibiCount);

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
  int raw = analogRead(PULSE_PIN);
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

// --------------------------------------------------------------- outputs ---

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
#if RGB_COMMON_ANODE
  analogWrite(RGB_R, 255 - r); analogWrite(RGB_G, 255 - g); analogWrite(RGB_B, 255 - b);
#else
  analogWrite(RGB_R, r); analogWrite(RGB_G, g); analogWrite(RGB_B, b);
#endif
}

void showState() {
  switch (state) {
    case IDLE:         setRGB(0,   0,  40); break;   // dim blue — ready
    case RAMPING_UP:   setRGB(90, 45,   0); break;   // amber — changing
    case RAMPING_DOWN: setRGB(90, 45,   0); break;
    case RUNNING:      setRGB(0,  90,  25); break;   // green — active
  }
}

void chirp() { chirpUntilMs = millis() + CHIRP_MS; }

void serviceChirp() {
  digitalWrite(BUZZER_PIN, millis() < chirpUntilMs ? HIGH : LOW);
}

// The ONLY function that touches the output stage. Swap the LED for the
// LT1637 control input and this line is still all that drives it.
void applyOutput(uint8_t percent) {
  if (percent > MAX_INTENSITY) percent = MAX_INTENSITY;
  liveIntensity = percent;
  analogWrite(OUTPUT_PIN, (int)((uint16_t)percent * 255 / 100));
  digitalWrite(STATUS_LED, percent > 0 ? HIGH : LOW);
}

void stopOutputNow() {
  analogWrite(OUTPUT_PIN, 0);
  digitalWrite(STATUS_LED, LOW);
  liveIntensity = 0;
}

// -------------------------------------------------------------- sessions ---

void startSession(uint8_t intensity, uint16_t minutes) {
  if (intensity > MAX_INTENSITY) intensity = MAX_INTENSITY;
  if (minutes == 0 || minutes > MAX_SESSION_MIN) minutes = DEFAULT_MINUTES;
  targetIntensity = intensity;
  sessionMinutes  = minutes;
  sessionStartMs  = millis();
  rampStartMs     = millis();
  state           = RAMPING_UP;
  chirp();
}

void stopSession() {
  if (state == IDLE) return;
  rampStartMs = millis();
  state = RAMPING_DOWN;
  chirp();
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
  showState();
}

uint16_t remainingSeconds() {
  if (state == IDLE) return 0;
  uint32_t total = (uint32_t)sessionMinutes * 60000UL;
  uint32_t el    = millis() - sessionStartMs;
  return (el >= total) ? 0 : (uint16_t)((total - el) / 1000);
}

const char *stateName() {
  switch (state) {
    case IDLE:         return "idle";
    case RAMPING_UP:   return "ramping_up";
    case RUNNING:      return "running";
    case RAMPING_DOWN: return "ramping_down";
  }
  return "unknown";
}

// --------------------------------------------------------- physical UI -----

// The knob only takes over once you actually move it, so serial and app
// commands still work until someone reaches for the hardware.
void servicePot() {
  uint8_t pct = (uint8_t)((analogRead(POT_PIN) * (uint32_t)MAX_INTENSITY) / 1023);

  if (lastPotPct == 255) { lastPotPct = pct; return; }

  int8_t delta = (int8_t)pct - (int8_t)lastPotPct;
  if (delta < 0) delta = -delta;
  if (delta < POT_DEADBAND) return;

  lastPotPct = pct;
  targetIntensity = pct;
  if (state == RUNNING) applyOutput(pct);
}

void serviceButton() {
  bool now = digitalRead(BUTTON_PIN);
  uint32_t t = millis();

  if (now != lastButton && (t - lastButtonMs) > DEBOUNCE_MS) {
    lastButtonMs = t;
    lastButton = now;
    if (now == LOW) {                       // pressed (pull-up, so LOW)
      if (state == IDLE) startSession(targetIntensity, DEFAULT_MINUTES);
      else               stopSession();
    }
  }
}

// -------------------------------------------------------------- comms ------

void sendStatus() {
  // avr-libc's snprintf can't print floats — dtostrf converts them first.
  // Leaving %f in here silently emits "?", a classic AVR trap.
  char bpmStr[8], rmsStr[8], out[150];
  dtostrf(currentBPM,   0, 1, bpmStr);
  dtostrf(currentRMSSD, 0, 1, rmsStr);

  snprintf(out, sizeof(out),
    "{\"state\":\"%s\",\"intensity\":%u,\"target\":%u,\"remaining\":%u,"
    "\"bpm\":%s,\"rmssd\":%s,\"batt\":255}",
    stateName(), liveIntensity, targetIntensity, remainingSeconds(),
    bpmStr, rmsStr);

  Serial.println(out);
}

void handleCommand(char *cmd) {
  if (cmd[0] == '\0') return;

  if (strncmp(cmd, "START", 5) == 0) {
    uint8_t  inten = targetIntensity;
    uint16_t mins  = DEFAULT_MINUTES;
    char *p1 = strchr(cmd, ':');
    if (p1) {
      inten = atoi(p1 + 1);
      char *p2 = strchr(p1 + 1, ':');
      if (p2) mins = atoi(p2 + 1);
    }
    startSession(inten, mins);

  } else if (strncmp(cmd, "STOP", 4) == 0) {
    stopSession();

  } else if (strncmp(cmd, "SET", 3) == 0) {
    char *p1 = strchr(cmd, ':');
    if (p1) {
      uint8_t inten = atoi(p1 + 1);
      if (inten > MAX_INTENSITY) inten = MAX_INTENSITY;
      targetIntensity = inten;
      if (state == RUNNING) applyOutput(inten);
    }
  }
  sendStatus();
}

void pumpSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      rxBuf[rxLen] = '\0';
      if (rxLen) handleCommand(rxBuf);
      rxLen = 0;
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = ch;
    } else {
      rxLen = 0;
    }
  }
}

// ------------------------------------------------------------------ main ---

void setup() {
  pinMode(OUTPUT_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(RGB_R, OUTPUT); pinMode(RGB_G, OUTPUT); pinMode(RGB_B, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  stopOutputNow();
  digitalWrite(BUZZER_PIN, LOW);
  showState();

  Serial.begin(115200);
  delay(300);
  Serial.println(F("AlphaX Uno bench demo ready"));
  Serial.println(F("Knob = intensity, button = start/stop"));
  Serial.println(F("Serial: START:<pct>:<min>  SET:<pct>  STOP"));
}

void loop() {
  uint32_t now = millis();

  pumpSerial();
  serviceButton();
  servicePot();
  serviceChirp();

  if (now - lastSampleMs >= SAMPLE_PERIOD_MS) { lastSampleMs = now; samplePulse(); }

  updateSession();

  if (now - lastStatusMs >= STATUS_PERIOD_MS) { lastStatusMs = now; sendStatus(); }
}
