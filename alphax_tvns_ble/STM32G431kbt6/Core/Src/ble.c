/* ANNA-B112 GATT server over u-connectXpress AT commands. See ble.h. */
#include "ble.h"
#include "main.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* 128-bit UUIDs, written in normal reading order for AT+UBTGSER/AT+UBTGCHA. */
#define UUID_SERVICE "6E5A0001B5A3F393E0A9E50E24DCCA9E"
#define UUID_COMMAND "6E5A0002B5A3F393E0A9E50E24DCCA9E"
#define UUID_STATUS  "6E5A0003B5A3F393E0A9E50E24DCCA9E"

/* Advertising data (appended to the mandatory flags): one AD block, type 0x07
 * "complete list of 128-bit service UUIDs", UUID in little-endian byte order.
 * The iOS app scans with this service as a filter, so it must be advertised. */
#define ADV_DATA     "11079ECADC240EE5A9E093F3A3B501005A6E"

#define PROP_COMMAND "0C"   /* write | write without response */
#define PROP_STATUS  "12"   /* read | notify */

#define T_POWER_ms    10u    /* U8 load-switch ramp (~1.5 ms) with margin, reset held */
#define T_STARTUP_ms  4000u  /* wait for "+STARTUP" after reset release */
#define T_CMD_ms      1500u
#define T_RETRY_ms    2000u
#define MAX_RETRIES   5u

#define RX_SIZE   1024u
#define LINE_MAX  600u       /* a 244-byte write arrives as 488 hex characters */
#define CMD_MAX   64u        /* longest app command we accept */
#define NOTIFY_MAX 120u      /* stays under the ATT MTU iOS negotiates (185) */

typedef enum { S_OFF, S_POWER, S_BOOT, S_CMD, S_READY, S_RETRY, S_FAILED } bstate_t;
typedef enum {
	C_ECHO, C_GET_NAME, C_GET_ADV, C_SET_NAME, C_SET_ADV, C_SAVE, C_RESTART,
	C_SERVICE, C_CHAR_CMD, C_CHAR_STATUS, C_NOTIFY
} step_t;

static bstate_t st = S_OFF;
static step_t step;
static bool busy;                     /* a command is waiting for OK/ERROR */
static uint32_t t_state, t_cmd;
static uint32_t retries;
static bool persisted;                /* name/adv saved this power cycle; don't loop */
static bool name_ok, adv_ok;
static int conn_handle = -1;
static int cmd_handle = -1, status_handle = -1;

static uint8_t rx_byte;
static uint8_t rx_ring[RX_SIZE];
static volatile uint16_t rx_head, rx_tail;
static char line[LINE_MAX + 1];
static uint32_t line_len;
static char resp[96];                 /* last "+XXX:" information line of a command */
static char tx[2u * NOTIFY_MAX + 48u];

/* ---- UART ---- */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
	if (h->Instance != USART1) return;
	uint16_t n = (uint16_t)((rx_head + 1u) % RX_SIZE);
	if (n != rx_tail) { rx_ring[rx_head] = rx_byte; rx_head = n; }
	HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
	if (h->Instance != USART1) return;
	if (h->RxState == HAL_UART_STATE_READY) HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

static void send(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void send(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tx, sizeof tx - 1u, fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if ((uint32_t)n > sizeof tx - 2u) n = (int)(sizeof tx - 2u);
	tx[n++] = '\r';
	resp[0] = 0;
	busy = true;
	t_cmd = HAL_GetTick();
	HAL_UART_Transmit(&huart1, (uint8_t *)tx, (uint16_t)n, 100);
}

/* ---- power ---- */

/* Nothing may drive the module's pins while its supply is off, or it is
 * back-powered through its I/O protection diodes. */
static void pins_safe(void)
{
	HAL_UART_AbortReceive_IT(&huart1);
	HAL_UART_DeInit(&huart1);
	GPIO_InitTypeDef g = { .Pin = BLE_RXD_Pin, .Mode = GPIO_MODE_OUTPUT_PP,
	                       .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
	HAL_GPIO_WritePin(BLE_RXD_GPIO_Port, BLE_RXD_Pin, GPIO_PIN_RESET);
	HAL_GPIO_Init(BLE_RXD_GPIO_Port, &g);
	HAL_GPIO_WritePin(BLE_DSR_GPIO_Port, BLE_DSR_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(BLE_RESET_N_GPIO_Port, BLE_RESET_N_Pin, GPIO_PIN_RESET);
}

static void link_lost(void)
{
	bool was = conn_handle >= 0;
	conn_handle = -1;
	if (was) ble_on_disconnect();
}

void ble_power_off(void)
{
	link_lost();
	pins_safe();
	HAL_GPIO_WritePin(BLE_EN_GPIO_Port, BLE_EN_Pin, GPIO_PIN_RESET);
	busy = false;
	st = S_OFF;
}

static void power_on(void)
{
	pins_safe();                                   /* reset held low */
	HAL_GPIO_WritePin(BLE_EN_GPIO_Port, BLE_EN_Pin, GPIO_PIN_SET);
	persisted = false;
	cmd_handle = status_handle = -1;
	st = S_POWER;
	t_state = HAL_GetTick();
}

static void fail(void)
{
	ble_power_off();
	if (++retries > MAX_RETRIES) { st = S_FAILED; return; }
	st = S_RETRY;
	t_state = HAL_GetTick();
}

void ble_init(void)
{
	retries = 0;
	power_on();
}

/* ---- configuration sequence ---- */

static bool same_ignoring_quotes_case(const char *a, const char *b)
{
	for (;;) {
		while (*a == '"') a++;
		while (*b == '"') b++;
		if (!*a || !*b) return *a == *b;
		if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
		a++; b++;
	}
}

static void issue(step_t s)
{
	step = s;
	switch (s) {
	case C_ECHO:        send("ATE0"); break;
	case C_GET_NAME:    send("AT+UBTLN?"); break;
	case C_GET_ADV:     send("AT+UBTAD?"); break;
	case C_SET_NAME:    send("AT+UBTLN=\"" BLE_DEVICE_NAME "\""); break;
	case C_SET_ADV:     send("AT+UBTAD=" ADV_DATA); break;
	case C_SAVE:        send("AT&W"); break;
	case C_RESTART:     send("AT+CPWROFF"); break;
	case C_SERVICE:     send("AT+UBTGSER=" UUID_SERVICE); break;
	case C_CHAR_CMD:    send("AT+UBTGCHA=" UUID_COMMAND "," PROP_COMMAND ",1,1"); break;
	case C_CHAR_STATUS: send("AT+UBTGCHA=" UUID_STATUS "," PROP_STATUS ",1,1,7B7D"); break;
	case C_NOTIFY:      break;   /* sent by ble_notify() */
	}
}

static void start_config(void)
{
	st = S_CMD;
	issue(C_ECHO);
}

/* Value after "+XXX:" in resp, or NULL. */
static const char *resp_value(const char *tag)
{
	size_t n = strlen(tag);
	if (strncmp(resp, tag, n) != 0) return 0;
	return resp + n;
}

static void on_result(bool ok)
{
	busy = false;
	if (st == S_READY) return;          /* notifications: nothing to do either way */
	if (st != S_CMD) return;

	const char *v;
	switch (step) {
	case C_ECHO:
		if (!ok) { fail(); return; }
		issue(C_GET_NAME);
		break;
	case C_GET_NAME:
		v = resp_value("+UBTLN:");
		name_ok = ok && v && same_ignoring_quotes_case(v, BLE_DEVICE_NAME);
		issue(C_GET_ADV);
		break;
	case C_GET_ADV:
		v = resp_value("+UBTAD:");
		adv_ok = ok && v && same_ignoring_quotes_case(v, ADV_DATA);
		issue((name_ok && adv_ok) || persisted ? C_SERVICE : C_SET_NAME);
		break;
	case C_SET_NAME:  if (!ok) { fail(); return; } issue(C_SET_ADV); break;
	case C_SET_ADV:   if (!ok) { fail(); return; } issue(C_SAVE); break;
	case C_SAVE:      if (!ok) { fail(); return; } issue(C_RESTART); break;
	case C_RESTART:
		if (!ok) { fail(); return; }
		persisted = true;               /* module reboots and prints +STARTUP */
		st = S_BOOT;
		t_state = HAL_GetTick();
		break;
	case C_SERVICE:
		if (!ok || !resp_value("+UBTGSER:")) { fail(); return; }
		issue(C_CHAR_CMD);
		break;
	case C_CHAR_CMD:
		v = resp_value("+UBTGCHA:");
		if (!ok || !v) { fail(); return; }
		cmd_handle = atoi(v);
		issue(C_CHAR_STATUS);
		break;
	case C_CHAR_STATUS:
		v = resp_value("+UBTGCHA:");
		if (!ok || !v) { fail(); return; }
		status_handle = atoi(v);
		retries = 0;
		st = S_READY;
		break;
	case C_NOTIFY:
		break;
	}
}

/* ---- incoming lines ---- */

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	c = (char)toupper((unsigned char)c);
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* "+UUBTGRW:<conn>,<handle>,<hex>,<options>" */
static void on_gatt_write(const char *p)
{
	(void)strtol(p, (char **)&p, 10);
	if (*p++ != ',') return;
	long h = strtol(p, (char **)&p, 10);
	if (*p++ != ',' || h != cmd_handle) return;

	char cmd[CMD_MAX + 1];
	uint32_t n = 0;
	while (n < CMD_MAX) {
		int hi = hexval(p[0]), lo = hexval(p[1]);
		if (hi < 0 || lo < 0) break;
		char c = (char)(hi * 16 + lo);
		if (c < ' ' || c > '~') break;          /* printable ASCII only */
		cmd[n++] = c;
		p += 2;
	}
	cmd[n] = 0;
	if (n) ble_on_command(cmd);
}

static void on_line(const char *s)
{
	if (!strcmp(s, "OK"))    { if (busy) on_result(true);  return; }
	if (!strcmp(s, "ERROR")) { if (busy) on_result(false); return; }

	if (!strncmp(s, "+STARTUP", 8)) {
		if (st == S_BOOT) { start_config(); return; }
		if (st == S_CMD || st == S_READY) {      /* unexpected module reboot */
			link_lost();
			busy = false;
			cmd_handle = status_handle = -1;
			start_config();
		}
		return;
	}
	if (!strncmp(s, "+UUBTACLC:", 10)) {
		int h = atoi(s + 10);
		if (h >= 0) conn_handle = h;
		return;
	}
	if (!strncmp(s, "+UUBTACLD:", 10)) { link_lost(); return; }
	if (!strncmp(s, "+UUBTGRW:", 9))   { on_gatt_write(s + 9); return; }
	if (!strncmp(s, "+UU", 3)) return;          /* other unsolicited events */
	if (s[0] == '+' && busy) {
		strncpy(resp, s, sizeof resp - 1u);
		resp[sizeof resp - 1u] = 0;
	}
}

static void drain_rx(void)
{
	while (rx_tail != rx_head) {
		char c = (char)rx_ring[rx_tail];
		rx_tail = (uint16_t)((rx_tail + 1u) % RX_SIZE);
		if (c == '\r' || c == '\n') {
			if (line_len) {
				line[line_len] = 0;
				on_line(line);
				line_len = 0;
			}
		} else if (line_len < LINE_MAX) {
			line[line_len++] = c;
		}
	}
}

/* ---- public ---- */

void ble_poll(void)
{
	uint32_t now = HAL_GetTick();

	switch (st) {
	case S_POWER:
		if (now - t_state >= T_POWER_ms) {
			MX_USART1_UART_Init();
			rx_head = rx_tail = 0;
			line_len = 0;
			HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
			HAL_GPIO_WritePin(BLE_RESET_N_GPIO_Port, BLE_RESET_N_Pin, GPIO_PIN_SET);  /* release */
			st = S_BOOT;
			t_state = now;
		}
		return;
	case S_RETRY:
		if (now - t_state >= T_RETRY_ms) power_on();
		return;
	case S_OFF:
	case S_FAILED:
		return;
	default:
		break;
	}

	drain_rx();

	if (st == S_BOOT && now - t_state >= T_STARTUP_ms) {
		/* Greeting may be disabled; try talking to it anyway. */
		start_config();
	}
	if (busy && now - t_cmd >= T_CMD_ms) {
		busy = false;
		if (st == S_CMD) fail();
	}
}

bool ble_ready(void) { return st == S_READY; }
bool ble_connected(void) { return st == S_READY && conn_handle >= 0; }
bool ble_failed(void) { return st == S_FAILED; }

bool ble_notify(const char *text)
{
	if (!ble_connected() || busy || status_handle < 0) return false;
	size_t n = strlen(text);
	if (n > NOTIFY_MAX) return false;

	static const char hex[] = "0123456789ABCDEF";
	char buf[2u * NOTIFY_MAX + 1u];
	for (size_t i = 0; i < n; i++) {
		buf[2u * i]      = hex[(uint8_t)text[i] >> 4];
		buf[2u * i + 1u] = hex[(uint8_t)text[i] & 15u];
	}
	buf[2u * n] = 0;
	step = C_NOTIFY;
	send("AT+UBTGSN=%d,%d,%s", conn_handle, status_handle, buf);
	return true;
}
