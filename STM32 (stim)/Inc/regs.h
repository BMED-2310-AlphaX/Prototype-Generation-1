/* Minimal STM32F303xE register map (RM0316) for the peripherals this project uses.
 * The project has no CMSIS/HAL, so only what is needed is defined here. */
#ifndef REGS_H
#define REGS_H

#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

/* RCC */
#define RCC_BASE        0x40021000u
#define RCC_CR          REG(RCC_BASE + 0x00)
#define RCC_CFGR        REG(RCC_BASE + 0x04)
#define RCC_AHBENR      REG(RCC_BASE + 0x14)
#define RCC_APB2ENR     REG(RCC_BASE + 0x18)
#define RCC_APB1ENR     REG(RCC_BASE + 0x1C)
#define RCC_CSR         REG(RCC_BASE + 0x24)
#define RCC_CFGR2       REG(RCC_BASE + 0x2C)

#define RCC_CR_HSEON    (1u << 16)
#define RCC_CR_HSERDY   (1u << 17)
#define RCC_CR_HSEBYP   (1u << 18)
#define RCC_CR_PLLON    (1u << 24)
#define RCC_CR_PLLRDY   (1u << 25)

#define RCC_AHBENR_GPIOA (1u << 17)
#define RCC_AHBENR_GPIOB (1u << 18)
#define RCC_AHBENR_GPIOC (1u << 19)
#define RCC_APB1ENR_TIM2   (1u << 0)
#define RCC_APB1ENR_USART2 (1u << 17)
#define RCC_APB1ENR_DAC1   (1u << 29)

#define RCC_CSR_RMVF     (1u << 24)
#define RCC_CSR_PINRSTF  (1u << 26)
#define RCC_CSR_PORRSTF  (1u << 27)
#define RCC_CSR_SFTRSTF  (1u << 28)
#define RCC_CSR_IWDGRSTF (1u << 29)

/* FLASH */
#define FLASH_ACR       REG(0x40022000u)
#define FLASH_ACR_PRFTBE (1u << 4)

/* GPIO */
#define GPIOA_BASE      0x48000000u
#define GPIOB_BASE      0x48000400u
#define GPIOC_BASE      0x48000800u
#define GPIO_MODER(b)   REG((b) + 0x00)
#define GPIO_PUPDR(b)   REG((b) + 0x0C)
#define GPIO_IDR(b)     REG((b) + 0x10)
#define GPIO_BSRR(b)    REG((b) + 0x18)
#define GPIO_AFRL(b)    REG((b) + 0x20)

/* TIM2 (32-bit) */
#define TIM2_BASE       0x40000000u
#define TIM2_CR1        REG(TIM2_BASE + 0x00)
#define TIM2_CR2        REG(TIM2_BASE + 0x04)
#define TIM2_DIER       REG(TIM2_BASE + 0x0C)
#define TIM2_SR         REG(TIM2_BASE + 0x10)
#define TIM2_EGR        REG(TIM2_BASE + 0x14)
#define TIM2_CNT        REG(TIM2_BASE + 0x24)
#define TIM2_PSC        REG(TIM2_BASE + 0x28)
#define TIM2_ARR        REG(TIM2_BASE + 0x2C)
#define TIM_CR1_CEN     (1u << 0)
#define TIM_CR1_URS     (1u << 2)
#define TIM_CR1_ARPE    (1u << 7)
#define TIM_CR2_MMS_UPDATE (2u << 4)
#define TIM_DIER_UIE    (1u << 0)
#define TIM_SR_UIF      (1u << 0)
#define TIM_EGR_UG      (1u << 0)
#define TIM2_IRQn       28

/* DAC1 */
#define DAC_BASE        0x40007400u
#define DAC_CR          REG(DAC_BASE + 0x00)
#define DAC_DHR12R1     REG(DAC_BASE + 0x08)
#define DAC_DHR12R2     REG(DAC_BASE + 0x14)
#define DAC_DOR1        REG(DAC_BASE + 0x2C)
#define DAC_CR_EN1      (1u << 0)
#define DAC_CR_TEN1     (1u << 2)
#define DAC_CR_TSEL1_MASK (7u << 3)
#define DAC_CR_TSEL1_TIM2 (4u << 3)
#define DAC_CR_EN2      (1u << 16)

/* USART2 */
#define USART2_BASE     0x40004400u
#define USART2_CR1      REG(USART2_BASE + 0x00)
#define USART2_BRR      REG(USART2_BASE + 0x0C)
#define USART2_ISR      REG(USART2_BASE + 0x1C)
#define USART2_ICR      REG(USART2_BASE + 0x20)
#define USART2_RDR      REG(USART2_BASE + 0x24)
#define USART2_TDR      REG(USART2_BASE + 0x28)
#define USART_CR1_UE    (1u << 0)
#define USART_CR1_RE    (1u << 2)
#define USART_CR1_TE    (1u << 3)
#define USART_ISR_ORE   (1u << 3)
#define USART_ISR_RXNE  (1u << 5)
#define USART_ISR_TXE   (1u << 7)
#define USART_ICR_ORECF (1u << 3)

/* IWDG */
#define IWDG_KR         REG(0x40003000u)
#define IWDG_PR         REG(0x40003004u)
#define IWDG_RLR        REG(0x40003008u)
#define IWDG_SR         REG(0x4000300Cu)

/* Cortex-M4 core */
#define SYST_CSR        REG(0xE000E010u)
#define SYST_RVR        REG(0xE000E014u)
#define SYST_CVR        REG(0xE000E018u)
#define SCB_SHPR3       REG(0xE000ED20u)
#define NVIC_ISER0      REG(0xE000E100u)
#define NVIC_ICER0      REG(0xE000E180u)
#define NVIC_ICPR0      REG(0xE000E280u)

static inline void irq_disable(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void irq_enable(void)  { __asm volatile ("cpsie i" ::: "memory"); }

#endif
