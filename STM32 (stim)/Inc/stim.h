/* tVNS biphasic stimulation engine.
 *
 * Output: DAC1_OUT1 (PA4) -> Howland pump Vin (R3).
 *         DAC1_OUT2 (PA5) -> Howland Vref (R1), held at mid-scale.
 * Iout = (V_PA4 - V_PA5) * (1/Rsense + 1/Rset) ~= 10.01 mA/V.
 *
 * One period: +Iamp for PW, 0 for IPG, -Iamp for PW, 0 for the rest of 1/Freq.
 * TIM2 (1 us tick) triggers the DAC in hardware at each phase edge, so edges have
 * no interrupt jitter and the + and - phases last exactly the same number of ticks.
 * The ISR only queues the next phase's DAC code and duration.
 */
#ifndef STIM_H
#define STIM_H

#include <stdint.h>
#include <stdbool.h>

/* Hard limits (firmware clamps everything to these) */
#define STIM_AMP_MAX_uA     15000u
#define STIM_FREQ_MIN_HZ    1u
#define STIM_FREQ_MAX_HZ    100u
#define STIM_PW_MIN_us      50u
#define STIM_PW_MAX_us      500u
#define STIM_IPG_MAX_us     200u
#define STIM_RAMP_MIN_uA_s  100u
#define STIM_RAMP_MAX_uA_s  20000u

/* DAC output buffer can only swing ~0.2 V .. VDDA-0.2 V */
#define DAC_CODE_MIN        250u
#define DAC_CODE_MAX        3845u
#define DAC_MID             2048u
#define DAC_TRIM_RANGE      200u   /* zero/vref trims allowed within MID +/- this */

/* Full-scale current for 4096 DAC codes, in uA: 3.3 V * (1/100 + 1/100k) S = 33033 uA */
#define STIM_GAIN_FS_DEFAULT_uA 33033u

typedef struct {
	uint32_t amp_uA;      /* target amplitude per phase */
	uint32_t freq_hz;
	uint32_t pw_us;       /* per phase */
	uint32_t ipg_us;      /* interphase gap, 0 allowed */
	uint32_t ramp_uA_s;   /* amplitude increase rate (decreases are immediate) */
	uint32_t zero_code;   /* DAC code that gives zero current (offset trim) */
	uint32_t gain_fs_uA;  /* gain calibration */
} stim_params_t;

typedef enum { STIM_OFF = 0, STIM_RUNNING, STIM_STOPPING, STIM_FAULT } stim_state_t;

void stim_init(uint32_t timer_clk_hz);
const char *stim_set(const stim_params_t *p);  /* NULL on success, else error text */
void stim_get(stim_params_t *p);
bool stim_start(void);
void stim_stop(void);                 /* graceful: finishes current biphasic pulse */
void stim_estop(bool fault);          /* immediate: output to zero now */
void stim_set_vref(uint32_t code);
uint32_t stim_get_vref(void);
stim_state_t stim_state(void);
uint32_t stim_current_amp_uA(void);   /* amplitude actually being output (after ramp/rounding) */
uint32_t stim_amp_limit_uA(const stim_params_t *p);
void stim_check_alive(uint32_t now_ms);  /* call from main loop; faults if timer stalls */
void stim_output_codes(uint32_t *pos, uint32_t *neg);

#endif
