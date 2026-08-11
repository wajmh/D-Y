/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    fdcan.h
  * @brief   This file contains all the function prototypes for
  *          the fdcan.c file
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
#ifndef __FDCAN_H__
#define __FDCAN_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern FDCAN_HandleTypeDef hfdcan1;

extern FDCAN_HandleTypeDef hfdcan2;

extern FDCAN_HandleTypeDef hfdcan3;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

/* USER CODE BEGIN Prototypes */
extern volatile float battery1_can_sum_voltage;
extern volatile float battery1_can_current;
extern volatile uint32_t battery1_can_rx_count;
extern volatile uint32_t battery1_can_status_last_rx_tick;
extern volatile uint8_t battery1_can_charge_mos_state;
extern volatile uint8_t battery1_can_discharge_mos_state;
extern volatile uint32_t battery1_can_mos_rx_count;
extern volatile uint32_t battery1_can_mos_last_rx_tick;
extern volatile float battery2_can_sum_voltage;
extern volatile float battery2_can_current;
extern volatile uint32_t battery2_can_rx_count;
extern volatile uint32_t battery2_can_status_last_rx_tick;
extern volatile uint8_t battery2_can_charge_mos_state;
extern volatile uint8_t battery2_can_discharge_mos_state;
extern volatile uint32_t battery2_can_mos_rx_count;
extern volatile uint32_t battery2_can_mos_last_rx_tick;

void MX_FDCAN1_Init(void);
void MX_FDCAN2_Init(void);
void MX_FDCAN3_Init(void);

void FDCAN_BatteryCanStart(void);
void FDCAN_BatteryCanTask(void);
HAL_StatusTypeDef FDCAN_SendChargeReplyToCan2(uint8_t batteryIndex);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __FDCAN_H__ */

