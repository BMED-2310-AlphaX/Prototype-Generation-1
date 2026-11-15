// tVNS stimulator control firmware - ESP32 DevKit V1 (ESP32-WROOM-32)
//
// Arduino IDE: board "ESP32 Dev Module" (or "DOIT ESP32 DEVKIT V1"),
// esp32 core by Espressif 3.x. Serial Monitor 115200, line ending "Newline" or "Both".
//
// Wiring (see WIRING.txt):
//   GPIO25 (DAC1) -> free end of R3      (Howland Vin)
//   GPIO26 (DAC2) -> free end of R1      (Howland Vref, 1.65 V)
//   GND           -> battery midpoint    (circuit GND)
//   GPIO27        -> optional enable input on the analog side (high = allowed)
// BOOT button (GPIO0) = emergency stop. Blue LED (GPIO2) = stimulating.
// Stimulation is OFF at boot.

#include "esp_task_wdt.h"
#include "stim.h"

static const int PIN_VIN    = 25;
static const int PIN_VREF   = 26;
static const int PIN_EN     = 27;
static const int PIN_BUTTON = 0;
static const int PIN_LED    = 2;

static bool button_pressed() { return digitalRead(PIN_BUTTON) == LOW; }

static const char *state_name(stim_state_t s)
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
  Serial.printf("state      %s\n", state_name(stim_state()));
  Serial.printf("amp        %lu uA (target)\n", (unsigned long)p.amp_uA);
  Serial.printf("amp now    %lu uA (after ramp and 8-bit DAC rounding, ~129 uA steps)\n", (unsigned long)stim_current_amp_uA());
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
  Serial.print("on | off | stop (immediate) | status\n"
               "amp <uA>  freq <Hz>  pw <us>  ipg <us>  ramp <uA/s>\n"
               "zero <code>  vref <code>  gain <uA>   (calibration; codes 108-148, mid 128)\n"
               "Changes while running take effect at the next period; amplitude increases ramp.\n");
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
  char *arg = strchr(line, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
  uint32_t v = 0;
  bool has_v = arg && parse_u32(arg, &v);

  if (!strcmp(line, "")) return;
  if (!strcmp(line, "help"))   { print_help(); return; }
  if (!strcmp(line, "status")) { print_status(); return; }
  if (!strcmp(line, "stop"))   { stim_estop(false); Serial.println("stopped"); return; }
  if (!strcmp(line, "off"))    { stim_stop(); Serial.println("stopping after current pulse"); return; }
  if (!strcmp(line, "on")) {
    if (button_pressed()) { Serial.println("refused: button held"); return; }
    Serial.println(stim_start() ? "running (ramping up)" : "refused: already running or bad parameters");
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
}

void setup()
{
  // Output to zero first: after a crash/software reset the DAC keeps its last value
  // until this runs.
  stim_init(PIN_VIN, PIN_VREF, PIN_EN);

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);

  // Task watchdog on loop(): ~0.5 s, panics and reboots if loop() hangs.
  esp_task_wdt_config_t wdt = { .timeout_ms = 500, .idle_core_mask = 0, .trigger_panic = true };
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(NULL);

  esp_reset_reason_t rr = esp_reset_reason();
  Serial.println("\ntVNS stimulator (ESP32) ready. Output OFF. Type help.");
  if (rr == ESP_RST_PANIC || rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT)
    Serial.println("WARNING: last reset was a crash or watchdog");
  Serial.print("> ");
}

void loop()
{
  esp_task_wdt_reset();

  stim_state_t s = stim_state();
  if (button_pressed() && (s == STIM_RUNNING || s == STIM_STOPPING)) {
    stim_estop(false);
    Serial.println("\nE-STOP (button)");
  }
  stim_check_alive();

  static bool fault_reported;
  s = stim_state();
  if (s == STIM_FAULT) {
    if (!fault_reported) { Serial.println("\nFAULT: stimulation timer stalled, output forced to zero"); fault_reported = true; }
  } else {
    fault_reported = false;
  }
  digitalWrite(PIN_LED, (s == STIM_RUNNING || s == STIM_STOPPING) ? HIGH : LOW);

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
