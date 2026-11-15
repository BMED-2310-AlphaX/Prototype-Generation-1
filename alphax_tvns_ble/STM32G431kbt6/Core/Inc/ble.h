/* u-blox ANNA-B112 (u-connectXpress AT firmware) as a GATT server.
 *
 * Exposes the same service as the ESP32 firmware, so the iOS app works unchanged:
 *   service 6E5A0001-..., command 6E5A0002-... (write), status 6E5A0003-... (notify)
 *
 * Everything is non-blocking: ble_poll() advances a state machine and must be
 * called from the main loop. Command syntax follows the u-connectXpress AT
 * commands manual UBX-14044127 R52, chapters 6 and 12.
 *
 * Wiring (schematic block 12): USART1 PA9 -> RXD, PA10 <- TXD, no flow control
 * (module CTS tied to GND). BLE_EN PA7 switches module power through U8.
 * BLE_RESET_N PB4 is open drain. BLE_DSR PB0 is held low (asserted).
 */
#ifndef BLE_H
#define BLE_H

#include <stdbool.h>
#include <stdint.h>

#define BLE_DEVICE_NAME "AlphaX-01"

void ble_init(void);                  /* power the module and start configuring it */
void ble_poll(void);                  /* call every main-loop pass */
void ble_power_off(void);             /* safe shutdown order: lines low, then power */
bool ble_ready(void);                 /* GATT service is up and advertising */
bool ble_connected(void);
bool ble_failed(void);                /* gave up after repeated configuration errors */
bool ble_notify(const char *text);    /* status notification; false if busy or not connected */

/* Implemented by the application. */
void ble_on_command(const char *cmd);   /* text written to the command characteristic */
void ble_on_disconnect(void);

#endif
