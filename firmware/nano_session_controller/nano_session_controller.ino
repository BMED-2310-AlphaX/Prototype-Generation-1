/*
 * AlphaX — Arduino Nano R3 session controller
 * Team AlphaX, BMED 2310
 *
 * WHAT THIS IS FOR
 *   The Nano R3 (ATmega328P) has no Bluetooth radio, so it cannot talk to the
 *   iPhone app by itself. What it CAN do is run the full session state machine
 *   — ramps, timing, safety caps, HRV — driven over the USB serial monitor.
 *
 *   That means you can verify every behaviour your design claims tonight, with
 *   a board you already own, and swap in a radio later without changing any of
 *   the logic below.
 *
 * ADDING BLUETOOTH LATER
 *   Set USE_BLE_MODULE to 1 and wire an HM-10 (or AT-09) BLE module to D10/D11.
 *   Commands then arrive over the module instead of USB.
 *
 *   An HC-05 or HC-06 will NOT work with an iPhone. Those are Bluetooth
 *   Classic / SPP, which iOS does not allow apps to use. Only BLE modules
 *   (HM-10, AT-09) can talk to the app.
 *
 * WIRING (nothing required — the built-in LED works with no parts)
 *   D9   PWM output. Put an LED + 220Ω to GND here to watch the ramp fade.
 *        Later, this is the control line into the LT1637 stage.
 *   D13  built-in LED, lit whenever a session is active. No wiring needed.
 *   A0   pulse sensor analog in (optional)
 *
 *   D10  <- HM-10 TX    (only if USE_BLE_MODULE 1)
 *   D11  -> HM-10 RX    via a divider, see note at the bottom
 *
 * MEMORY NOTE
 *   The Nano has 2 KB of SRAM, so this avoids the String class and keeps
 *   static text in flash with F(). That is why the parsing looks more manual
 *   than the ESP32 version — it is the same logic, written to fit.
 */

#include <stdlib.h>          // dtostrf

// ---------------------------------------------------------------- config ---

#define USE_BLE_MODULE 0     // 0 = USB serial only, 1 = HM-10 on D10/D11

#define OUTPUT_PIN   9       // PWM — LED now, op-amp control later
#define STATUS_LED   13      // built-in, on while a session runs
#define PULSE_PIN    A0      // optional pulse sensor

const uint8_t  MAX_INTENSITY    = 80;     // percent — hard cap
const uint16_t MAX_SESSION_MIN  = 60;     // minutes — auto shut-off ceiling
const uint16_t RAMP_MS          = 1500;   // soft start / soft stop
const uint16_t STATUS_PERIOD_MS = 500;    // status print rate
const uint8_t  SAMPLE_PERIOD_MS = 10;     // 100 Hz pulse sampling

#if USE_BLE_MODULE
  #include <SoftwareSerial.h>
  SoftwareSerial BLESerial(10, 11);       // RX, TX
  #define LINK BLESerial
  const long LINK_BAUD = 9600;            // HM-10 default
#else
  #define LINK Serial
  const long LINK_BAUD = 115200;
#endif

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

char    rxBuf[40];
uint8_t rxLen = 0;

// -------------------------------------------------------- pulse  +  HRV ----

const uint8_t IBI_BUFFER = 12;
uint16_t ibiBuf[IBI_BUFFER];
uint8_t  ibiCount = 0, ibiIndex = 0;

uint32_t lastBeatMs     = 0;
int      signalBaseline = 512;            // 10-bit ADC midpoint
int      signalPeak     = 512;
bool     aboveThreshold = false;
float    currentBPM     = 0;
float    currentRMSSD   = 0;

void recordBeat(uint32_t nowMs) {
  if (lastBeatMs == 0) { lastBeatMs = nowMs; return; }
  uint32_t ibi = nowMs - lastBeatMs;
  lastBeatMs = nowMs;

  if (ibi < 300 || ibi > 2000) return;    // reject artifacts (200 / 30 bpm)

  ibiBuf[ibiIndex] = (uint16_t)ibi;
  ibiIndex = (ibiIndex + 1) % IBI_BUFFER;
  if (ibiCount < IBI_BUFFER) ibiCount++;

  uint32_t sum = 0;
  for (uint8_t i = 0; i < ibiCount; i++) sum += ibiBuf[i];
  currentBPM = 60000.0 / (sum / (float)ibiCount);

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
  int raw = analogRead(PULSE_PIN);        // 0..1023

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
// The ONLY function that touches hardware. Change this one body if the output
// stage ever needs something other than a proportional PWM level.

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
    case IDLE:         return "idle";
    case RAMPING_UP:   return "ramping_up";
    case RUNNING:      return "running";
    case RAMPING_DOWN: return "ramping_down";
  }
  return "unknown";
}

void sendStatus() {
  // avr-libc's snprintf cannot print floats, so dtostrf converts them first.
  // Leaving %f in here silently prints "?" — a classic AVR trap.
  char bpmStr[8], rmsStr[8], out[150];
  dtostrf(currentBPM,   0, 1, bpmStr);
  dtostrf(currentRMSSD, 0, 1, rmsStr);

  snprintf(out, sizeof(out),
    "{\"state\":\"%s\",\"intensity\":%u,\"target\":%u,\"remaining\":%u,"
    "\"bpm\":%s,\"rmssd\":%s,\"batt\":255}",
    stateName(), liveIntensity, targetIntensity, remainingSeconds(),
    bpmStr, rmsStr);

  LINK.println(out);
#if USE_BLE_MODULE
  Serial.println(out);        // mirror to USB for debugging
#endif
}

// -------------------------------------------------------------- commands ---
//   START:<intensity>:<minutes>
//   STOP
//   SET:<intensity>

void handleCommand(char *cmd) {
  if (cmd[0] == '\0') return;

  if (strncmp(cmd, "START", 5) == 0) {
    uint8_t  inten = 40;
    uint16_t mins  = 20;
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

void pump(Stream &port) {
  while (port.available()) {
    char ch = (char)port.read();
    if (ch == '\n' || ch == '\r') {
      rxBuf[rxLen] = '\0';
      if (rxLen) handleCommand(rxBuf);
      rxLen = 0;
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = ch;
    } else {
      rxLen = 0;                        // overlong line, discard
    }
  }
}

// ------------------------------------------------------------------ main ---

void setup() {
  pinMode(OUTPUT_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  stopOutputNow();

  Serial.begin(115200);
#if USE_BLE_MODULE
  BLESerial.begin(LINK_BAUD);
#endif
  delay(300);

  Serial.println(F("AlphaX Nano session controller ready"));
  Serial.println(F("Commands: START:<pct>:<min>  SET:<pct>  STOP"));
}

void loop() {
  uint32_t now = millis();

  pump(Serial);
#if USE_BLE_MODULE
  pump(BLESerial);
#endif

  if (now - lastSampleMs >= SAMPLE_PERIOD_MS) { lastSampleMs = now; samplePulse(); }

  updateSession();

  if (now - lastStatusMs >= STATUS_PERIOD_MS) { lastStatusMs = now; sendStatus(); }
}

/*
 * HM-10 WIRING, when you get one
 *
 *   HM-10 VCC  -> Nano 5V      (most breakout boards regulate to 3.3 V)
 *   HM-10 GND  -> Nano GND
 *   HM-10 TX   -> Nano D10     direct, 3.3 V is a valid HIGH for the Nano
 *   HM-10 RX   <- Nano D11 through a divider:
 *
 *        D11 ──[ 1kΩ ]──┬── HM-10 RX
 *                       │
 *                     2kΩ
 *                       │
 *                      GND
 *
 *   The Nano drives 5 V and the HM-10's RX pin is 3.3 V tolerant only. Wiring
 *   D11 straight to it will damage the module over time.
 *
 *   The app also needs different UUIDs for an HM-10: it exposes a fixed
 *   service FFE0 with characteristic FFE1, not our custom 6E5A000x set.
 */
