/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define WAKE_Pin GPIO_PIN_0
#define WAKE_GPIO_Port GPIOA
#define WAKE_EXTI_IRQn EXTI0_IRQn
#define VBAT_SENSE_Pin GPIO_PIN_1
#define VBAT_SENSE_GPIO_Port GPIOA
#define STATUS_LED_Pin GPIO_PIN_3
#define STATUS_LED_GPIO_Port GPIOA
#define DAC_STIM_Pin GPIO_PIN_4
#define DAC_STIM_GPIO_Port GPIOA
#define DAC_REF_Pin GPIO_PIN_5
#define DAC_REF_GPIO_Port GPIOA
#define OUT_EN_Pin GPIO_PIN_6
#define OUT_EN_GPIO_Port GPIOA
#define BLE_EN_Pin GPIO_PIN_7
#define BLE_EN_GPIO_Port GPIOA
#define BLE_DSR_Pin GPIO_PIN_0
#define BLE_DSR_GPIO_Port GPIOB
#define EN_RAIL_MCU_Pin GPIO_PIN_8
#define EN_RAIL_MCU_GPIO_Port GPIOA
#define BLE_RXD_Pin GPIO_PIN_9
#define BLE_RXD_GPIO_Port GPIOA
#define BLE_TXD_Pin GPIO_PIN_10
#define BLE_TXD_GPIO_Port GPIOA
#define USB_DM_Pin GPIO_PIN_11
#define USB_DM_GPIO_Port GPIOA
#define USB_DP_Pin GPIO_PIN_12
#define USB_DP_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define BLE_RESET_N_Pin GPIO_PIN_4
#define BLE_RESET_N_GPIO_Port GPIOB
#define STAT_Pin GPIO_PIN_5
#define STAT_GPIO_Port GPIOB
#define BLE_DTR_Pin GPIO_PIN_6
#define BLE_DTR_GPIO_Port GPIOB
#define VBUS_SENSE_Pin GPIO_PIN_7
#define VBUS_SENSE_GPIO_Port GPIOB
#define VBUS_SENSE_EXTI_IRQn EXTI9_5_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
