#include <Arduino.h>
#include "esp_timer.h"
#include "soc/soc.h"
#include "soc/rtc_io_reg.h"
#include "soc/gpio_reg.h"
#include "stim.h"

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Needs the esp32 Arduino core 3.x (Tools > Board > Boards Manager > esp32 by Espressif)"
#endif
#if !CONFIG_IDF_TARGET_ESP32
#error "Built-in DAC on GPIO25/26 needs the original ESP32 (board: ESP32 Dev Module / DOIT ESP32 DEVKIT V1)"
#endif

struct seg_t { uint8_t code; uint32_t dur_us; };

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static hw_timer_t *tmr;
static int pin_en_ = -1;

static stim_params_t active, pending;
static volatile bool pending_dirty;
static volatile stim_state_t state = STIM_OFF;
static volatile bool stop_req;
static volatile uint32_t cur_amp_uA;
static volatile uint32_t last_period_ms;
static uint32_t vref_code = DAC_MID;
static seg_t seg[4];
static volatile uint32_t n_seg, q;
static volatile uint32_t out_pos = DAC_MID, out_neg = DAC_MID;

// Direct register write: fast and safe inside the ISR. dacWrite() is used once at
// init to set up the pads.
static inline void IRAM_ATTR dac1_set(uint32_t v) { REG_SET_FIELD(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_DAC, v); }
static inline void IRAM_ATTR dac2_set(uint32_t v) { REG_SET_FIELD(RTC_IO_PAD_DAC2_REG, RTC_IO_PDAC2_DAC, v); }
static inline void IRAM_ATTR en_write(bool on)
{
  if (pin_en_ < 0 || pin_en_ > 31) return;
  REG_WRITE(on ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, 1u << pin_en_);
}
static inline uint32_t IRAM_ATTR now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

static uint32_t amp_to_delta(uint32_t amp_uA, const stim_params_t *p)
{
  return (amp_uA * 256u + p->gain_fs_uA / 2u) / p->gain_fs_uA;
}

uint32_t stim_amp_limit_uA(const stim_params_t *p)
{
  uint32_t up = DAC_CODE_MAX - p->zero_code;
  uint32_t dn = p->zero_code - DAC_CODE_MIN;
  uint32_t d = up < dn ? up : dn;
  uint32_t lim = d * p->gain_fs_uA / 256u;
  return lim < STIM_AMP_MAX_uA ? lim : STIM_AMP_MAX_uA;
}

static const char *validate(const stim_params_t *p)
{
  if (p->freq_hz < STIM_FREQ_MIN_HZ || p->freq_hz > STIM_FREQ_MAX_HZ) return "freq out of range (1-100 Hz)";
  if (p->pw_us < STIM_PW_MIN_us || p->pw_us > STIM_PW_MAX_us) return "pw out of range (50-500 us)";
  if (p->ipg_us > STIM_IPG_MAX_us || (p->ipg_us != 0 && p->ipg_us < STIM_SEG_MIN_us)) return "ipg out of range (0 or 20-200 us)";
  if (p->ramp_uA_s < STIM_RAMP_MIN_uA_s || p->ramp_uA_s > STIM_RAMP_MAX_uA_s) return "ramp out of range (100-20000 uA/s)";
  if (p->zero_code < DAC_MID - DAC_TRIM_RANGE || p->zero_code > DAC_MID + DAC_TRIM_RANGE) return "zero code out of range (108-148)";
  if (p->gain_fs_uA < 20000u || p->gain_fs_uA > 50000u) return "gain out of range (20000-50000)";
  if (2u * p->pw_us + p->ipg_us + 100u > 1000000u / p->freq_hz) return "pulse does not fit in the period";
  if (p->amp_uA > stim_amp_limit_uA(p)) return "amp above limit (see status)";
  return nullptr;
}

// Build one full period. Amplitude is fixed for the whole period, so the + and -
// phases always match.
static void IRAM_ATTR build_period()
{
  if (pending_dirty) { active = pending; pending_dirty = false; }

  uint32_t a = cur_amp_uA;
  if (a >= active.amp_uA) {
    a = active.amp_uA;
  } else {
    uint32_t step = active.ramp_uA_s / active.freq_hz;
    if (step == 0) step = 1;
    a = (a + step < active.amp_uA) ? a + step : active.amp_uA;
  }
  cur_amp_uA = a;

  uint32_t z = active.zero_code;
  uint32_t d = (a * 256u + active.gain_fs_uA / 2u) / active.gain_fs_uA;
  uint32_t pos = z + d, neg = z - d;
  if (pos > DAC_CODE_MAX || neg < DAC_CODE_MIN) { pos = z; neg = z; }
  out_pos = pos; out_neg = neg;

  uint32_t period = 1000000u / active.freq_hz;
  uint32_t n = 0;
  seg[n++] = { (uint8_t)pos, active.pw_us };
  if (active.ipg_us) seg[n++] = { (uint8_t)z, active.ipg_us };
  seg[n++] = { (uint8_t)neg, active.pw_us };
  seg[n++] = { (uint8_t)z, period - 2u * active.pw_us - active.ipg_us };
  n_seg = n;
  last_period_ms = now_ms();
}

// Fires when the current phase ends. The timer has already reloaded to 0 in
// hardware, so the next phase is timed from the exact alarm instant.
static void IRAM_ATTR on_alarm()
{
  bool halt = false;
  uint32_t dur = 0;

  portENTER_CRITICAL_ISR(&mux);
  uint32_t next = q + 1u;
  if (next >= n_seg && (stop_req || state != STIM_RUNNING)) {
    // Rest phase just started (output already at zero): stop here.
    dac1_set(active.zero_code);
    en_write(false);
    cur_amp_uA = 0;
    out_pos = out_neg = active.zero_code;
    if (state != STIM_FAULT) state = STIM_OFF;
    halt = true;
  } else {
    if (next >= n_seg) { build_period(); next = 0; }
    q = next;
    dac1_set(seg[next].code);
    dur = seg[next].dur_us;
  }
  portEXIT_CRITICAL_ISR(&mux);

  if (halt) timerStop(tmr);
  else timerAlarm(tmr, dur, true, 0);
}

void stim_init(int pin_vin, int pin_vref, int pin_en)
{
  pin_en_ = pin_en;
  if (pin_en_ >= 0) { digitalWrite(pin_en_, LOW); pinMode(pin_en_, OUTPUT); digitalWrite(pin_en_, LOW); }

  pending = { 5000, 25, 250, 50, 2500, DAC_MID, STIM_GAIN_FS_DEFAULT_uA };
  active = pending;

  dacWrite(pin_vref, vref_code);       // configures the DAC pads
  dacWrite(pin_vin, active.zero_code);
  dac2_set(vref_code);
  dac1_set(active.zero_code);

  tmr = timerBegin(1000000);           // 1 us ticks
  timerStop(tmr);
  timerAttachInterrupt(tmr, &on_alarm);
}

const char *stim_set(const stim_params_t *p)
{
  const char *err = validate(p);
  if (err) return err;
  portENTER_CRITICAL(&mux);
  pending = *p;
  pending_dirty = true;
  if (cur_amp_uA > p->amp_uA) cur_amp_uA = p->amp_uA;   // decreases apply next period
  if (state == STIM_OFF || state == STIM_FAULT) {
    active = *p;
    pending_dirty = false;
    dac1_set(active.zero_code);
  }
  portEXIT_CRITICAL(&mux);
  return nullptr;
}

void stim_get(stim_params_t *p)
{
  portENTER_CRITICAL(&mux);
  *p = pending_dirty ? pending : active;
  portEXIT_CRITICAL(&mux);
}

bool stim_start()
{
  if (state != STIM_OFF && state != STIM_FAULT) return false;
  if (validate(&active)) return false;

  timerStop(tmr);
  portENTER_CRITICAL(&mux);
  cur_amp_uA = 0;
  stop_req = false;
  build_period();
  q = 0;
  dac1_set(seg[0].code);
  state = STIM_RUNNING;
  portEXIT_CRITICAL(&mux);

  en_write(true);
  timerWrite(tmr, 0);
  timerAlarm(tmr, seg[0].dur_us, true, 0);
  timerStart(tmr);
  return true;
}

void stim_stop()
{
  portENTER_CRITICAL(&mux);
  if (state == STIM_RUNNING) { stop_req = true; state = STIM_STOPPING; }
  portEXIT_CRITICAL(&mux);
}

void stim_estop(bool fault)
{
  timerStop(tmr);
  portENTER_CRITICAL(&mux);
  en_write(false);
  dac1_set(active.zero_code);
  cur_amp_uA = 0;
  stop_req = false;
  state = fault ? STIM_FAULT : STIM_OFF;
  out_pos = out_neg = active.zero_code;
  portEXIT_CRITICAL(&mux);
}

void stim_set_vref(uint32_t code) { vref_code = code; dac2_set(code); }
uint32_t stim_get_vref() { return vref_code; }
stim_state_t stim_state() { return state; }

uint32_t stim_current_amp_uA()
{
  if (state != STIM_RUNNING && state != STIM_STOPPING) return 0;
  return (out_pos - active.zero_code) * active.gain_fs_uA / 256u;
}

void stim_output_codes(uint32_t *pos, uint32_t *neg) { *pos = out_pos; *neg = out_neg; }

void stim_check_alive()
{
  if (state != STIM_RUNNING && state != STIM_STOPPING) return;
  uint32_t period_ms = 1000u / active.freq_hz;
  if (now_ms() - last_period_ms > period_ms + 100u) stim_estop(true);
}
