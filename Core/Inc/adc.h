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

extern ADC_HandleTypeDef hadc1;

extern ADC_HandleTypeDef hadc2;

/* USER CODE BEGIN Private defines */

#define ADC_REFERENCE_VOLTAGE                         3.3f
#define ADC_MAX_RAW_VALUE                             4095.0f

#define BATTERY_VOLTAGE_DIVIDER_TOP_OHM              55500.0f
#define BATTERY_VOLTAGE_DIVIDER_BOTTOM_OHM           2000.0f
#define BATTERY_VOLTAGE_DIVIDER_RATIO                ((BATTERY_VOLTAGE_DIVIDER_TOP_OHM + BATTERY_VOLTAGE_DIVIDER_BOTTOM_OHM) / BATTERY_VOLTAGE_DIVIDER_BOTTOM_OHM)

#define CURRENT_ZERO_VOLTAGE                          1.600f
#define LEG_CURRENT_AMPS_PER_VOLT                     25.0f
#define BATTERY_CURRENT_AMPS_PER_VOLT                 25.0f
#define PERIPHERAL_DISCHARGE_CURRENT_AMPS_PER_VOLT    25.0f

/* USER CODE END Private defines */

void MX_ADC1_Init(void);
void MX_ADC2_Init(void);

/* USER CODE BEGIN Prototypes */

/* ADC2 DMA 电池电压读取 */
void ADC1_StartDMA(void);
void ADC2_StartDMA(void);
void ADC_CalibrateLegCurrentOffsets(void);
void ADC_UpdateCurrents(void);
uint16_t ADC1_GetLegCurrentADC(uint8_t leg);
uint16_t ADC1_GetLegCurrentOffsetADC(uint8_t leg);
uint16_t ADC1_GetBattery1CurrentADC(void);
uint16_t ADC1_GetBattery2CurrentADC(void);
uint16_t ADC2_GetPeripheralDischargeCurrentADC(void);
uint16_t ADC2_GetPeripheralDischargeCurrentOffsetADC(void);
float ADC_GetLegCurrent(uint8_t leg);
float ADC_GetLeg1Current(void);
float ADC_GetLeg2Current(void);
float ADC_GetLeg3Current(void);
float ADC_GetLeg4Current(void);
float ADC_GetBattery1Current(void);
float ADC_GetBattery2Current(void);
float ADC_GetPeripheralDischargeCurrent(void);
void ADC2_UpdateBatteryVoltages(void);
uint16_t ADC2_GetBattery1ADC(void);
uint16_t ADC2_GetBattery2ADC(void);
float ADC2_GetBattery1Voltage(void);
float ADC2_GetBattery2Voltage(void);

/* 全局电池电压变量 */
extern float battery1_voltage;
extern float battery2_voltage;
extern float leg_current[4];
extern float battery1_current;
extern float battery2_current;
extern float peripheral_discharge_current;

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H__ */

