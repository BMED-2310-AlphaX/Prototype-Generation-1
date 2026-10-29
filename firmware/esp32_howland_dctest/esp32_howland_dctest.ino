/*
 * AlphaX — ESP32 Howland DC bench test
 * Team AlphaX, BMED 2310
 *
 * WHAT THIS IS FOR
 *   Proves the whole control chain — phone -> Bluetooth -> firmware -> DAC ->
 *   Howland -> measurable current — using a steady DC level instead of the
 *   biphasic pulse train. Everything is verified except the waveform shape.
 *
 *   Same BLE protocol, same app, same commands as the production sketch. The
 *   only change is the output stage: GPIO25's DAC drives a DC voltage into
 *   the Howland's vin, instead of PWM driving a transducer.
 *
 * ############  DUMMY LOAD ONLY. NEVER A PERSON.  ############
 *
 *   This sketch delivers DIRECT CURRENT. DC through skin electrodes causes
 *   electrochemical burns and is exactly what the biphasic waveform and the
 *   blocking capacitor exist to prevent. It is safe here only because the
 *   load is a resistor on a bench.
 *
 *   Build the dummy load from the tissue model in your LTspice file:
 *
 *       drv ---[ 10k || 470nF ]---[ 500R ]---[ 10k || 470nF ]--- GND
 *
 *   Measure current as the voltage across the 500R and divide by 500.
 *
 * WIRING
 *   GPIO25 (DAC1) ---[ 10k ]---+--- Howland vin
 *                              |
 *                            [ 2k ]
 *                              |
 *                             GND
 *
 *   The 10k/2k divider scales the DAC's 0-3.3 V down to 0-0.55 V, which the
 *   Howland converts to 0-5.5 mA. It is also a HARDWARE ceiling: even a
 *   firmware fault commanding full scale cannot exceed ~5.5 mA. Without the
 *   divider, 3.3 V into vin would command 33 mA.
 *
 *   ESP32 GND must tie to the Howland's ground. The op-amp supply rails stay
 *   entirely away from the ESP32.
 *
 * CHECK BEFORE CONNECTING
 *   Set MAX_DAC low, run a session, and measure GPIO25 with a multimeter
 *   BEFORE wiring it to the Howland. Confirm the voltage tracks the app's
 *   intensity slider. Only then connect vin.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------------------------------------------------------- config ---

#define DEVICE_NAME  "AlphaX-01"

#define DAC_PIN      25      // DAC1 -> divider -> Howland vin
#define PULSE_PIN    34      // ADC1, input-only — safe with BLE running

// Full-scale DAC code allowed. 255 = 3.3 V out of the pin, which the 10k/2k
// divider turns into 0.55 V at vin, i.e. ~5.5 mA through the Howland.
//
// If you have NOT built the divider, set this to 39. That caps the pin at
// about 0.5 V, giving ~5 mA, at the cost of using only 39 of 256 levels.
#define MAX_DAC      255

// Mirrors output level onto the onboard LED so you can see the ramp without
// a meter. Set to -1 to disable.
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

void writeDuty(uint8_t level) {
  // level is 0..255 of the ALLOWED range, not of the DAC's full range.
  uint8_t code = (uint8_t)((uint32_t)level * MAX_DAC / 255);
  dacWrite(DAC_PIN, code);

#if LED_MIRROR_PIN >= 0
  // Cosmetic only — watch the ramp without a meter.
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(LED_MIRROR_PIN, level);
  #else
    ledcWrite(LED_CHANNEL, level);
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
  Serial.println("\nAlphaX Howland DC bench test");
  Serial.println("DUMMY LOAD ONLY — this output is DC.");
  Serial.printf("MAX_DAC = %d  (%.2f V at the pin, full scale)\n",
                MAX_DAC, MAX_DAC * 3.3 / 255.0);

  // DAC needs no setup on the ESP32 — dacWrite() is enough.
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
  Serial.println("Measure GPIO25 with a meter BEFORE wiring to vin.");
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
