/* tVNS biphasic stimulation engine: TIM6 paces DAC1 channel 1 in hardware.
 * Ported from the bench STM32F303 engine (STM32 (stim)/Src/stim.c).
 * See stim.h for the waveform and the ownership of OUT_EN. */
#include "stim.h"
#include "main.h"
#include "dac.h"
#include "tim.h"

#define LEADIN_us       5000u   /* OUT_EN high -> first pulse; covers TLP175A turn-on */
#define REST_MIN_us     100u    /* the ISR rebuilds the next period during the rest */
#define CHUNK_MAX_us    50000u  /* TIM6 is 16-bit, so long rests are split into chunks */
#define SEG_MAX         24u     /* +, gap, -, and up to 21 rest chunks (1 Hz) */

typedef struct { uint16_t code; uint16_t dur_us; } seg_t;

static stim_params_t active, pending;
static volatile bool pending_dirty;
static volatile stim_state_t state = STIM_OFF;
static volatile bool stop_req;
static volatile uint32_t cur_amp_uA;      /* ramped amplitude for the current period */
static volatile uint32_t last_period_ms;
static uint32_t vref_code = DAC_MID;
static seg_t seg[SEG_MAX];
static volatile uint32_t n_seg, q;        /* q = segment queued in the preload registers */
static volatile uint32_t out_pos = DAC_MID;

/* ---- small helpers ---- */

static inline uint32_t irq_save(void) { uint32_t m = __get_PRIMASK(); __disable_irq(); return m; }
static inline void irq_restore(uint32_t m) { __set_PRIMASK(m); }

static inline void out_en_high(void) { OUT_EN_GPIO_Port->BSRR = OUT_EN_Pin; }
static inline void out_en_low(void)  { OUT_EN_GPIO_Port->BRR = OUT_EN_Pin; }

/* Channel 1 copies DHR to the output only on a TIM6 TRGO (update) event. */
static void dac_arm_trigger(void)
{
	DAC1->CR &= ~DAC_CR_EN1;            /* TSEL/TEN can only change while disabled */
	DAC1->CR = (DAC1->CR & ~(DAC_CR_TSEL1 | DAC_CR_TEN1)) | DAC_TRIGGER_T6_TRGO;
	DAC1->CR |= DAC_CR_EN1;
}

/* Put channel 1 on `code` now. With the timer stopped, a software update event
 * (UG) still produces TRGO, which latches the code. If the output does not
 * follow (it should), fall back to untriggered mode so zero is guaranteed. */
static void dac_force(uint32_t code)
{
	DAC1->DHR12R1 = code;
	TIM6->EGR = TIM_EGR_UG;             /* URS is set: no interrupt from this */
	for (uint32_t i = 0; i < 32u && DAC1->DOR1 != code; i++) { __NOP(); }
	if (DAC1->DOR1 != code) {
		DAC1->CR &= ~DAC_CR_EN1;
		DAC1->CR &= ~(DAC_CR_TSEL1 | DAC_CR_TEN1);
		DAC1->CR |= DAC_CR_EN1;
		DAC1->DHR12R1 = code;           /* untriggered: output follows in ~1 clock */
	}
}

static void timer_halt(void)
{
	TIM6->CR1 &= ~TIM_CR1_CEN;
	TIM6->SR = 0;
	NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
}

/* ---- parameters ---- */

static uint32_t amp_to_delta(uint32_t amp_uA, const stim_params_t *p)
{
	return (amp_uA * 4096u + p->gain_fs_uA / 2u) / p->gain_fs_uA;
}

uint32_t stim_amp_limit_uA(const stim_params_t *p)
{
	uint32_t up = DAC_CODE_MAX - p->zero_code;
	uint32_t dn = p->zero_code - DAC_CODE_MIN;
	uint32_t d = up < dn ? up : dn;
	uint32_t lim = d * p->gain_fs_uA / 4096u;
	return lim < STIM_AMP_MAX_uA ? lim : STIM_AMP_MAX_uA;
}

static const char *validate(const stim_params_t *p)
{
	if (p->freq_hz < STIM_FREQ_MIN_HZ || p->freq_hz > STIM_FREQ_MAX_HZ) return "freq out of range (1-100 Hz)";
	if (p->pw_us < STIM_PW_MIN_us || p->pw_us > STIM_PW_MAX_us) return "pw out of range (50-500 us)";
	if (p->ipg_us > STIM_IPG_MAX_us || (p->ipg_us != 0u && p->ipg_us < STIM_SEG_MIN_us)) return "ipg out of range (0 or 20-200 us)";
	if (p->ramp_uA_s < STIM_RAMP_MIN_uA_s || p->ramp_uA_s > STIM_RAMP_MAX_uA_s) return "ramp out of range (100-20000 uA/s)";
	if (p->zero_code < DAC_MID - DAC_TRIM_RANGE || p->zero_code > DAC_MID + DAC_TRIM_RANGE) return "zero code out of range";
	if (p->gain_fs_uA < STIM_GAIN_FS_MIN_uA || p->gain_fs_uA > STIM_GAIN_FS_MAX_uA) return "gain out of range";
	if (2u * p->pw_us + p->ipg_us + REST_MIN_us > 1000000u / p->freq_hz) return "pulse does not fit in the period";
	if (p->amp_uA > stim_amp_limit_uA(p)) return "amp above limit";
	return 0;
}

/* Build one full period. Runs with the timer stopped, or from the ISR at the
 * start of the previous period's last rest chunk. Amplitude is fixed for the
 * whole period, so the + and - phases always match. */
static void build_period(void)
{
	if (pending_dirty) {
		active = pending;
		pending_dirty = false;
	}

	uint32_t a = cur_amp_uA;
	if (a >= active.amp_uA) {
		a = active.amp_uA;
	} else {
		uint32_t step = active.ramp_uA_s / active.freq_hz;
		if (step == 0u) step = 1u;
		a = (a + step < active.amp_uA) ? a + step : active.amp_uA;
	}
	cur_amp_uA = a;

	uint32_t z = active.zero_code;
	uint32_t d = amp_to_delta(a, &active);
	uint32_t pos = z + d, neg = z - d;
	if (pos > DAC_CODE_MAX || neg < DAC_CODE_MIN) { pos = z; neg = z; }  /* cannot happen after validate */
	out_pos = pos;

	uint32_t n = 0;
	seg[n++] = (seg_t){ (uint16_t)pos, (uint16_t)active.pw_us };
	if (active.ipg_us) seg[n++] = (seg_t){ (uint16_t)z, (uint16_t)active.ipg_us };
	seg[n++] = (seg_t){ (uint16_t)neg, (uint16_t)active.pw_us };

	uint32_t rest = 1000000u / active.freq_hz - 2u * active.pw_us - active.ipg_us;
	uint32_t chunks = (rest + CHUNK_MAX_us - 1u) / CHUNK_MAX_us;
	uint32_t each = rest / chunks;
	for (uint32_t i = 0; i < chunks; i++) {
		uint32_t dur = (i == chunks - 1u) ? rest - each * (chunks - 1u) : each;
		seg[n++] = (seg_t){ (uint16_t)z, (uint16_t)dur };
	}
	n_seg = n;
	last_period_ms = HAL_GetTick();
}

/* Load segment i into the preload registers; it starts at the next update. */
static void queue(uint32_t i)
{
	q = i;
	DAC1->DHR12R1 = seg[i].code;
	TIM6->ARR = seg[i].dur_us - 1u;
}

/* TIM6 update: segment q has just started playing. Called at priority 0. */
static void stim_isr(void)
{
	uint32_t next = q + 1u;
	if (next >= n_seg) {
		/* The last rest chunk just started; the output is at zero. */
		if (stop_req || state != STIM_RUNNING) {
			timer_halt();
			out_en_low();
			dac_force(active.zero_code);
			cur_amp_uA = 0;
			out_pos = active.zero_code;
			stop_req = false;
			if (state != STIM_FAULT) state = STIM_OFF;
			return;
		}
		build_period();
		next = 0;
	}
	queue(next);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM6) stim_isr();
}

/* ---- public API ---- */

void stim_init(uint32_t vdda_mV)
{
	out_en_low();

	uint32_t gain = vdda_mV * STIM_GAIN_PER_V_uA / 1000u;
	if (gain < STIM_GAIN_FS_MIN_uA || gain > STIM_GAIN_FS_MAX_uA)
		gain = 3000u * STIM_GAIN_PER_V_uA / 1000u;    /* bad VDDA reading: assume 3.0 V */

	pending = (stim_params_t){
		.amp_uA = 5000, .freq_hz = 25, .pw_us = 250, .ipg_us = 50,
		.ramp_uA_s = 2500, .zero_code = DAC_MID, .gain_fs_uA = gain,
	};
	active = pending;

	/* TIM6 was set up by MX_TIM6_Init (1 MHz tick, TRGO on update, ARR preload).
	 * Take it over: update events from UG don't interrupt, update IRQ enabled. */
	timer_halt();
	TIM6->CR1 = TIM_CR1_ARPE | TIM_CR1_URS;
	TIM6->DIER = TIM_DIER_UIE;

	/* Both DAC channels at mid-scale, then enabled. Channel 2 (reference) is
	 * untriggered and stays put; channel 1 follows TIM6. */
	DAC1->DHR12R2 = vref_code;
	DAC1->CR |= DAC_CR_EN2;
	dac_arm_trigger();
	dac_force(active.zero_code);
}

const char *stim_set(const stim_params_t *p)
{
	const char *err = validate(p);
	if (err) return err;
	uint32_t m = irq_save();
	pending = *p;
	pending_dirty = true;
	/* Decreases apply at the next period; increases ramp. */
	if (cur_amp_uA > p->amp_uA) cur_amp_uA = p->amp_uA;
	if (state == STIM_OFF || state == STIM_FAULT) {
		active = *p;
		pending_dirty = false;
		dac_force(active.zero_code);
	}
	irq_restore(m);
	return 0;
}

void stim_get(stim_params_t *p)
{
	uint32_t m = irq_save();
	*p = pending_dirty ? pending : active;
	irq_restore(m);
}

bool stim_start(void)
{
	if (state != STIM_OFF && state != STIM_FAULT) return false;
	if (validate(&active)) return false;

	timer_halt();
	dac_arm_trigger();
	dac_force(active.zero_code);
	cur_amp_uA = 0;
	stop_req = false;

	/* Zero lead-in in the active ARR, then period 0 queued behind it. */
	TIM6->CR1 = TIM_CR1_ARPE | TIM_CR1_URS;
	TIM6->CNT = 0;
	TIM6->ARR = LEADIN_us - 1u;
	TIM6->EGR = TIM_EGR_UG;
	TIM6->SR = 0;
	NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);

	build_period();
	queue(0);

	state = STIM_RUNNING;
	out_en_high();
	TIM6->CR1 |= TIM_CR1_CEN;
	return true;
}

void stim_stop(void)
{
	uint32_t m = irq_save();
	if (state == STIM_RUNNING) {
		stop_req = true;
		state = STIM_STOPPING;
	}
	irq_restore(m);
}

void stim_estop(bool fault)
{
	uint32_t m = irq_save();
	timer_halt();
	out_en_low();
	dac_force(active.zero_code);
	cur_amp_uA = 0;
	out_pos = active.zero_code;
	stop_req = false;
	state = fault ? STIM_FAULT : STIM_OFF;
	irq_restore(m);
}

void stim_set_vref(uint32_t code) { vref_code = code; DAC1->DHR12R2 = code; }
uint32_t stim_get_vref(void) { return vref_code; }
stim_state_t stim_state(void) { return state; }

uint32_t stim_current_amp_uA(void)
{
	if (state != STIM_RUNNING && state != STIM_STOPPING) return 0;
	return (out_pos - active.zero_code) * active.gain_fs_uA / 4096u;
}

void stim_check_alive(void)
{
	if (state != STIM_RUNNING && state != STIM_STOPPING) return;
	uint32_t period_ms = 1000u / active.freq_hz;
	if (HAL_GetTick() - last_period_ms > period_ms + LEADIN_us / 1000u + 100u)
		stim_estop(true);
}
