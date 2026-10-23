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

#define FDCAN_BUS_TELEMETRY_REPORT_ID            0x04200000U
#define FDCAN_BUS_TELEMETRY_REPORT_PERIOD_MS     200U
#define FDCAN_PERIPHERAL_CURRENT_REPORT_ID       FDCAN_BUS_TELEMETRY_REPORT_ID /* 兼容旧别名 */
#define FDCAN_PERIPHERAL_CURRENT_REPORT_PERIOD_MS FDCAN_BUS_TELEMETRY_REPORT_PERIOD_MS /* 兼容旧别名 */

#define FDCAN_BATTERY_ALARM_REPORT_ID            0x04400000U
#define FDCAN_BATTERY_ALARM_REPORT_PERIOD_MS     500U
#define FDCAN_BATTERY_ALARM_HEARTBEAT_PERIOD_MS  FDCAN_BATTERY_ALARM_REPORT_PERIOD_MS /* 兼容旧别名：统一 500ms 心跳 */

/* 急停状态上报 (小脑 FDCAN2 总线) */
#define FDCAN_ESTOP_REPORT_ID                    0x04300000U
#define FDCAN_ESTOP_ACTIVE_REPORT_PERIOD_MS      50U   /* 急停触发锁定期间：50ms 高频周期保活 */
#define FDCAN_ESTOP_NORMAL_REPORT_PERIOD_MS      500U  /* 平时正常释放期间：500ms 低频心跳保活 */

#define FDCAN_CHARGE_MODE_CMD_ID                 0x0500FF80U
#define FDCAN_RK_CHARGE_MODE_CMD_ID              0x04500000U
#define FDCAN_RK_CHARGE_MODE_STATUS_ID           0x04500001U
#define FDCAN_CHARGE_MODE_EXIT                   0x00U
#define FDCAN_CHARGE_MODE_ENTER                  0x01U
#define FDCAN_CHARGER_CAN_TIMEOUT_MS             3000U

/* 电池总压物理合理性范围校验 (V)：过滤 BMS 通信单包瞬态 0V 或飞帧 */
#define FDCAN_BATTERY_VOLTAGE_MIN                10.0f
#define FDCAN_BATTERY_VOLTAGE_MAX                80.0f

/* 小脑声光控制接口协议定义 */
#define FDCAN_RK_INDICATOR_CMD_ID                0x04600000U
#define FDCAN_RK_INDICATOR_STATUS_ID             0x04600001U
#define FDCAN_RK_INDICATOR_REPORT_PERIOD_MS      500U

/* 小脑三色状态指示灯功能码定义 (CAN2 ID: 0x04600000, Byte 0)
 * 硬件说明：实际板载/外部指示灯为红、绿、黄 (R/G/Y) 独立三色，无法混色 (无白光)；
 * 原理图 Page 9 误将黄灯标为 RGB_B，实际物理引脚 PC14 驱动的是黄色 LED。
 */
typedef enum
{
  FDCAN_RGB_FUNC_OFF                   = 0x00U, /* 关灯 (OFF) */
  FDCAN_RGB_FUNC_P0_RED_BURST_6HZ      = 0x01U, /* P0: 急停 / 碰撞 / BMS报警 (红 爆闪 6Hz) */
  FDCAN_RGB_FUNC_P1_RED_FAST_3HZ       = 0x02U, /* P1: 严重故障 (红 快闪 3Hz) */
  FDCAN_RGB_FUNC_P3_YELLOW_DOUBLE_0_5HZ= 0x03U, /* P3: 倾倒、跌落 (黄 双连闪 0.5Hz) */
  FDCAN_RGB_FUNC_P4_YELLOW_STD_1HZ     = 0x04U, /* P4: 一般警告 (黄 标准闪 1Hz) */
  FDCAN_RGB_FUNC_P5_GREEN_PULSE_3HZ    = 0x05U, /* P5: 语音监听 / 指令确认 (绿 短促闪 3Hz) */
  FDCAN_RGB_FUNC_P6_MARQUEE_2HZ        = 0x06U, /* P6: 自主巡检 / 建图 (红黄绿三色跑马灯 2Hz) */
  FDCAN_RGB_FUNC_SOLID_RED             = 0x07U, /* 红灯常亮 */
  FDCAN_RGB_FUNC_SOLID_GREEN           = 0x08U, /* 绿灯常亮 */
  FDCAN_RGB_FUNC_SOLID_YELLOW          = 0x09U, /* 黄灯常亮 (PC14) */
  FDCAN_RGB_FUNC_SOLID_YELLOW_ALIAS    = 0x0AU  /* 黄灯常亮兼容别名 */
} FDCAN_RgbFunctionCode_t;

#define FDCAN_INDICATOR_ALARM_OFF                0x00U  /* Alarm 报警器关闭 (静音) */
#define FDCAN_INDICATOR_ALARM_ON                 0x01U  /* Alarm 报警器开启 (硬件自主间隔型鸣叫) */
#define FDCAN_INDICATOR_BUZZER_MODE_OFF          FDCAN_INDICATOR_ALARM_OFF /* 兼容别名 */
#define FDCAN_INDICATOR_BUZZER_MODE_ON           FDCAN_INDICATOR_ALARM_ON  /* 兼容别名 */

#define FDCAN_INDICATOR_DEFAULT_TIMEOUT_MS       2000U

/* 调试配置开关：置 1 时关闭小脑对蜂鸣器/Alarm 的控制接口；置 0 开放小脑控制 */
#define DEBUG_DISABLE_RK_BUZZER_CTRL             0

/* IAP 在线升级协议定义 */
#define FDCAN_IAP_CMD_ID                         0x04700000U
#define FDCAN_IAP_RESP_ID                        0x04700001U
#define IAP_BOOT_FLAG_MAGIC                      0x55AA0000U
#define IAP_HOT_BOOT_MAGIC                       0xAA550000U
#define IAP_BOOT_FLAG_ADDR                       (0x2001FC00U)  /* 预留 1KB 栈空间安全裕量，防止与 MSP 栈顶碰撞 */

/* App 固件版本号定义 (三位版本: Major.Minor.Patch，例如 v1.4.0) */
#define APP_FW_VERSION_MAJOR                     1U
#define APP_FW_VERSION_MINOR                     4U
#define APP_FW_VERSION_PATCH                     1U

void FDCAN_HandleIapCommand(const FDCAN_RxHeaderTypeDef *rxHeader, const uint8_t *rxData);
void FDCAN_IAP_JumpToBootloader(void);

typedef struct
{
  uint8_t functionCode;     /* RGB 功能码 (0x00: OFF, 0x01~0x09: 各灯效) */
  uint8_t alarmState;       /* Alarm 报警器状态: 0: 关, 1: 开 (自主间隔型鸣叫) */
  uint32_t timeoutMs;       /* 安全超时时间 (ms) */
  uint32_t lastRxTick;      /* 上次收到有效控制帧的时间戳 */
} RkIndicatorControl_t;

extern volatile RkIndicatorControl_t rkIndicatorCtrl;

void FDCAN_BatteryCanStart(void);
void FDCAN_BatteryCanTask(void);
HAL_StatusTypeDef FDCAN_SendBusTelemetryReportToRk(void);
#define FDCAN_SendPeripheralCurrentReportToRk FDCAN_SendBusTelemetryReportToRk /* 兼容旧别名 */
HAL_StatusTypeDef FDCAN_SendBatteryAlarmReportToRk(void);
HAL_StatusTypeDef FDCAN_SendEmergencyStopReportToRk(uint8_t estopActive);
HAL_StatusTypeDef FDCAN_SendChargeReplyToCharger(uint8_t batteryIndex);
#define FDCAN_SendChargeReplyToCan2 FDCAN_SendChargeReplyToCharger /* 兼容旧命名别名 */
float FDCAN_GetBatterySoc(uint8_t batteryIndex);
uint8_t FDCAN_IsBatterySocValid(uint8_t batteryIndex);
uint8_t FDCAN_IsRkRgbActive(void);
uint8_t FDCAN_GetRgbFunctionCode(void);
uint8_t FDCAN_IsRkBuzzerActive(void);
#define FDCAN_IsRkAlarmActive FDCAN_IsRkBuzzerActive
uint8_t FDCAN_GetRkAlarmState(void);
HAL_StatusTypeDef FDCAN_SendIndicatorStatusToRk(void);

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

