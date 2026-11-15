/* tVNS biphasic stimulation engine for the STM32G431 board.
 *
 * Output: DAC1_OUT1 (PA4, DAC_STIM) -> Howland input R13.
 *         DAC1_OUT2 (PA5, DAC_REF)  -> Howland reference R11, held at mid-scale.
 * Iout = (V_PA4 - V_PA5) * (1/Rsense + 1/150k) = (V_PA4 - V_PA5) * 10.007 mA/V.
 *
 * One period: +amp for PW, 0 for IPG, -amp for PW, 0 for the rest of 1/freq.
 * TIM6 (1 us tick) triggers DAC channel 1 in hardware at each segment edge, so
 * edges have no interrupt jitter and both phases last exactly the same number
 * of ticks. The TIM6 interrupt only queues the next segment's code and length.
 *
 * The engine also owns OUT_EN (PA6, photoMOS U9): high only while the
 * waveform runs, low on stop and on any fault. The +/-14.5 V rails (EN_RAIL)
 * belong to the caller: rails must be up and settled before stim_start().
 */
#ifndef STIM_H
#define STIM_H

#include <stdint.h>
#include <stdbool.h>

/* Hard limits; everything is validated against these. */
#define STIM_AMP_MAX_uA     7500u   /* compliance is clean to 7.5 mA (OPA2196 sim, handoff 5) */
#define STIM_FREQ_MIN_HZ    1u
#define STIM_FREQ_MAX_HZ    100u
#define STIM_PW_MIN_us      50u
#define STIM_PW_MAX_us      500u
#define STIM_IPG_MAX_us     200u
#define STIM_SEG_MIN_us     20u     /* shortest segment the ISR can keep up with at 16 MHz */
#define STIM_RAMP_MIN_uA_s  100u
#define STIM_RAMP_MAX_uA_s  20000u

/* Buffered DAC swings 0.2 V .. VDDA-0.2 V. At VDDA = 3.0 V that is codes 273..3823. */
#define DAC_CODE_MIN        280u
#define DAC_CODE_MAX        3815u
#define DAC_MID             2048u
#define DAC_TRIM_RANGE      200u    /* zero/vref trims allowed within MID +/- this */

/* Full-scale current for 4096 DAC codes, uA: VDDA * (1/100 + 1/150k).
 * 3.000 V -> 30020 uA. stim_init() recomputes it from the measured VDDA. */
#define STIM_GAIN_PER_V_uA  10007u
#define STIM_GAIN_FS_MIN_uA 25000u
#define STIM_GAIN_FS_MAX_uA 36000u

typedef struct {
	uint32_t amp_uA;      /* target amplitude per phase */
	uint32_t freq_hz;
	uint32_t pw_us;       /* per phase */
	uint32_t ipg_us;      /* interphase gap, 0 allowed */
	uint32_t ramp_uA_s;   /* amplitude increase rate (decreases are immediate) */
	uint32_t zero_code;   /* DAC_STIM code that gives zero current (offset trim) */
	uint32_t gain_fs_uA;  /* current for 4096 codes (gain calibration) */
} stim_params_t;

typedef enum { STIM_OFF = 0, STIM_RUNNING, STIM_STOPPING, STIM_FAULT } stim_state_t;

void stim_init(uint32_t vdda_mV);
const char *stim_set(const stim_params_t *p);  /* NULL on success, else error text */
void stim_get(stim_params_t *p);
bool stim_start(void);                /* raises OUT_EN, waits STIM_LEADIN_us, then pulses */
void stim_stop(void);                 /* graceful: finishes the current biphasic pulse */
void stim_estop(bool fault);          /* immediate: OUT_EN low and output to zero now */
void stim_set_vref(uint32_t code);
uint32_t stim_get_vref(void);
stim_state_t stim_state(void);
uint32_t stim_current_amp_uA(void);   /* amplitude actually being output (after ramp/rounding) */
uint32_t stim_amp_limit_uA(const stim_params_t *p);
void stim_check_alive(void);          /* call from the main loop; faults if the timer stalls */

#endif
