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
#define GIMBAL_Y_STBY_Pin GPIO_PIN_0
#define GIMBAL_Y_STBY_GPIO_Port GPIOC
#define GIMBAL_X_STBY_Pin GPIO_PIN_1
#define GIMBAL_X_STBY_GPIO_Port GPIOC
#define BOOST_EN_Pin GPIO_PIN_2
#define BOOST_EN_GPIO_Port GPIOC
#define GIMBAL_X_EN_Pin GPIO_PIN_0
#define GIMBAL_X_EN_GPIO_Port GPIOA
#define GIMBAL_Y_EN_Pin GPIO_PIN_6
#define GIMBAL_Y_EN_GPIO_Port GPIOA
#define SOFT_I2C_SCL_Pin GPIO_PIN_10
#define SOFT_I2C_SCL_GPIO_Port GPIOB
#define SOFT_I2C_SDA_Pin GPIO_PIN_11
#define SOFT_I2C_SDA_GPIO_Port GPIOB
#define BOOT0_CTRL_Pin GPIO_PIN_8
#define BOOT0_CTRL_GPIO_Port GPIOB
#define G431_FAULT_OUT_Pin GPIO_PIN_12
#define G431_FAULT_OUT_GPIO_Port GPIOC
#define G431_ENABLE_IN_Pin GPIO_PIN_2
#define G431_ENABLE_IN_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
