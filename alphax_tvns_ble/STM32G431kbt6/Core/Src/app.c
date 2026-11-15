/* AlphaX tVNS application layer.
 *
 * Session sequence (POWER_SUBSYSTEM_HANDOFF.md section 4, IEC 60601-2-10
 * "output blocked at power-up"):
 *   start: EN_RAIL high -> wait RAIL_SETTLE_MS -> stim_start() raises OUT_EN,
 *          waits 5 ms, then pulses (250 us phases, 50 us gap, 25 Hz)
 *   stop:  waveform finishes its pulse -> OUT_EN low -> EN_RAIL low
 *   e-stop (button, USB plugged in, fault): OUT_EN low and output to zero at
 *          once, then EN_RAIL low
 *
 * App protocol (unchanged from the ESP32 firmware, so the iOS app works as is):
 *   START:<intensity>:<minutes>   STOP   SET:<intensity>
 *   intensity is 0-80 percent of AMP_AT_MAX_INTENSITY_uA.
 *   Status JSON is notified every 500 ms.
 *
 * Debug output goes to SWO (ITM port 0): CubeIDE > SWV ITM Data Console.
 *
 * NEVER halt this firmware in the debugger with electrodes on a person: the
 * DAC holds whatever segment was queued while the CPU is stopped.
 */
#include "app.h"
#include "main.h"
#include "adc.h"
#include "iwdg.h"
#include "stim.h"
#include "ble.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- configuration ---- */

/* Current delivered at the app's 80% ceiling. 5 mA matches the design point.
 * Raising it raises the dose at every slider position. */
#define AMP_AT_MAX_INTENSITY_uA 5000u
#define MAX_INTENSITY           80u     /* app slider ceiling, percent */
#define DEFAULT_INTENSITY       40u
#define MAX_SESSION_MIN         60u
#define DEFAULT_SESSION_MIN     20u

#define RAIL_SETTLE_MS          20u     /* TPS65131 soft start (~1 ms) plus output caps; check on the scope */
#define STATUS_PERIOD_MS        500u
#define BATT_PERIOD_MS          1000u

#define BATT_START_MIN_mV       3450u   /* refuse to start below this (handoff block 5) */
#define BATT_END_mV             3300u   /* end a session below this */
#define BATT_LOW_COUNT          3u      /* consecutive low readings before ending */
#define BATT_EMPTY_mV           3300u   /* 0% for the app */
#define BATT_FULL_mV            4150u   /* 100% for the app */

/* ---- state ---- */

typedef enum { A_IDLE, A_RAILS_UP, A_RUNNING } app_state_t;

static volatile app_state_t app = A_IDLE;   /* also read from the EXTI interrupt */
static uint32_t t_rails;                /* when EN_RAIL went high */
static uint32_t session_start_ms, session_ms;
static uint32_t target_pct = DEFAULT_INTENSITY;
static uint32_t pending_minutes = DEFAULT_SESSION_MIN;
static volatile bool estop_flag;        /* set from the EXTI interrupt */
static volatile const char *estop_why;
static uint32_t vbat_mV, vdda_mV = 3000u;
static uint32_t low_count;
static bool wdg_reset;                  /* last reset came from the watchdog */
static uint32_t t_status, t_batt;

/* ---- SWO console ---- */

int __io_putchar(int ch)
{
	ITM_SendChar((uint32_t)ch);
	return ch;
}

/* ---- pins ---- */

static inline bool usb_present(void)  { return HAL_GPIO_ReadPin(VBUS_SENSE_GPIO_Port, VBUS_SENSE_Pin) == GPIO_PIN_SET; }
static inline bool button_down(void)  { return HAL_GPIO_ReadPin(WAKE_GPIO_Port, WAKE_Pin) == GPIO_PIN_SET; }  /* R10 pull-down */
static inline bool charging(void)     { return usb_present() && HAL_GPIO_ReadPin(STAT_GPIO_Port, STAT_Pin) == GPIO_PIN_RESET; }
static inline void rails(bool on)     { HAL_GPIO_WritePin(EN_RAIL_MCU_GPIO_Port, EN_RAIL_MCU_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static inline void led(bool on)       { HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }

/* ---- ADC: battery and VDDA ---- */

static uint32_t adc_read(uint32_t channel)
{
	ADC_ChannelConfTypeDef c = {0};
	c.Channel = channel;
	c.Rank = ADC_REGULAR_RANK_1;
	c.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;   /* 500k source (1M/1M) and VREFINT both need it */
	c.SingleDiff = ADC_SINGLE_ENDED;
	c.OffsetNumber = ADC_OFFSET_NONE;
	if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) return 0;

	uint32_t sum = 0;
	for (int i = 0; i < 4; i++) {
		HAL_ADC_Start(&hadc1);
		if (HAL_ADC_PollForConversion(&hadc1, 5) != HAL_OK) { HAL_ADC_Stop(&hadc1); return 0; }
		sum += HAL_ADC_GetValue(&hadc1);
		HAL_ADC_Stop(&hadc1);
	}
	return sum / 4u;
}

static void measure(void)
{
	uint32_t ref = adc_read(ADC_CHANNEL_VREFINT);
	if (ref) vdda_mV = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(ref, ADC_RESOLUTION_12B);
	uint32_t raw = adc_read(ADC_CHANNEL_2);
	vbat_mV = raw * vdda_mV * 2u / 4095u;      /* VBAT/2 divider R6/R7 */
}

static uint32_t batt_percent(void)
{
	if (vbat_mV <= BATT_EMPTY_mV) return 0;
	if (vbat_mV >= BATT_FULL_mV) return 100;
	return (vbat_mV - BATT_EMPTY_mV) * 100u / (BATT_FULL_mV - BATT_EMPTY_mV);
}

/* ---- sessions ---- */

static uint32_t pct_to_amp(uint32_t pct)
{
	if (pct > MAX_INTENSITY) pct = MAX_INTENSITY;
	return pct * AMP_AT_MAX_INTENSITY_uA / MAX_INTENSITY;
}

static uint32_t amp_to_pct(uint32_t uA) { return uA * MAX_INTENSITY / AMP_AT_MAX_INTENSITY_uA; }

static uint32_t remaining_s(void)
{
	if (app != A_RUNNING) return 0;
	uint32_t el = HAL_GetTick() - session_start_ms;
	return el >= session_ms ? 0 : (session_ms - el) / 1000u;
}

static const char *state_name(void)
{
	if (app == A_RAILS_UP) return "ramping_up";
	if (app != A_RUNNING) return "idle";
	switch (stim_state()) {
	case STIM_RUNNING:
		/* Within one DAC code (~7 uA) of target counts as there. */
		return stim_current_amp_uA() + 20u < pct_to_amp(target_pct) ? "ramping_up" : "running";
	case STIM_STOPPING: return "ramping_down";
	default:            return "idle";
	}
}

/* Immediate stop. Safe from interrupt context. */
static void emergency(const char *why)
{
	stim_estop(false);
	rails(false);
	estop_why = why;
	estop_flag = true;
}

static void rails_down(const char *why)
{
	rails(false);
	app = A_IDLE;
	printf("session end: %s\n", why);
}

static bool start_session(uint32_t pct, uint32_t minutes)
{
	if (app != A_IDLE) { printf("refused: already running\n"); return false; }
	if (pct > MAX_INTENSITY) pct = MAX_INTENSITY;
	if (minutes == 0u || minutes > MAX_SESSION_MIN) minutes = DEFAULT_SESSION_MIN;
	if (usb_present())  { printf("refused: USB connected\n"); return false; }
	if (button_down())  { printf("refused: button held\n"); return false; }
	if (vbat_mV < BATT_START_MIN_mV) { printf("refused: battery %lu mV\n", (unsigned long)vbat_mV); return false; }

	stim_params_t p;
	stim_get(&p);
	p.amp_uA = pct_to_amp(pct);
	const char *err = stim_set(&p);
	if (err) { printf("refused: %s\n", err); return false; }

	target_pct = pct;
	pending_minutes = minutes;
	estop_flag = false;
	low_count = 0;
	rails(true);
	t_rails = HAL_GetTick();
	app = A_RAILS_UP;
	printf("session start: %lu%% (%lu uA) for %lu min\n",
	       (unsigned long)pct, (unsigned long)p.amp_uA, (unsigned long)minutes);
	return true;
}

static void end_session(const char *why)
{
	if (app == A_RAILS_UP) { rails_down(why); return; }
	if (app == A_RUNNING) {
		stim_stop();              /* finishes the pulse; rails drop once it is off */
		printf("stopping: %s\n", why);
	}
}

static void set_intensity(uint32_t pct)
{
	if (pct > MAX_INTENSITY) pct = MAX_INTENSITY;
	stim_params_t p;
	stim_get(&p);
	p.amp_uA = pct_to_amp(pct);
	const char *err = stim_set(&p);
	if (err) { printf("refused: %s\n", err); return; }
	target_pct = pct;
}

static void send_status(void)
{
	char buf[120];
	snprintf(buf, sizeof buf,
	         "{\"state\":\"%s\",\"intensity\":%lu,\"target\":%lu,\"remaining\":%lu,\"batt\":%lu}",
	         state_name(),
	         (unsigned long)amp_to_pct(stim_current_amp_uA()),
	         (unsigned long)target_pct,
	         (unsigned long)remaining_s(),
	         (unsigned long)batt_percent());
	ble_notify(buf);
}

/* ---- BLE callbacks ---- */

void ble_on_command(const char *cmd)
{
	printf("[ble] %s\n", cmd);
	if (!strncmp(cmd, "START", 5)) {
		uint32_t pct = DEFAULT_INTENSITY, min = DEFAULT_SESSION_MIN;
		const char *p = strchr(cmd, ':');
		if (p) {
			pct = strtoul(p + 1, (char **)&p, 10);
			if (*p == ':') min = strtoul(p + 1, 0, 10);
		}
		start_session(pct, min);
	} else if (!strncmp(cmd, "STOP", 4)) {
		end_session("app");
	} else if (!strncmp(cmd, "SET", 3)) {
		const char *p = strchr(cmd, ':');
		if (p) set_intensity(strtoul(p + 1, 0, 10));
	}
	send_status();
}

void ble_on_disconnect(void)
{
	printf("[ble] disconnected\n");
	if (app != A_IDLE) end_session("link lost");    /* fail safe: phone gone, session ends */
}

/* ---- interrupts ---- */

void app_gpio_irq(uint16_t pin)
{
	if (app == A_IDLE) return;
	if (pin == WAKE_Pin && button_down()) emergency("button");
	if (pin == VBUS_SENSE_Pin && usb_present()) emergency("USB connected");
}

/* ---- LED ---- */

static void update_led(void)
{
	uint32_t t = HAL_GetTick();
	bool fault = stim_state() == STIM_FAULT || ble_failed() || (wdg_reset && t < 10000u);
	bool on;
	if (fault)                 on = (t / 100u) % 2u;            /* fast blink */
	else if (app != A_IDLE)    on = true;                       /* session */
	else if (usb_present())    on = charging() ? (t / 500u) % 2u : true;  /* charging / full */
	else if (ble_connected())  on = (t % 1000u) < 50u;          /* short flash every second */
	else                       on = (t % 3000u) < 50u;          /* short flash every 3 s */
	led(on);
}

/* ---- main entry points ---- */

void app_init(void)
{
#ifdef DEBUG
	__HAL_DBGMCU_FREEZE_IWDG();       /* breakpoints would otherwise reset the chip */
	HAL_DBGMCU_EnableDBGSleepMode();  /* keep SWD alive through __WFI */
#endif
	wdg_reset = __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0u;
	__HAL_RCC_CLEAR_RESET_FLAGS();

	rails(false);
	HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
	(void)adc_read(ADC_CHANNEL_VREFINT);   /* first read after enabling the reference path */
	measure();

	stim_init(vdda_mV);
	ble_init();

	printf("\nAlphaX tVNS (STM32G431) ready. VDDA %lu mV, battery %lu mV%s\n",
	       (unsigned long)vdda_mV, (unsigned long)vbat_mV,
	       wdg_reset ? "\nWARNING: last reset was the watchdog" : "");
	t_status = t_batt = HAL_GetTick();
}

void app_poll(void)
{
	uint32_t now = HAL_GetTick();

	ble_poll();

	/* The interrupt checks again by polling, in case an edge was missed. */
	if (app != A_IDLE) {
		if (usb_present())      emergency("USB connected");
		else if (button_down()) emergency("button");
	}

	/* Finish emergency stops (raised here or in interrupt context). */
	if (estop_flag) {
		estop_flag = false;
		if (app != A_IDLE) rails_down((const char *)estop_why);
	}

	stim_check_alive();

	switch (app) {
	case A_IDLE:
		break;
	case A_RAILS_UP:
		if (now - t_rails >= RAIL_SETTLE_MS) {
			bool ok;
			__disable_irq();      /* no e-stop may slip in between the check and the start */
			ok = !estop_flag && !usb_present() && !button_down() && stim_start();
			__enable_irq();
			if (ok) {
				session_ms = pending_minutes * 60000u;
				session_start_ms = HAL_GetTick();
				app = A_RUNNING;
			} else {
				rails_down("could not start");
			}
		}
		break;
	case A_RUNNING: {
		stim_state_t s = stim_state();
		if (s == STIM_OFF)   { rails_down("stopped"); break; }
		if (s == STIM_FAULT) { rails_down("FAULT: stimulation timer stalled"); break; }
		if (s == STIM_RUNNING && remaining_s() == 0u) end_session("duration elapsed");
		break;
	}
	}

	if (now - t_batt >= BATT_PERIOD_MS) {
		t_batt = now;
		measure();
		if (app != A_IDLE && vbat_mV < BATT_END_mV) {
			if (++low_count >= BATT_LOW_COUNT) end_session("battery low");
		} else {
			low_count = 0;
		}
	}

	if (now - t_status >= STATUS_PERIOD_MS) {
		t_status = now;
		send_status();
	}

	update_led();

	/* Refresh only after every check above has run this pass. */
	HAL_IWDG_Refresh(&hiwdg);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
	app_gpio_irq(pin);
}
