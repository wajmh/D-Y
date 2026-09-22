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
extern volatile float battery1_can_soc;
extern volatile uint8_t battery1_can_soc_valid;
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
extern volatile float battery2_can_soc;
extern volatile uint8_t battery2_can_soc_valid;
extern volatile uint32_t battery2_can_rx_id;
extern volatile uint32_t battery2_can_rx_count;
extern volatile uint32_t battery2_can_status_last_rx_tick;
extern volatile uint8_t battery2_can_charge_mos_state;
extern volatile uint8_t battery2_can_discharge_mos_state;
extern volatile BmsDischargeMosState_t battery2_bms_discharge_state;
extern volatile uint32_t battery2_can_mos_rx_id;
extern volatile uint32_t battery2_can_mos_rx_count;
extern volatile uint32_t battery2_can_mos_last_rx_tick;
extern volatile uint32_t charger_can_rx_count;
extern volatile uint32_t charger_can_rx_id;
extern volatile uint32_t charger_can_last_rx_tick;
extern volatile uint32_t charger_can_rx_interval_ms;
extern volatile uint8_t rk_charge_mode_request;
extern volatile uint8_t charge_mode_active;

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
#define FDCAN_CHARGER_CAN_TIMEOUT_MS             3000U

/* 小脑声光控制接口协议定义 */
#define FDCAN_RK_INDICATOR_CMD_ID                0x04600000U
#define FDCAN_RK_INDICATOR_STATUS_ID             0x04600001U
#define FDCAN_RK_INDICATOR_REPORT_PERIOD_MS      500U

#define FDCAN_INDICATOR_CTRL_RGB_MASK            0x01U
#define FDCAN_INDICATOR_CTRL_BUZZER_MASK         0x02U

#define FDCAN_INDICATOR_RGB_MODE_SOLID           0x00U
#define FDCAN_INDICATOR_RGB_MODE_BLINK           0x01U

#define FDCAN_INDICATOR_BUZZER_MODE_OFF          0x00U
#define FDCAN_INDICATOR_BUZZER_MODE_ON           0x01U
#define FDCAN_INDICATOR_BUZZER_MODE_BEEP_ONCE    0x02U
#define FDCAN_INDICATOR_BUZZER_MODE_BLINK        0x03U

#define FDCAN_INDICATOR_DEFAULT_TIMEOUT_MS       3000U
#define FDCAN_INDICATOR_BEEP_ONCE_DURATION_MS    200U

typedef struct
{
  uint8_t rgbTakeover;      /* 1: 小脑接管 RGB 控制权, 0: 本地自动电量指示 */
  uint8_t buzzerTakeover;   /* 1: 小脑接管蜂鸣器控制权, 0: 本地静音 */
  uint8_t r;                /* 红色分量 (0/1) */
  uint8_t g;                /* 绿色分量 (0/1) */
  uint8_t b;                /* 蓝色分量 (0/1) */
  uint8_t rgbMode;          /* RGB 模式: 0:常亮, 1:闪烁 */
  uint8_t buzzerMode;       /* 蜂鸣器模式: 0:关, 1:长鸣, 2:单次短鸣(200ms), 3:周期鸣叫 */
  uint32_t halfPeriodMs;    /* 闪烁/鸣叫半周期 (ms) */
  uint32_t timeoutMs;       /* 安全超时时间 (ms, 0表示无限期) */
  uint32_t lastRxTick;      /* 上次收到有效控制帧的时间戳 */
  uint32_t beepStartTick;   /* 单次短鸣触发时间戳 */
  uint8_t beepActive;       /* 单次短鸣进行中标志 */
} RkIndicatorControl_t;

extern volatile RkIndicatorControl_t rkIndicatorCtrl;

void FDCAN_BatteryCanStart(void);
void FDCAN_BatteryCanTask(void);
HAL_StatusTypeDef FDCAN_SendBatteryAlarmReportToRk(void);
HAL_StatusTypeDef FDCAN_SendChargeReplyToCharger(uint8_t batteryIndex);
#define FDCAN_SendChargeReplyToCan2 FDCAN_SendChargeReplyToCharger /* 兼容旧命名别名 */
float FDCAN_GetBatterySoc(uint8_t batteryIndex);
uint8_t FDCAN_IsBatterySocValid(uint8_t batteryIndex);
uint8_t FDCAN_IsRkRgbActive(void);
uint8_t FDCAN_IsRkBuzzerActive(void);
HAL_StatusTypeDef FDCAN_SendIndicatorStatusToRk(uint8_t rOutput, uint8_t gOutput, uint8_t bOutput, uint8_t buzzerOutput);

/* Bus-Off 自动恢复接口 */
void FDCAN_CheckAndRecoverAllBusOff(void);
uint8_t FDCAN_IsAnyBusOff(void);

/* Bus-Off 恢复统计（用于调试和遥测，volatile 确保实时性） */
extern volatile uint32_t fdcan1_busoff_recovery_count;
extern volatile uint32_t fdcan1_busoff_recovery_fail_count;
extern volatile uint32_t fdcan2_busoff_recovery_count;
extern volatile uint32_t fdcan2_busoff_recovery_fail_count;
extern volatile uint32_t fdcan3_busoff_recovery_count;
extern volatile uint32_t fdcan3_busoff_recovery_fail_count;

/* Bus-Off 诊断指标（PSR/ECR 快照，用于背景遥测与健康度排查） */
extern volatile uint32_t fdcan1_last_psr;
extern volatile uint32_t fdcan1_last_ecr;
extern volatile uint32_t fdcan2_last_psr;
extern volatile uint32_t fdcan2_last_ecr;
extern volatile uint32_t fdcan3_last_psr;
extern volatile uint32_t fdcan3_last_ecr;

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __FDCAN_H__ */

