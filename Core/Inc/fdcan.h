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

void MX_FDCAN1_Init(void);
void MX_FDCAN2_Init(void);
void MX_FDCAN3_Init(void);

/* USER CODE BEGIN Prototypes */
/* BMS 上报内部放电 MOS 明确状态枚举 */
typedef enum
{
  BMS_DISCHARGE_MOS_UNKNOWN = 0,    /* 未知 / 初始化 / 未收到有效帧 / 异常值 */
  BMS_DISCHARGE_MOS_PROHIBITED = 1, /* 明确禁止放电 (BMS 报文明确指示 0x00) */
  BMS_DISCHARGE_MOS_ALLOWED = 2     /* 明确允许放电 (BMS 报文明确指示 0x01) */
} BmsDischargeMosState_t;

extern volatile uint32_t battery_can_forward_count;
extern volatile uint32_t battery_can_forward_drop_count;
extern volatile float battery1_can_sum_voltage;
extern volatile float battery1_can_current;
extern volatile uint32_t battery1_can_rx_id;
extern volatile uint32_t battery1_can_rx_count;
extern volatile uint32_t battery1_can_status_last_rx_tick;
extern volatile uint8_t battery1_can_charge_mos_state;
extern volatile uint8_t battery1_can_discharge_mos_state;
extern volatile BmsDischargeMosState_t battery1_bms_discharge_state;
extern volatile uint32_t battery1_can_mos_rx_id;
extern volatile uint32_t battery1_can_mos_rx_count;
extern volatile uint32_t battery1_can_mos_last_rx_tick;
extern volatile float battery2_can_sum_voltage;
extern volatile float battery2_can_current;
extern volatile uint32_t battery2_can_rx_id;
extern volatile uint32_t battery2_can_rx_count;
extern volatile uint32_t battery2_can_status_last_rx_tick;
extern volatile uint8_t battery2_can_charge_mos_state;
extern volatile uint8_t battery2_can_discharge_mos_state;
extern volatile BmsDischargeMosState_t battery2_bms_discharge_state;
extern volatile uint32_t battery2_can_mos_rx_id;
extern volatile uint32_t battery2_can_mos_rx_count;
extern volatile uint32_t battery2_can_mos_last_rx_tick;

void MX_FDCAN1_Init(void);
void MX_FDCAN2_Init(void);
void MX_FDCAN3_Init(void);

#define FDCAN_BATTERY_ALARM_REPORT_ID            0x04400000U
#define FDCAN_BATTERY_ALARM_REPORT_PERIOD_MS     200U
#define FDCAN_BATTERY_ALARM_HEARTBEAT_PERIOD_MS  200U
#define FDCAN_BATTERY_ALARM_CLEAR_BURST_COUNT    10U

#define FDCAN_CHARGE_MODE_CMD_ID                 0x0500FF80U
#define FDCAN_RK_CHARGE_MODE_CMD_ID              0x04500000U
#define FDCAN_RK_CHARGE_MODE_STATUS_ID           0x04500001U
#define FDCAN_CHARGE_MODE_EXIT                   0x00U
#define FDCAN_CHARGE_MODE_ENTER                  0x01U

void FDCAN_BatteryCanStart(void);
void FDCAN_BatteryCanTask(void);
HAL_StatusTypeDef FDCAN_SendBatteryAlarmReportToRk(void);
HAL_StatusTypeDef FDCAN_SendChargeReplyToCan2(uint8_t batteryIndex);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __FDCAN_H__ */

