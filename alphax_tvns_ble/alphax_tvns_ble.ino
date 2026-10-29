/*
 * AlphaX — tVNS stimulator with Bluetooth control
 * Team AlphaX, BMED 2310
 *
 * Merges two pieces of work:
 *   stim.cpp / stim.h   Max's biphasic stimulation engine (unchanged)
 *   this file           Joy's BLE service and session logic
 *
 * The stimulation engine owns the waveform, the amplitude ramp, the parameter
 * limits, the e-stop and the timer watchdog. This file adds a radio and a
 * session clock on top. Nothing here reaches the DAC directly — every output
 * change goes through stim_set() / stim_start() / stim_stop(), so the engine's
 * safety checks cannot be bypassed by a bad command from the phone.
 *
 * WIRING  (from Max's WIRING.txt — the circuit itself does not change)
 *   GPIO25 (DAC1) -> free end of R3        Howland Vin
 *   GPIO26 (DAC2) -> free end of R1        Howland Vref, lifted off GND
 *   GND           -> battery midpoint      circuit ground
 *   100k resistor across GPIO25 and GPIO26 (holds output near zero on reset)
 *   GPIO27        -> analog-side enable, if present
 *
 *   Do NOT connect +9 V / -9 V to the ESP32. Power it from USB.
 *   Order: USB in and booted -> batteries on -> "on". Shut down in reverse.
 *
 * ############  TEST INTO A DUMMY LOAD FIRST  ############
 *   drv --[ 10k || 470nF ]--[ 500R ]--[ 10k || 470nF ]-- GND
 *   Scope the current through the 500R before any electrode touches skin,
 *   and have the circuit reviewed by the instructor first.
 *
 * APP PROTOCOL  (unchanged, so the iOS app needs no modification)
 *   service 6E5A0001-..., command ...0002 (write), status ...0003 (notify)
 *   START:<intensity>:<minutes>   STOP   SET:<intensity>
 *   intensity is 0-80 percent of AMP_AT_MAX_INTENSITY_uA.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_task_wdt.h"
#include "stim.h"

// ---------------------------------------------------------------- config ---

#define DEVICE_NAME "AlphaX-01"

static const int PIN_VIN    = 25;   // DAC1 -> Howland Vin
static const int PIN_VREF   = 26;   // DAC2 -> Howland Vref
static const int PIN_EN     = 27;   // optional analog enable
static const int PIN_BUTTON = 0;    // BOOT button = emergency stop
static const int PIN_LED    = 2;    // on while stimulating

// Current delivered when the app's slider is at its 80% ceiling.
// 5 mA matches the LTspice design point. Raising this raises the dose at every
// slider position, so change it deliberately and re-check EA-2 current density.
#define AMP_AT_MAX_INTENSITY_uA 5000u

#define MAX_INTENSITY    80u        // app slider ceiling, percent
#define MAX_SESSION_MIN  60u        // session length ceiling, minutes
#define STATUS_PERIOD_MS 500u

// BLE UUIDs — must match BLEManager.swift exactly
#define SERVICE_UUID     "6e5a0001-b5a3-f393-e0a9-e50e24dcca9e"
#define CMD_CHAR_UUID    "6e5a0002-b5a3-f393-e0a9-e50e24dcca9e"
#define STATUS_CHAR_UUID "6e5a0003-b5a3-f393-e0a9-e50e24dcca9e"

// ----------------------------------------------------------------- state ---

BLECharacteristic *statusChar = nullptr;
BLEServer         *bleServer  = nullptr;
bool deviceConnected = false;

static uint32_t sessionStartMs = 0;
static uint32_t sessionMinutes = 20;
static uint8_t  targetPercent  = 40;
static bool     sessionActive  = false;
static bool     streamStatus   = false;   // mirror status JSON to the console
static uint32_t lastStatusMs   = 0;

// ------------------------------------------------------------ conversion ---
// The app speaks percent; the engine speaks microamps. One DAC code is about
// 129 uA, so the engine rounds to the nearest code and reports what it
// actually set — which is why status reports measured amplitude, not commanded.

static uint32_t percentToAmp(uint32_t pct)
{
  if (pct > MAX_INTENSITY) pct = MAX_INTENSITY;
  return pct * AMP_AT_MAX_INTENSITY_uA / MAX_INTENSITY;
}

static uint32_t ampToPercent(uint32_t uA)
{
  return uA * MAX_INTENSITY / AMP_AT_MAX_INTENSITY_uA;
}

// -------------------------------------------------------------- sessions ---

static uint32_t remainingSeconds()
{
  if (!sessionActive) return 0;
  uint32_t total = sessionMinutes * 60000u;
  uint32_t el    = millis() - sessionStartMs;
  return (el >= total) ? 0 : (total - el) / 1000u;
}

// The app's state string is derived from the engine rather than tracked
// separately, so the two can never disagree about what the hardware is doing.
static const char *stateName()
{
  switch (stim_state()) {
    case STIM_RUNNING:
      // One DAC code of tolerance, else rounding leaves it "ramping" forever.
      return (stim_current_amp_uA() + 150u < percentToAmp(targetPercent))
             ? "ramping_up" : "running";
    case STIM_STOPPING: return "ramping_down";
    case STIM_FAULT:    return "idle";
    default:            return "idle";
  }
}

static bool startSession(uint32_t intensity, uint32_t minutes)
{
  if (intensity > MAX_INTENSITY) intensity = MAX_INTENSITY;
  if (minutes == 0 || minutes > MAX_SESSION_MIN) minutes = 20;

  if (digitalRead(PIN_BUTTON) == LOW) {
    Serial.println("refused: e-stop button held");
    return false;
  }

  stim_params_t p;
  stim_get(&p);
  p.amp_uA = percentToAmp(intensity);

  const char *err = stim_set(&p);
  if (err) { Serial.printf("refused: %s\n", err); return false; }
  if (!stim_start()) { Serial.println("refused: already running or bad params"); return false; }

  targetPercent  = intensity;
  sessionMinutes = minutes;
  sessionStartMs = millis();
  sessionActive  = true;
  Serial.printf("session start: %lu%% (%lu uA) for %lu min\n",
                (unsigned long)intensity, (unsigned long)p.amp_uA,
                (unsigned long)minutes);
  return true;
}

static void endSession(const char *why)
{
  stim_stop();                  // graceful: finishes the current biphasic pair
  sessionActive = false;
  Serial.printf("session end: %s\n", why);
}

static void setIntensity(uint32_t intensity)
{
  if (intensity > MAX_INTENSITY) intensity = MAX_INTENSITY;
  stim_params_t p;
  stim_get(&p);
  p.amp_uA = percentToAmp(intensity);
  const char *err = stim_set(&p);
  if (err) { Serial.printf("refused: %s\n", err); return; }
  targetPercent = intensity;
}

// ---------------------------------------------------------------- status ---

static void sendStatus()
{
  uint32_t liveAmp = stim_current_amp_uA();

  char buf[220];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"intensity\":%lu,\"target\":%lu,\"remaining\":%lu,"
    "\"bpm\":0.0,\"rmssd\":0.0,\"batt\":255,\"amp_uA\":%lu}",
    stateName(),
    (unsigned long)ampToPercent(liveAmp),
    (unsigned long)targetPercent,
    (unsigned long)remainingSeconds(),
    (unsigned long)liveAmp);

  if (deviceConnected && statusChar) {
    statusChar->setValue((uint8_t *)buf, strlen(buf));
    statusChar->notify();
  }
  if (streamStatus) Serial.println(buf);
}

// -------------------------------------------------------- app commands -----

static void handleAppCommand(String cmd)
{
  cmd.trim();
  if (cmd.length() == 0) return;
  Serial.printf("[ble] %s\n", cmd.c_str());

  if (cmd.startsWith("START")) {
    int c1 = cmd.indexOf(':');
    int c2 = cmd.indexOf(':', c1 + 1);
    uint32_t inten = (c1 > 0) ? cmd.substring(c1 + 1, c2 > 0 ? c2 : cmd.length()).toInt() : 40;
    uint32_t mins  = (c2 > 0) ? cmd.substring(c2 + 1).toInt() : 20;
    startSession(inten, mins);

  } else if (cmd.startsWith("STOP")) {
    endSession("app");

  } else if (cmd.startsWith("SET")) {
    int c1 = cmd.indexOf(':');
    if (c1 > 0) setIntensity(cmd.substring(c1 + 1).toInt());
  }
  sendStatus();
}

// ------------------------------------------------------------- callbacks ---

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    deviceConnected = true;
    Serial.println("\n[ble] app connected");
  }
  void onDisconnect(BLEServer *s) override {
    deviceConnected = false;
    Serial.println("\n[ble] app disconnected");
    // Fail safe: the phone going away ends the session.
    if (sessionActive) endSession("link lost");
    s->startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    handleAppCommand(c->getValue());
#else
    std::string v = c->getValue();
    if (v.empty()) return;
    handleAppCommand(String(v.c_str()));
#endif
  }
};

// ------------------------------------------------------- serial console ----
// Max's console, kept intact and extended. Useful for bench work without a
// phone, and for calibrating zero / vref / gain against a meter.

static bool button_pressed() { return digitalRead(PIN_BUTTON) == LOW; }

static const char *engine_state_name(stim_state_t s)
{
  switch (s) {
    case STIM_RUNNING:  return "RUNNING";
    case STIM_STOPPING: return "STOPPING";
    case STIM_FAULT:    return "FAULT (timer stalled; output forced to zero)";
    default:            return "OFF";
  }
}

static void print_status()
{
  stim_params_t p; stim_get(&p);
  uint32_t pos, neg; stim_output_codes(&pos, &neg);
  Serial.printf("state      %s\n", engine_state_name(stim_state()));
  Serial.printf("app state  %s\n", stateName());
  Serial.printf("bluetooth  %s\n", deviceConnected ? "connected" : "advertising");
  Serial.printf("session    %s", sessionActive ? "active, " : "idle\n");
  if (sessionActive) Serial.printf("%lu s remaining\n", (unsigned long)remainingSeconds());
  Serial.printf("intensity  %lu%% target -> %lu uA\n",
                (unsigned long)targetPercent, (unsigned long)percentToAmp(targetPercent));
  Serial.printf("amp now    %lu uA (after ramp and 8-bit DAC rounding, ~129 uA steps)\n",
                (unsigned long)stim_current_amp_uA());
  Serial.printf("amp limit  %lu uA\n", (unsigned long)stim_amp_limit_uA(&p));
  Serial.printf("freq       %lu Hz\n", (unsigned long)p.freq_hz);
  Serial.printf("pw         %lu us per phase\n", (unsigned long)p.pw_us);
  Serial.printf("ipg        %lu us\n", (unsigned long)p.ipg_us);
  Serial.printf("ramp       %lu uA/s\n", (unsigned long)p.ramp_uA_s);
  Serial.printf("zero code  %lu\n", (unsigned long)p.zero_code);
  Serial.printf("vref code  %lu\n", (unsigned long)stim_get_vref());
  Serial.printf("gain       %lu uA full scale\n", (unsigned long)p.gain_fs_uA);
  Serial.printf("codes +/-  %lu / %lu\n", (unsigned long)pos, (unsigned long)neg);
  Serial.printf("button     %s\n", button_pressed() ? "PRESSED" : "released");
}

static void print_help()
{
  Serial.print("on | off | stop (immediate) | status | stream on|off\n"
               "amp <uA>  freq <Hz>  pw <us>  ipg <us>  ramp <uA/s>\n"
               "zero <code>  vref <code>  gain <uA>   (calibration; codes 108-148, mid 128)\n"
               "App commands also work here: START:<pct>:<min>  SET:<pct>  STOP\n");
}

static bool parse_u32(const char *s, uint32_t *out)
{
  uint32_t v = 0;
  if (!*s) return false;
  for (; *s; s++) {
    if (*s < '0' || *s > '9' || v > 400000000u) return false;
    v = v * 10u + (uint32_t)(*s - '0');
  }
  *out = v;
  return true;
}

static void handle_line(char *line)
{
  // App-style commands work from the console too, so the whole chain can be
  // exercised before the phone is involved.
  if (!strncmp(line, "START", 5) || !strncmp(line, "SET", 3) || !strcmp(line, "STOP")) {
    handleAppCommand(String(line));
    return;
  }

  char *arg = strchr(line, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
  uint32_t v = 0;
  bool has_v = arg && parse_u32(arg, &v);

  if (!strcmp(line, ""))       return;
  if (!strcmp(line, "help"))   { print_help(); return; }
  if (!strcmp(line, "status")) { print_status(); return; }
  if (!strcmp(line, "stop"))   { stim_estop(false); sessionActive = false; Serial.println("stopped"); return; }
  if (!strcmp(line, "off"))    { endSession("console"); return; }
  if (!strcmp(line, "on")) {
    startSession(targetPercent, sessionMinutes);
    return;
  }
  if (!strcmp(line, "stream")) {
    streamStatus = arg && !strcmp(arg, "on");
    Serial.println(streamStatus ? "streaming on" : "streaming off");
    return;
  }
  if (!strcmp(line, "vref")) {
    if (!has_v || v < DAC_MID - DAC_TRIM_RANGE || v > DAC_MID + DAC_TRIM_RANGE) { Serial.println("vref 108-148"); return; }
    if (stim_state() == STIM_RUNNING || stim_state() == STIM_STOPPING) { Serial.println("turn off first"); return; }
    stim_set_vref(v); Serial.println("ok"); return;
  }

  stim_params_t p; stim_get(&p);
  uint32_t *field = nullptr;
  if      (!strcmp(line, "amp"))  field = &p.amp_uA;
  else if (!strcmp(line, "freq")) field = &p.freq_hz;
  else if (!strcmp(line, "pw"))   field = &p.pw_us;
  else if (!strcmp(line, "ipg"))  field = &p.ipg_us;
  else if (!strcmp(line, "ramp")) field = &p.ramp_uA_s;
  else if (!strcmp(line, "zero")) field = &p.zero_code;
  else if (!strcmp(line, "gain")) field = &p.gain_fs_uA;
  if (!field) { Serial.println("unknown command, try help"); return; }
  if (!has_v) { Serial.println("needs a number"); return; }
  *field = v;
  const char *err = stim_set(&p);
  Serial.println(err ? err : "ok");
  if (field == &p.amp_uA) targetPercent = ampToPercent(p.amp_uA);
}

// ------------------------------------------------------------------ main ---

void setup()
{
  // Output to zero first: after a crash the DAC holds its last value until
  // this runs.
  stim_init(PIN_VIN, PIN_VREF, PIN_EN);

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);
  delay(200);

  // BLE comes up before the watchdog is armed — init takes longer than the
  // 500 ms loop timeout would allow.
  BLEDevice::setMTU(247);          // must precede init, or notifications truncate
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

  esp_task_wdt_config_t wdt = { .timeout_ms = 500, .idle_core_mask = 0, .trigger_panic = true };
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(NULL);

  esp_reset_reason_t rr = esp_reset_reason();
  Serial.println("\nAlphaX tVNS stimulator ready. Output OFF. Type help.");
  Serial.printf("Advertising as %s\n", DEVICE_NAME);
  Serial.printf("App 80%% = %lu uA\n", (unsigned long)AMP_AT_MAX_INTENSITY_uA);
  if (rr == ESP_RST_PANIC || rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT)
    Serial.println("WARNING: last reset was a crash or watchdog");
  Serial.print("> ");
}

void loop()
{
  esp_task_wdt_reset();

  // Emergency stop takes priority over everything, including the app.
  stim_state_t s = stim_state();
  if (button_pressed() && (s == STIM_RUNNING || s == STIM_STOPPING)) {
    stim_estop(false);
    sessionActive = false;
    Serial.println("\nE-STOP (button)");
  }
  stim_check_alive();

  static bool fault_reported;
  s = stim_state();
  if (s == STIM_FAULT) {
    if (!fault_reported) {
      Serial.println("\nFAULT: stimulation timer stalled, output forced to zero");
      fault_reported = true;
      sessionActive = false;
    }
  } else {
    fault_reported = false;
  }
  digitalWrite(PIN_LED, (s == STIM_RUNNING || s == STIM_STOPPING) ? HIGH : LOW);

  // Session duration is owned here; the engine runs until told to stop.
  if (sessionActive && remainingSeconds() == 0) endSession("duration elapsed");

  uint32_t now = millis();
  if (now - lastStatusMs >= STATUS_PERIOD_MS) { lastStatusMs = now; sendStatus(); }

  static char line[48];
  static uint32_t len;
  static char prev;
  while (Serial.available()) {
    char c = (char)Serial.read();
    bool crlf = (c == '\n' && prev == '\r');
    prev = c;
    if (crlf) continue;
    if (c == '\r' || c == '\n') {
      Serial.println();
      line[len] = 0;
      handle_line(line);
      len = 0;
      Serial.print("> ");
    } else if ((c == '\b' || c == 0x7F) && len) {
      len--; Serial.print("\b \b");
    } else if (c >= ' ' && len < sizeof line - 1) {
      line[len++] = c; Serial.print(c);
    }
  }
}
