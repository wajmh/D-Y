/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.h
  * @brief   This file contains all the function prototypes for
  *          the adc.c file
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
#ifndef __ADC_H__
#define __ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern ADC_HandleTypeDef hadc2;

/* USER CODE BEGIN Private defines */

#define ADC_REFERENCE_VOLTAGE                         3.26f
#define ADC_MAX_RAW_VALUE                             4095.0f

/* VBUS 分压电阻：R10(30k) + R9(30k) + R54(47k) = 107kΩ，R52(2kΩ) */
#define VBUS_VOLTAGE_DIVIDER_TOP_OHM                  107000.0f
#define VBUS_VOLTAGE_DIVIDER_BOTTOM_OHM               2000.0f
#define VBUS_VOLTAGE_DIVIDER_RATIO                    ((VBUS_VOLTAGE_DIVIDER_TOP_OHM + VBUS_VOLTAGE_DIVIDER_BOTTOM_OHM) / VBUS_VOLTAGE_DIVIDER_BOTTOM_OHM)

#define CURRENT_ZERO_VOLTAGE                          1.600f
#define PERIPHERAL_DISCHARGE_CURRENT_AMPS_PER_VOLT    25.0f

/* USER CODE END Private defines */

void MX_ADC2_Init(void);

/* USER CODE BEGIN Prototypes */

/* ADC2 DMA 采样启动、停止与校准 */
void ADC2_StartDMA(void);
void ADC2_StopDMA(void);
void ADC_CalibrateCurrentOffsets(void);
void ADC_UpdateCurrents(void);
void ADC2_UpdateVbusVoltage(void);

/* 模拟量读取接口 */
float ADC_GetVbusVoltage(void);
float ADC_GetPeripheralDischargeCurrent(void);
uint16_t ADC2_GetPeripheralDischargeCurrentADC(void);
uint16_t ADC2_GetVbusVoltageADC(void);

/* 全局变量 */
extern float vbus_voltage;
extern uint16_t adc2_buffer[2];
extern float peripheral_discharge_current;

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H__ */

