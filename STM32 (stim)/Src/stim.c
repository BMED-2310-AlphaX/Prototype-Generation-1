#include "stim.h"
#include "regs.h"

extern volatile uint32_t g_ms;

/* Optional output-enable / kill line for the analog front end: PB5 (Arduino D4),
 * high = stimulation allowed. Low at boot, on stop and on any fault.
 * Give it an external pull-down so it is also low during reset. */
#define EN_PIN 5u
static inline void en_high(void) { GPIO_BSRR(GPIOB_BASE) = 1u << EN_PIN; }
static inline void en_low(void)  { GPIO_BSRR(GPIOB_BASE) = 1u << (EN_PIN + 16); }

#define SEG_MIN_us     10u   /* shortest segment; the ISR needs time to queue the next */
#define REST_MIN_us    100u
#define LEADIN_us      10u

typedef struct { uint32_t code; uint32_t dur_us; } seg_t;

static stim_params_t active, pending;
static volatile bool pending_dirty;
static volatile stim_state_t state = STIM_OFF;
static volatile bool stop_req;
static volatile uint32_t cur_amp_uA;     /* ramped amplitude for the current period */
static volatile uint32_t last_period_ms;
static uint32_t vref_code = DAC_MID;
static seg_t seg[4];
static uint32_t n_seg, q;                /* q = index of the segment queued in hardware */
static volatile uint32_t out_pos, out_neg;

/* ---- DAC helpers ---- */

/* Put channel 1 on the given code right now (software path, no trigger). */
static void dac_force(uint32_t code)
{
	DAC_CR &= ~DAC_CR_EN1;
	DAC_CR &= ~(DAC_CR_TEN1 | DAC_CR_TSEL1_MASK);
	DAC_CR |= DAC_CR_EN1;
	DAC_DHR12R1 = code;
}

/* Channel 1 latches DHR -> output only on TIM2 update events. */
static void dac_arm_timer_trigger(void)
{
	DAC_CR &= ~DAC_CR_EN1;
	DAC_CR |= DAC_CR_TEN1 | DAC_CR_TSEL1_TIM2;
	DAC_CR |= DAC_CR_EN1;
}

static void timer_halt(void)
{
	TIM2_CR1 &= ~TIM_CR1_CEN;
	TIM2_SR = 0;
	NVIC_ICPR0 = 1u << TIM2_IRQn;
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
	if (p->ipg_us > STIM_IPG_MAX_us || (p->ipg_us != 0 && p->ipg_us < SEG_MIN_us)) return "ipg out of range (0 or 10-200 us)";
	if (p->ramp_uA_s < STIM_RAMP_MIN_uA_s || p->ramp_uA_s > STIM_RAMP_MAX_uA_s) return "ramp out of range (100-20000 uA/s)";
	if (p->zero_code < DAC_MID - DAC_TRIM_RANGE || p->zero_code > DAC_MID + DAC_TRIM_RANGE) return "zero code out of range";
	if (p->gain_fs_uA < 20000u || p->gain_fs_uA > 50000u) return "gain out of range (20000-50000)";
	if (2u * p->pw_us + p->ipg_us + REST_MIN_us > 1000000u / p->freq_hz) return "pulse does not fit in the period";
	if (p->amp_uA > stim_amp_limit_uA(p)) return "amp above limit (see status)";
	return 0;
}

/* Build one full period. Called with the timer stopped, or from the ISR while the
 * previous period's rest segment is playing. Amplitude is fixed for the whole
 * period, so + and - phases always match. */
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
		if (step == 0) step = 1;
		a = (a + step < active.amp_uA) ? a + step : active.amp_uA;
	}
	cur_amp_uA = a;

	uint32_t z = active.zero_code;
	uint32_t d = amp_to_delta(a, &active);
	uint32_t pos = z + d, neg = z - d;
	if (pos > DAC_CODE_MAX || neg < DAC_CODE_MIN) { pos = z; neg = z; }  /* cannot happen after validate */
	out_pos = pos;
	out_neg = neg;

	uint32_t period = 1000000u / active.freq_hz;
	n_seg = 0;
	seg[n_seg++] = (seg_t){ pos, active.pw_us };
	if (active.ipg_us) seg[n_seg++] = (seg_t){ z, active.ipg_us };
	seg[n_seg++] = (seg_t){ neg, active.pw_us };
	seg[n_seg++] = (seg_t){ z, period - 2u * active.pw_us - active.ipg_us };

	last_period_ms = g_ms;
}

static void queue(uint32_t i)
{
	q = i;
	DAC_DHR12R1 = seg[i].code;     /* latched by hardware at the next update event */
	TIM2_ARR = seg[i].dur_us - 1u; /* preloaded, takes effect at the next update event */
}

/* Fires at the start of each segment; seg[q] is now playing. */
void TIM2_IRQHandler(void)
{
	TIM2_SR = ~TIM_SR_UIF;

	uint32_t next = q + 1u;
	if (next >= n_seg) {
		/* Rest segment just started (output is at zero). */
		if (stop_req || state != STIM_RUNNING) {
			timer_halt();
			dac_force(active.zero_code);
			en_low();
			cur_amp_uA = 0;
			state = STIM_OFF;
			return;
		}
		build_period();
		next = 0;
	}
	queue(next);
}

/* ---- public API ---- */

void stim_init(uint32_t timer_clk_hz)
{
	/* Kill line first */
	RCC_AHBENR |= RCC_AHBENR_GPIOA | RCC_AHBENR_GPIOB;
	en_low();
	GPIO_MODER(GPIOB_BASE) = (GPIO_MODER(GPIOB_BASE) & ~(3u << (EN_PIN * 2))) | (1u << (EN_PIN * 2));

	pending = (stim_params_t){
		.amp_uA = 5000, .freq_hz = 25, .pw_us = 250, .ipg_us = 50,
		.ramp_uA_s = 2500, .zero_code = DAC_MID, .gain_fs_uA = STIM_GAIN_FS_DEFAULT_uA,
	};
	active = pending;

	/* PA4, PA5 analog */
	GPIO_MODER(GPIOA_BASE) |= (3u << (4 * 2)) | (3u << (5 * 2));
	RCC_APB1ENR |= RCC_APB1ENR_DAC1 | RCC_APB1ENR_TIM2;
	(void)RCC_APB1ENR;

	/* Output buffers stay enabled (BOFF = 0); 100k Howland inputs are an easy load. */
	DAC_DHR12R2 = vref_code;
	DAC_CR |= DAC_CR_EN2;
	dac_force(active.zero_code);

	/* TIM2: 1 us tick, TRGO on update, update IRQ at top priority */
	TIM2_CR1 = 0;
	TIM2_PSC = timer_clk_hz / 1000000u - 1u;
	TIM2_CR2 = TIM_CR2_MMS_UPDATE;
	TIM2_DIER = TIM_DIER_UIE;
	timer_halt();
	NVIC_ISER0 = 1u << TIM2_IRQn;
}

const char *stim_set(const stim_params_t *p)
{
	const char *err = validate(p);
	if (err) return err;
	irq_disable();
	pending = *p;
	pending_dirty = true;
	/* Amplitude decreases take effect at the next period; never jump up. */
	if (cur_amp_uA > p->amp_uA) cur_amp_uA = p->amp_uA;
	irq_enable();
	if (state == STIM_OFF || state == STIM_FAULT) {
		active = *p;
		pending_dirty = false;
		dac_force(active.zero_code);
	}
	return 0;
}

void stim_get(stim_params_t *p)
{
	irq_disable();
	*p = pending_dirty ? pending : active;
	irq_enable();
}

bool stim_start(void)
{
	if (state != STIM_OFF && state != STIM_FAULT) return false;
	if (validate(&active)) return false;

	timer_halt();
	dac_force(active.zero_code);
	dac_arm_timer_trigger();
	cur_amp_uA = 0;
	stop_req = false;

	/* Short zero lead-in loaded into the shadow ARR, then queue period 0. */
	TIM2_CR1 = TIM_CR1_ARPE | TIM_CR1_URS;
	TIM2_CNT = 0;
	TIM2_ARR = LEADIN_us - 1u;
	TIM2_EGR = TIM_EGR_UG;
	TIM2_SR = 0;
	NVIC_ICPR0 = 1u << TIM2_IRQn;

	build_period();
	queue(0);

	state = STIM_RUNNING;
	en_high();
	TIM2_CR1 |= TIM_CR1_CEN;
	return true;
}

void stim_stop(void)
{
	if (state == STIM_RUNNING) {
		stop_req = true;
		state = STIM_STOPPING;
	}
}

void stim_estop(bool fault)
{
	irq_disable();
	timer_halt();
	en_low();
	dac_force(active.zero_code);
	cur_amp_uA = 0;
	stop_req = false;
	state = fault ? STIM_FAULT : STIM_OFF;
	irq_enable();
}

void stim_set_vref(uint32_t code) { vref_code = code; DAC_DHR12R2 = code; }
uint32_t stim_get_vref(void) { return vref_code; }
stim_state_t stim_state(void) { return state; }

uint32_t stim_current_amp_uA(void)
{
	if (state != STIM_RUNNING && state != STIM_STOPPING) return 0;
	return (out_pos - active.zero_code) * active.gain_fs_uA / 4096u;
}

void stim_output_codes(uint32_t *pos, uint32_t *neg) { *pos = out_pos; *neg = out_neg; }

void stim_check_alive(uint32_t now_ms)
{
	if (state != STIM_RUNNING && state != STIM_STOPPING) return;
	uint32_t period_ms = 1000u / active.freq_hz;
	if (now_ms - last_period_ms > period_ms + 100u)
		stim_estop(true);
}
