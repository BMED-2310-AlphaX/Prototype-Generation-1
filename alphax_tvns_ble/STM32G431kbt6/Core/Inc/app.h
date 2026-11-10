/* AlphaX tVNS application: sessions, power sequencing, safety checks.
 * main.c calls app_init() once after the CubeMX init functions and app_poll()
 * on every pass of the main loop. */
#ifndef APP_H
#define APP_H

#include <stdint.h>

void app_init(void);
void app_poll(void);
void app_gpio_irq(uint16_t pin);    /* from HAL_GPIO_EXTI_Callback */

#endif
