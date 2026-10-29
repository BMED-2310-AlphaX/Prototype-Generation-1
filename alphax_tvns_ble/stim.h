// tVNS biphasic stimulation engine for ESP32 (DevKit V1, built-in 8-bit DAC).
//
// Output: DAC1 (GPIO25) -> Howland pump Vin (R3).
//         DAC2 (GPIO26) -> Howland Vref (R1), held at mid-scale.
// Iout = (V_GPIO25 - V_GPIO26) * (1/Rsense + 1/Rset) ~= 10.01 mA/V.
// One DAC step = 3.3 V / 256 = 12.9 mV = ~129 uA of output current.
//
// One period: +Iamp for PW, 0 for IPG, -Iamp for PW, 0 for the rest of 1/Freq.
// A 1 MHz hardware timer times each phase; the timer reloads in hardware so phase
// lengths are exact, and the DAC is written at the start of each phase in the ISR
// (a few us of fixed latency, ~1 us jitter).
#pragma once
#include <stdint.h>

#define STIM_AMP_MAX_uA     15000u
#define STIM_FREQ_MIN_HZ    1u
#define STIM_FREQ_MAX_HZ    100u
#define STIM_PW_MIN_us      50u
#define STIM_PW_MAX_us      500u
#define STIM_IPG_MAX_us     200u
#define STIM_SEG_MIN_us     20u     // shortest phase; ISR needs time to reload
#define STIM_RAMP_MIN_uA_s  100u
#define STIM_RAMP_MAX_uA_s  20000u

// ESP32 DAC is only linear between roughly 0.1 V and 3.2 V
#define DAC_CODE_MIN        10u
#define DAC_CODE_MAX        245u
#define DAC_MID             128u
#define DAC_TRIM_RANGE      20u

// Full-scale current for 256 DAC codes, in uA: 3.3 V * (1/100 + 1/100k) S
#define STIM_GAIN_FS_DEFAULT_uA 33033u

struct stim_params_t {
  uint32_t amp_uA;
  uint32_t freq_hz;
  uint32_t pw_us;
  uint32_t ipg_us;
  uint32_t ramp_uA_s;
  uint32_t zero_code;
  uint32_t gain_fs_uA;
};

enum stim_state_t { STIM_OFF = 0, STIM_RUNNING, STIM_STOPPING, STIM_FAULT };

void stim_init(int pin_vin, int pin_vref, int pin_en);
const char *stim_set(const stim_params_t *p);   // nullptr on success
void stim_get(stim_params_t *p);
bool stim_start();
void stim_stop();                 // graceful: finish the current biphasic pulse
void stim_estop(bool fault);      // immediate: output to zero now
void stim_set_vref(uint32_t code);
uint32_t stim_get_vref();
stim_state_t stim_state();
uint32_t stim_current_amp_uA();
uint32_t stim_amp_limit_uA(const stim_params_t *p);
void stim_output_codes(uint32_t *pos, uint32_t *neg);
void stim_check_alive();          // call from loop(); faults if the timer stalls
