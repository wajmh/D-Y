/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.h
  * @brief   This file contains all the function prototypes for
  *          the gpio.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GPIO_H__
#define __GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */
/* 电池包1 MOS 控制引脚 */
#define BAT1_CHARGE_MOS_GPIO_Port          GPIOC
#define BAT1_CHARGE_MOS_Pin                GPIO_PIN_12
#define BAT1_DISCHARGE_MOS_GPIO_Port       GPIOA
#define BAT1_DISCHARGE_MOS_Pin             GPIO_PIN_0
#define BAT1_RECHARGE_MOS_GPIO_Port        GPIOA
#define BAT1_RECHARGE_MOS_Pin              GPIO_PIN_4//回充
#define BAT1_PRE_DISCHARGE_MOS_GPIO_Port   GPIOB
#define BAT1_PRE_DISCHARGE_MOS_Pin         GPIO_PIN_0//预放电

/* 电池包2 MOS 控制引脚 */
#define BAT2_CHARGE_MOS_GPIO_Port          GPIOC
#define BAT2_CHARGE_MOS_Pin                GPIO_PIN_11
#define BAT2_DISCHARGE_MOS_GPIO_Port       GPIOA
#define BAT2_DISCHARGE_MOS_Pin             GPIO_PIN_5
#define BAT2_RECHARGE_MOS_GPIO_Port        GPIOC
#define BAT2_RECHARGE_MOS_Pin              GPIO_PIN_4
#define BAT2_PRE_DISCHARGE_MOS_GPIO_Port   GPIOB
#define BAT2_PRE_DISCHARGE_MOS_Pin         GPIO_PIN_1

/* 公共电源控制和急停输入 */
#define BACK_EMF_ABSORB_GPIO_Port          GPIOB
#define BACK_EMF_ABSORB_Pin                GPIO_PIN_11//反电吸收
#define PERIPHERAL_POWER_GPIO_Port         GPIOB
#define PERIPHERAL_POWER_Pin               GPIO_PIN_10//外设供电
#define EMERGENCY_STOP_GPIO_Port           GPIOD
#define EMERGENCY_STOP_Pin                 GPIO_PIN_2//急停

/* 默认按高电平打开 MOS；如果硬件是低电平有效，需要改这里 */
#define POWER_SWITCH_ON                    GPIO_PIN_SET
#define POWER_SWITCH_OFF                   GPIO_PIN_RESET
/* 急停有效电平：如果急停低电平有效，改成 GPIO_PIN_RESET */
#define POWER_ESTOP_ACTIVE_STATE           GPIO_PIN_RESET
#define POWER_ESTOP_DEBOUNCE_MS            30U
/* 预放电等待时间，单位 ms */
#define POWER_PRE_DISCHARGE_DELAY_MS       400U
/* dcdc 启动延迟，单位 ms */
#define POWER_PRE_DISCHARGE_DELAY          200U
/* CAN 超过该时间未收到状态帧时，认为电池通信掉线 */
#define POWER_BATTERY_CAN_TIMEOUT_MS       2000U
/* 双电池回充 MOS 切换阈值，两个电池压差超过该值时只开高电压电池回充 */
#define POWER_RECHARGE_BALANCE_DIFF        0.10f
/* 关低压回充 MOS 后，等待控制脚读回 OFF 的超时时间 */
#define POWER_RECHARGE_SWITCH_TIMEOUT_MS   5U
/* 双回充 MOS 打开后，某路电流占总电流低于该比例，认为疑似未放电 */
#define POWER_BATTERY_CURRENT_MIN_SHARE    0.10f
/* 电流占比异常持续时间，单位 ms */
/* 电池物理在线判定电压阈值，低于该值判定为电池拔出，高于等于该值判定为物理在线 (V)，暂定 60V */
#define BATTERY_PHYSICAL_PRESENT_VOLTAGE   50.0f
/* 电池异常报警状态定义 */
#define BATTERY_ALARM_STATUS_NORMAL                  0x00U /* 正常在线 */
#define BATTERY_ALARM_STATUS_CAN_COMM_LOST           0x01U /* CAN 通信掉线；已处于放电态时保持本地路径 */
#define BATTERY_ALARM_STATUS_REMOVED                 0x02U /* 电池物理拔出 / 无电压 */
#define BATTERY_ALARM_STATUS_BMS_PROHIBIT_DISCHARGE  0x03U /* BMS 明确禁止放电，对应本地放电 MOS 和回充 MOS 已关闭 */
#define BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN       0x04U /* BMS 放电 MOS 状态未知 / 无效，对应本地路径已关闭 */
/* USER CODE END Private defines */

void MX_GPIO_Init(void);

/* USER CODE BEGIN Prototypes */
extern volatile uint8_t bat1_charge_mos_state;
extern volatile uint8_t bat1_discharge_mos_state;
extern volatile uint8_t bat1_recharge_mos_state;
extern volatile uint8_t bat1_pre_discharge_mos_state;
extern volatile uint8_t bat2_charge_mos_state;
extern volatile uint8_t bat2_discharge_mos_state;
extern volatile uint8_t bat2_recharge_mos_state;
extern volatile uint8_t bat2_pre_discharge_mos_state;
extern volatile uint8_t peripheral_power_state;
extern volatile uint8_t back_emf_absorb_state;

void Power_UpdateGpioDebugStates(void);
void Power_AllMosOff(void);
HAL_StatusTypeDef Power_EnterDischargeMode(void);
void Power_DischargeModeTask(void);
void Power_ExitDischargeMode(void);
GPIO_PinState Power_ReadEmergencyStop(void);
uint8_t Power_IsEmergencyStopActive(void);
uint8_t Power_GetBattery1AlarmStatus(void);
uint8_t Power_GetBattery2AlarmStatus(void);
uint8_t Power_IsBatteryPhysicallyPresent(uint8_t batteryIndex);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__ GPIO_H__ */

