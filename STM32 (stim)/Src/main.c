/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : tVNS stimulator control firmware (NUCLEO-F303RE)
 ******************************************************************************
 * Console: ST-LINK virtual COM port (USART2, PA2/PA3), 115200 8N1. Type "help".
 * Stimulation is OFF at boot. Blue user button (B1, PC13) = emergency stop.
 * See stim.h for the output scheme.
 ******************************************************************************
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "regs.h"
#include "stim.h"

volatile uint32_t g_ms;
static uint32_t sysclk_hz, pclk1_hz;
static bool clock_from_hse;
static uint32_t reset_flags;

/* Called from Reset_Handler before .data/.bss are set up, so no globals here.
 * The project is built with -mfloat-abi=hard, so the FPU (CP10/CP11) must be
 * enabled before any float instruction runs, or the core will fault. */
void SystemInit(void)
{
#if !defined(__SOFT_FP__) && defined(__ARM_FP)
	volatile uint32_t *const SCB_CPACR = (uint32_t *)0xE000ED88;
	*SCB_CPACR |= (0xFu << 20);  /* full access to CP10 and CP11 */
	__asm volatile ("dsb\n\tisb");
#endif
}

void SysTick_Handler(void) { g_ms++; }

/* Any CPU fault: force zero current, then wait for the watchdog to reset. */
void HardFault_Handler(void)
{
	GPIO_BSRR(GPIOB_BASE) = 1u << (5 + 16);
	TIM2_CR1 = 0;
	DAC_CR &= ~DAC_CR_EN1;  /* reload DAC ch1 at mid-scale without the timer trigger */
	DAC_CR &= ~(DAC_CR_TEN1 | DAC_CR_TSEL1_MASK);
	DAC_CR |= DAC_CR_EN1;
	DAC_DHR12R1 = DAC_MID;
	for (;;) {}
}
void NMI_Handler(void) { HardFault_Handler(); }

/* 72 MHz from the ST-LINK's 8 MHz MCO (HSE bypass). Falls back to HSI (64 MHz). */
static void clock_init(void)
{
	FLASH_ACR = (FLASH_ACR & ~7u) | 2u | FLASH_ACR_PRFTBE;   /* 2 wait states */

	RCC_CR |= RCC_CR_HSEBYP;
	RCC_CR |= RCC_CR_HSEON;
	for (uint32_t t = 0; t < 200000u && !(RCC_CR & RCC_CR_HSERDY); t++) {}
	clock_from_hse = (RCC_CR & RCC_CR_HSERDY) != 0;

	uint32_t cfgr = (4u << 8);                 /* AHB /1, APB1 /2, APB2 /1 */
	if (clock_from_hse) {
		RCC_CFGR2 = 0;                         /* PREDIV /1 */
		cfgr |= (1u << 16) | (7u << 18);       /* PLL src HSE/PREDIV, x9 -> 72 MHz */
		sysclk_hz = 72000000u;
	} else {
		RCC_CR &= ~(RCC_CR_HSEON | RCC_CR_HSEBYP);
		cfgr |= (14u << 18);                   /* PLL src HSI/2, x16 -> 64 MHz */
		sysclk_hz = 64000000u;
	}
	RCC_CFGR = cfgr;
	RCC_CR |= RCC_CR_PLLON;
	while (!(RCC_CR & RCC_CR_PLLRDY)) {}
	RCC_CFGR = cfgr | 2u;                      /* SYSCLK = PLL */
	while ((RCC_CFGR & (3u << 2)) != (2u << 2)) {}
	pclk1_hz = sysclk_hz / 2u;
}

static void iwdg_init(void)
{
	/* LSI ~40 kHz / 8 = 5 kHz, 500 counts -> ~100 ms (80-133 ms over LSI tolerance).
	 * Not frozen in debug on purpose: halting the core resets it, which removes the
	 * output instead of leaving DC current flowing. */
	IWDG_KR = 0xCCCC;
	IWDG_KR = 0x5555;
	IWDG_PR = 1;
	IWDG_RLR = 499;
	while (IWDG_SR & 3u) {}
	IWDG_KR = 0xAAAA;
}
static inline void iwdg_kick(void) { IWDG_KR = 0xAAAA; }

/* ---- UART console ---- */

static void uart_init(void)
{
	RCC_AHBENR |= RCC_AHBENR_GPIOA;
	RCC_APB1ENR |= RCC_APB1ENR_USART2;
	GPIO_AFRL(GPIOA_BASE) = (GPIO_AFRL(GPIOA_BASE) & ~(0xFFu << 8)) | (0x77u << 8);  /* PA2/PA3 AF7 */
	GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(0xFu << 4)) | (0xAu << 4);
	USART2_BRR = (pclk1_hz + 57600u) / 115200u;
	USART2_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void putc_(char c)
{
	while (!(USART2_ISR & USART_ISR_TXE)) {}
	USART2_TDR = (uint8_t)c;
}
static void puts_(const char *s) { while (*s) { if (*s == '\n') putc_('\r'); putc_(*s++); } }
static void putu(uint32_t v)
{
	char b[11]; int i = 10; b[i] = 0;
	do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
	puts_(&b[i]);
}
static void put_kv(const char *k, uint32_t v, const char *unit)
{
	puts_(k); putu(v); puts_(unit); puts_("\n");
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

static bool button_pressed(void) { return (GPIO_IDR(GPIOC_BASE) & (1u << 13)) == 0; }

static const char *state_name(stim_state_t s)
{
	switch (s) {
	case STIM_RUNNING:  return "RUNNING";
	case STIM_STOPPING: return "STOPPING";
	case STIM_FAULT:    return "FAULT (timer stalled; output forced to zero)";
	default:            return "OFF";
	}
}

static void print_status(void)
{
	stim_params_t p; stim_get(&p);
	uint32_t pos, neg; stim_output_codes(&pos, &neg);
	puts_("state      "); puts_(state_name(stim_state())); puts_("\n");
	put_kv("amp        ", p.amp_uA, " uA (target)");
	put_kv("amp now    ", stim_current_amp_uA(), " uA (after ramp and DAC rounding)");
	put_kv("amp limit  ", stim_amp_limit_uA(&p), " uA");
	put_kv("freq       ", p.freq_hz, " Hz");
	put_kv("pw         ", p.pw_us, " us per phase");
	put_kv("ipg        ", p.ipg_us, " us");
	put_kv("ramp       ", p.ramp_uA_s, " uA/s");
	put_kv("zero code  ", p.zero_code, "");
	put_kv("vref code  ", stim_get_vref(), "");
	put_kv("gain       ", p.gain_fs_uA, " uA full scale");
	put_kv("codes +/-  ", pos, ""); put_kv("           ", neg, "");
	puts_("clock      "); putu(sysclk_hz / 1000000u); puts_(clock_from_hse ? " MHz (HSE)\n" : " MHz (HSI fallback, timing +/-1%)\n");
	puts_("button      "); puts_(button_pressed() ? "PRESSED\n" : "released\n");
}

static void print_help(void)
{
	puts_("on | off | stop (immediate) | status\n"
	      "amp <uA>  freq <Hz>  pw <us>  ipg <us>  ramp <uA/s>\n"
	      "zero <code>  vref <code>  gain <uA>   (calibration)\n"
	      "Changes while running take effect at the next period; amplitude increases ramp.\n");
}

static void handle_line(char *line)
{
	char *arg = strchr(line, ' ');
	if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
	uint32_t v = 0;
	bool has_v = arg && parse_u32(arg, &v);

	if (!strcmp(line, "") ) return;
	if (!strcmp(line, "help")) { print_help(); return; }
	if (!strcmp(line, "status")) { print_status(); return; }
	if (!strcmp(line, "stop")) { stim_estop(false); puts_("stopped\n"); return; }
	if (!strcmp(line, "off")) { stim_stop(); puts_("stopping after current pulse\n"); return; }
	if (!strcmp(line, "on")) {
		if (button_pressed()) { puts_("refused: button held\n"); return; }
		puts_(stim_start() ? "running (ramping up)\n" : "refused: already running or bad parameters\n");
		return;
	}
	if (!strcmp(line, "vref")) {
		if (!has_v || v < DAC_MID - DAC_TRIM_RANGE || v > DAC_MID + DAC_TRIM_RANGE) { puts_("vref 1848-2248\n"); return; }
		if (stim_state() == STIM_RUNNING || stim_state() == STIM_STOPPING) { puts_("turn off first\n"); return; }
		stim_set_vref(v); puts_("ok\n"); return;
	}

	stim_params_t p; stim_get(&p);
	uint32_t *field = 0;
	if (!strcmp(line, "amp")) field = &p.amp_uA;
	else if (!strcmp(line, "freq")) field = &p.freq_hz;
	else if (!strcmp(line, "pw")) field = &p.pw_us;
	else if (!strcmp(line, "ipg")) field = &p.ipg_us;
	else if (!strcmp(line, "ramp")) field = &p.ramp_uA_s;
	else if (!strcmp(line, "zero")) field = &p.zero_code;
	else if (!strcmp(line, "gain")) field = &p.gain_fs_uA;
	if (!field) { puts_("unknown command, try help\n"); return; }
	if (!has_v) { puts_("needs a number\n"); return; }
	*field = v;
	const char *err = stim_set(&p);
	puts_(err ? err : "ok"); puts_("\n");
}

int main(void)
{
	reset_flags = RCC_CSR;
	RCC_CSR |= RCC_CSR_RMVF;

	clock_init();
	stim_init(pclk1_hz * 2u);      /* APB1 prescaler != 1, so timer clock = 2 x PCLK1 */
	RCC_AHBENR |= RCC_AHBENR_GPIOC; /* PC13 = B1, input by default */
	uart_init();

	SCB_SHPR3 = (SCB_SHPR3 & 0x00FFFFFFu) | (0xF0u << 24);  /* SysTick below TIM2 */
	SYST_RVR = sysclk_hz / 1000u - 1u;
	SYST_CVR = 0;
	SYST_CSR = 7u;

	iwdg_init();

	puts_("\ntVNS stimulator ready. Output OFF. Type help.\n");
	if (reset_flags & RCC_CSR_IWDGRSTF) puts_("WARNING: last reset was the watchdog\n");
	if (!clock_from_hse) puts_("WARNING: HSE (ST-LINK MCO) not found, using HSI\n");

	char line[48];
	uint32_t len = 0;
	for (;;) {
		iwdg_kick();

		if (button_pressed() && stim_state() != STIM_OFF && stim_state() != STIM_FAULT) {
			stim_estop(false);
			puts_("\nE-STOP (button)\n");
		}
		stim_check_alive(g_ms);
		static bool fault_reported;
		if (stim_state() == STIM_FAULT) {
			if (!fault_reported) { puts_("\nFAULT: stimulation timer stalled, output forced to zero\n"); fault_reported = true; }
		} else {
			fault_reported = false;
		}

		uint32_t isr = USART2_ISR;
		if (isr & USART_ISR_ORE) USART2_ICR = USART_ICR_ORECF;
		if (isr & USART_ISR_RXNE) {
			char c = (char)USART2_RDR;
			static char prev;
			bool crlf = (c == '\n' && prev == '\r');
			prev = c;
			if (crlf) {
				/* second half of CR LF */
			} else if (c == '\r' || c == '\n') {
				puts_("\n");
				line[len] = 0;
				handle_line(line);
				len = 0;
				puts_("> ");
			} else if ((c == '\b' || c == 0x7F) && len) {
				len--; puts_("\b \b");
			} else if (c >= ' ' && len < sizeof line - 1) {
				line[len++] = c; putc_(c);
			}
		}
	}
}
