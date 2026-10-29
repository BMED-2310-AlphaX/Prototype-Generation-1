/*
 * AlphaX — ESP32 standalone firmware
 * Team AlphaX, BMED 2310
 *
 * One board does everything: BLE radio, session state machine, safety limits,
 * PWM output to the op-amp stage, and optional pulse sensing for HRV. Use this
 * if you drop the Nucleo. If you keep the two-board rig, use the separate
 * esp32_ble_bridge + stm32_session_controller sketches instead.
 *
 * WIRING
 *   GPIO25  ->  control input of the LT1637 output stage
 *   GPIO34  <-  pulse sensor analog out        (optional, input-only pin)
 *   GND     <-> 9 V board GND                  REQUIRED, the only shared node
 *
 *   GPIO34 is on ADC1. Do not move the sensor to GPIO0/2/4/12-15/25-27 — those
 *   are ADC2, which the ESP32 cannot read while the radio is on. analogRead()
 *   there returns garbage with BLE active, and it is a miserable bug to find.
 *
 * POWER
 *   USB, or 5 V into VIN. Never the 9 V battery. The 9 V rail belongs to the
 *   output stage only; logic and output share a ground and nothing else.
 *
 * SAFETY
 *   MAX_INTENSITY and MAX_SESSION_MIN are software guardrails, not a current
 *   limit. Keep them, and have the output stage reviewed before anything
 *   touches skin.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------------------------------------------------------- config ---

#define DEVICE_NAME  "AlphaX-01"

#define OUTPUT_PIN   25      // PWM into the LT1637 stage
#define PULSE_PIN    34      // ADC1, input-only — safe with BLE running

// Mirrors the output duty onto an LED so you can SEE the ramp with no circuit
// attached. GPIO2 is the onboard LED on most ESP32 DevKits. Any free pin works
// with an external LED + 330 ohm resistor to GND. Set to -1 to disable.
#define LED_MIRROR_PIN 2
#define LED_CHANNEL    1     // core 2.x only

#define PWM_FREQ       20000 // 20 kHz, above hearing
#define PWM_RES_BITS   8     // 0..255
#define PWM_CHANNEL    0     // only used by core 2.x

const uint8_t  MAX_INTENSITY    = 80;    // percent — hard cap
const uint16_t MAX_SESSION_MIN  = 60;    // minutes — auto shut-off ceiling
const uint16_t RAMP_MS          = 1500;  // soft start / soft stop
const uint16_t STATUS_PERIOD_MS = 500;   // status notify rate
const uint16_t SAMPLE_PERIOD_MS = 10;    // 100 Hz pulse sampling

// BLE UUIDs — must match BLEManager.swift exactly
#define SERVICE_UUID     "6e5a0001-b5a3-f393-e0a9-e50e24dcca9e"
#define CMD_CHAR_UUID    "6e5a0002-b5a3-f393-e0a9-e50e24dcca9e"  // app -> device
#define STATUS_CHAR_UUID "6e5a0003-b5a3-f393-e0a9-e50e24dcca9e"  // device -> app

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

BLECharacteristic *statusChar = nullptr;
BLEServer         *bleServer  = nullptr;
bool deviceConnected = false;

// -------------------------------------------------------- pulse  +  HRV ----

const uint8_t IBI_BUFFER = 12;
uint16_t ibiBuf[IBI_BUFFER] = {0};
uint8_t  ibiCount = 0, ibiIndex = 0;

uint32_t lastBeatMs     = 0;
int      signalBaseline = 2048;        // 12-bit ADC midpoint on ESP32
int      signalPeak     = 2048;
bool     aboveThreshold = false;
float    currentBPM     = 0;
float    currentRMSSD   = 0;

void recordBeat(uint32_t nowMs) {
  if (lastBeatMs == 0) { lastBeatMs = nowMs; return; }
  uint32_t ibi = nowMs - lastBeatMs;
  lastBeatMs = nowMs;

  if (ibi < 300 || ibi > 2000) return;      // reject artifacts (200 / 30 bpm)

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
  int raw = analogRead(PULSE_PIN);          // 0..4095 on ESP32

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
// The ONLY function that touches the output stage. If your LT1637 board needs
// a digital enable rather than a proportional level, change this body and
// nothing else.

void writeDuty(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(OUTPUT_PIN, duty);          // core 3.x addresses the pin
#else
  ledcWrite(PWM_CHANNEL, duty);         // core 2.x addresses the channel
#endif

#if LED_MIRROR_PIN >= 0
  // Same duty on the LED. Purely cosmetic — it lets you watch the 1.5 s ramp
  // as a fade before the output stage exists.
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(LED_MIRROR_PIN, duty);
  #else
    ledcWrite(LED_CHANNEL, duty);
  #endif
#endif
}

void applyOutput(uint8_t percent) {
  if (percent > MAX_INTENSITY) percent = MAX_INTENSITY;
  liveIntensity = percent;
  writeDuty((uint8_t)((uint32_t)percent * 255 / 100));
}

void stopOutputNow() {
  writeDuty(0);
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
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"intensity\":%u,\"target\":%u,\"remaining\":%u,"
    "\"bpm\":%.1f,\"rmssd\":%.1f,\"batt\":255}",
    stateName(), liveIntensity, targetIntensity, remainingSeconds(),
    currentBPM, currentRMSSD);

  if (deviceConnected && statusChar) {
    statusChar->setValue((uint8_t *)buf, strlen(buf));
    statusChar->notify();
  }
  Serial.println(buf);
}

// -------------------------------------------------------------- commands ---
//   START:<intensity>:<minutes>
//   STOP
//   SET:<intensity>

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

// ------------------------------------------------------------- callbacks ---

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    deviceConnected = true;
    Serial.println("[ble] app connected");
  }
  void onDisconnect(BLEServer *s) override {
    deviceConnected = false;
    Serial.println("[ble] app disconnected -> stopping session");
    stopSession();                 // fail safe: phone walks away, output ends
    s->startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    // ESP32 core 3.x returns an Arduino String here; core 2.x returned
    // std::string. Both are handled so the sketch builds either way.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    String v = c->getValue();
    if (v.length() == 0) return;
    handleCommand(v);
#else
    std::string v = c->getValue();
    if (v.empty()) return;
    handleCommand(String(v.c_str()));
#endif
  }
};

// ------------------------------------------------------------------ main ---

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nAlphaX standalone starting");

  // PWM output
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(OUTPUT_PIN, PWM_FREQ, PWM_RES_BITS);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(OUTPUT_PIN, PWM_CHANNEL);
#endif
#if LED_MIRROR_PIN >= 0
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(LED_MIRROR_PIN, PWM_FREQ, PWM_RES_BITS);
  #else
    ledcSetup(LED_CHANNEL, PWM_FREQ, PWM_RES_BITS);
    ledcAttachPin(LED_MIRROR_PIN, LED_CHANNEL);
  #endif
#endif

  stopOutputNow();

  analogReadResolution(12);       // 0..4095, matches signalBaseline above

  // BLE. setMTU must come before init — without it notifications are capped
  // at 20 bytes and the status JSON arrives truncated and unparseable.
  BLEDevice::setMTU(247);
  BLEDevice::init(DEVICE_NAME);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  BLEService *service = bleServer->createService(SERVICE_UUID);

  BLECharacteristic *cmdChar = service->createCharacteristic(
      CMD_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  cmdChar->setCallbacks(new CommandCallbacks());

  statusChar = service->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusChar->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.printf("Advertising as %s\n", DEVICE_NAME);
}

void loop() {
  uint32_t now = millis();

  // Typed commands over USB serial, so you can run a session with no phone.
  static String rxLine = "";
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') { handleCommand(rxLine); rxLine = ""; }
    else { rxLine += ch; if (rxLine.length() > 120) rxLine = ""; }
  }

  if (now - lastSampleMs >= SAMPLE_PERIOD_MS) { lastSampleMs = now; samplePulse(); }

  updateSession();

  if (now - lastStatusMs >= STATUS_PERIOD_MS) { lastStatusMs = now; sendStatus(); }
}
