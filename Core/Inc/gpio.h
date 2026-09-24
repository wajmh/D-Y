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
/* USER CODE BEGIN Private defines */
/* ==================== 电池包1 MOS 控制引脚 ==================== */
#define BAT1_DISCHARGE_MOS_GPIO_Port       GPIOB
#define BAT1_DISCHARGE_MOS_Pin             GPIO_PIN_6   /* PB6: 主放电 MOS */
#define BAT1_RECHARGE_MOS_GPIO_Port        GPIOB
#define BAT1_RECHARGE_MOS_Pin              GPIO_PIN_7   /* PB7: 回充 MOS */
#define BAT1_PRE_DISCHARGE_MOS_GPIO_Port   GPIOB
#define BAT1_PRE_DISCHARGE_MOS_Pin         GPIO_PIN_9   /* PB9: 预放电 MOS */

/* ==================== 电池包2 MOS 控制引脚 ==================== */
#define BAT2_DISCHARGE_MOS_GPIO_Port       GPIOC
#define BAT2_DISCHARGE_MOS_Pin             GPIO_PIN_11  /* PC11: 主放电 MOS */
#define BAT2_RECHARGE_MOS_GPIO_Port        GPIOC
#define BAT2_RECHARGE_MOS_Pin              GPIO_PIN_10  /* PC10: 回充 MOS */
#define BAT2_PRE_DISCHARGE_MOS_GPIO_Port   GPIOC
#define BAT2_PRE_DISCHARGE_MOS_Pin         GPIO_PIN_12  /* PC12: 预放电 MOS */

/* ==================== 充电与外设供电控制 ==================== */
#define BAT_CHARGE_MOS_GPIO_Port           GPIOB
#define BAT_CHARGE_MOS_Pin                 GPIO_PIN_10  /* PB10: 单路总充电控制 MOS (直通 VBUS) */
#define PERIPHERAL_POWER_GPIO_Port         GPIOA
#define PERIPHERAL_POWER_Pin               GPIO_PIN_5   /* PA5: 外设供电 MOS */

/* 兼容旧宏定义 */
#define BAT1_CHARGE_MOS_GPIO_Port          BAT_CHARGE_MOS_GPIO_Port
#define BAT1_CHARGE_MOS_Pin                BAT_CHARGE_MOS_Pin
#define BAT2_CHARGE_MOS_GPIO_Port          BAT_CHARGE_MOS_GPIO_Port
#define BAT2_CHARGE_MOS_Pin                BAT_CHARGE_MOS_Pin

/* ==================== 2路反电势吸收保护 ==================== */
#define BACK_EMF_ABSORB_1_GPIO_Port        GPIOB
#define BACK_EMF_ABSORB_1_Pin              GPIO_PIN_12  /* PB12: 电池1回路反电势泄放 MOS */
#define BACK_EMF_ABSORB_2_GPIO_Port        GPIOB
#define BACK_EMF_ABSORB_2_Pin              GPIO_PIN_11  /* PB11: 电池2回路反电势泄放 MOS */
#define BACK_EMF_ABSORB_GPIO_Port          BACK_EMF_ABSORB_2_GPIO_Port
#define BACK_EMF_ABSORB_Pin                BACK_EMF_ABSORB_2_Pin

/* ==================== 新增：隔离 DCDC 使能控制 ==================== */
#define DCDC_EN_12V_GPIO_Port              GPIOA
#define DCDC_EN_12V_Pin                    GPIO_PIN_7   /* PA7: 12V 使能 */
#define DCDC_EN_24V_GPIO_Port              GPIOC
#define DCDC_EN_24V_Pin                    GPIO_PIN_4   /* PC4: 24V 使能 */

/* ==================== 新增：三色灯与蜂鸣器控制 ==================== */
#define RGB_LED_R_GPIO_Port                GPIOC
#define RGB_LED_R_Pin                      GPIO_PIN_15  /* PC15: 红灯 */
#define RGB_LED_G_GPIO_Port                GPIOC
#define RGB_LED_G_Pin                      GPIO_PIN_2   /* PC2: 绿灯 */
#define RGB_LED_B_GPIO_Port                GPIOC
#define RGB_LED_B_Pin                      GPIO_PIN_14  /* PC14: 蓝灯 */
#define BUZZER_ALARM_GPIO_Port             GPIOC
#define BUZZER_ALARM_Pin                   GPIO_PIN_13  /* PC13: 蜂鸣器 */

/* 急停输入（已停用，硬件未连接）
#define EMERGENCY_STOP_GPIO_Port           GPIOD
#define EMERGENCY_STOP_Pin                 GPIO_PIN_2   // PD2: 急停输入 (来自运控板光耦)
*/

/* 默认按高电平打开 MOS；如果硬件是低电平有效，需要改这里 */
#define POWER_SWITCH_ON                    GPIO_PIN_SET
#define POWER_SWITCH_OFF                   GPIO_PIN_RESET
/* DCDC 电源使能控制：硬件为低电平使能 (Active-Low) */
#define POWER_DCDC_ON                      GPIO_PIN_RESET
#define POWER_DCDC_OFF                     GPIO_PIN_SET
/* 急停有效电平与消抖参数（已停用）
#define POWER_ESTOP_ACTIVE_STATE           GPIO_PIN_RESET
#define POWER_ESTOP_DEBOUNCE_MS            30U
*/
/* 预放电等待时间，单位 ms */
#define POWER_PRE_DISCHARGE_DELAY_MS       400U
/* 主放电 MOS 打开后预充 MOS 延时关闭时间（重叠导通缓冲），单位 ms */
#define POWER_PRE_DISCHARGE_OVERLAP_MS     200U
#define POWER_PRE_DISCHARGE_DELAY          POWER_PRE_DISCHARGE_OVERLAP_MS
/* CAN 超过该时间未收到状态帧时，认为电池通信掉线 */
#define POWER_BATTERY_CAN_TIMEOUT_MS       2000U
/* 双电池回充 MOS 切换阈值，两个电池压差超过该值时只开高电压电池回充 */
#define POWER_RECHARGE_BALANCE_DIFF        0.10f
/* 充电模式下双电池压差在该阈值内时双开充电回路 */
#define POWER_CHARGE_BALANCE_DIFF          0.10f
/* 充电模式下向 CAN2 发送所选低压电池回复帧的周期 (ms) */
#define POWER_CHARGE_REPLY_PERIOD_MS       200U
/* 关低压回充 MOS 后，等待控制脚读回 OFF 的超时时间 */
#define POWER_RECHARGE_SWITCH_TIMEOUT_MS   5U
/* 双回充 MOS 打开后，某路电流占总电流低于该比例，认为疑似未放电 */
#define POWER_BATTERY_CURRENT_MIN_SHARE    0.10f

/* VBUS 泄放动作阈值：放电模式下超过 85V 通过 PB11 PWM (50%) 开启泄放；回落至 84V 关闭 (1V滞回防抖) */
#define POWER_VBUS_RELEASE_OPEN_VOLTAGE    85.0f
#define POWER_VBUS_RELEASE_CLOSE_VOLTAGE   84.0f
#define POWER_VBUS_RELEASE_PWM_DUTY        15U   /* PB11 TIM2_CH4 PWM 占空比 10% (ARR=99, Pulse=50) */

/* 电池物理在线判定电压阈值：低于该值判定为电池拔出/无电压 (V) */
#define BATTERY_PHYSICAL_PRESENT_VOLTAGE   20.0f
/* 电池异常报警状态定义 */
#define BATTERY_ALARM_STATUS_NORMAL                  0x00U /* 正常在线 */
#define BATTERY_ALARM_STATUS_CAN_COMM_LOST           0x01U /* CAN 通信掉线；已处于放电态时保持本地路径 */
#define BATTERY_ALARM_STATUS_REMOVED                 0x02U /* 电池物理拔出 / 无电压 */
#define BATTERY_ALARM_STATUS_BMS_PROHIBIT_DISCHARGE  0x03U /* BMS 明确禁止放电，对应本地放电 MOS 和回充 MOS 已关闭 */
#define BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN       0x04U /* BMS 放电 MOS 状态未知 / 无效；放电态维持本地路径供电防空中断电，非放电态禁止准入 */

/* 状态指示灯 SOC 阈值：>50% 绿灯常亮，20%~50% 蓝灯常亮，<20% 红灯常亮 */
#define POWER_STATUS_LED_SOC_HIGH_THRESHOLD          50.0f
#define POWER_STATUS_LED_SOC_LOW_THRESHOLD           20.0f
/* Bus-Off 错误蓝灯闪烁半周期 (ms) */
#define POWER_STATUS_LED_BUSOFF_BLINK_MS             200U
/* 开机自检完成就绪蜂鸣时长 (ms)：硬件为间歇脉冲型蜂鸣器，90ms 刚好触发单次短“滴”并避免触发第二声 */
#define POWER_BOOT_BEEP_DURATION_MS                  90U

/* ==================== 稳压电源/母线泄放测试模式配置 ====================
 * POWER_TEST_BENCH_SUPPLY_MODE:
 * 0: 正常双电池工作模式（严格依赖电池 CAN 与 BMS 握手）
 * 1: 稳压电源测试模式（屏蔽电池 CAN 通信检测，直接支持稳压电源测试泄放功能）
 *
 * POWER_TEST_SUPPLY_INPUT_CHANNEL:
 * 1: 稳压电源接 BAT1 接口（执行预充 400ms -> 导通 BAT1 主放电 MOS 上电至 VBUS）
 * 2: 稳压电源接 BAT2 接口（执行预充 400ms -> 导通 BAT2 主放电 MOS 上电至 VBUS）
 * 0: 稳压电源直接接 VBUS 母线端（保持双电池 MOS 全部关断，直接进行 VBUS 采样泄放）
 */
#define POWER_TEST_BENCH_SUPPLY_MODE          0
#define POWER_TEST_SUPPLY_INPUT_CHANNEL       1U

/* USER CODE END Private defines */

void MX_GPIO_Init(void);

/* USER CODE BEGIN Prototypes */
extern volatile uint8_t bat_charge_mos_state;
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
extern volatile uint8_t back_emf_absorb_1_state;
extern volatile uint8_t back_emf_absorb_2_state;
extern volatile uint8_t dcdc_12v_state;
extern volatile uint8_t dcdc_24v_state;

void Power_UpdateGpioDebugStates(void);
void Power_AllMosOff(void);
HAL_StatusTypeDef Power_EnterDischargeMode(void);
void Power_ModeTask(void);
void Power_DischargeModeTask(void);
void Power_ExitDischargeMode(void);
HAL_StatusTypeDef Power_EnterChargeMode(void);
void Power_ExitChargeMode(void);
void Power_UpdateVbusPowerRelease(void);
/* 急停检测函数（已停用，硬件未连接）
GPIO_PinState Power_ReadEmergencyStop(void);
uint8_t Power_IsEmergencyStopActive(void);
*/
uint8_t Power_GetBattery1AlarmStatus(void);
uint8_t Power_GetBattery2AlarmStatus(void);
uint8_t Power_IsBatteryPhysicallyPresent(uint8_t batteryIndex);

/* 辅助外设控制 API */
void Power_SetDcdc12V(uint8_t enable);
void Power_SetDcdc24V(uint8_t enable);
void Power_SetRgbLed(uint8_t r, uint8_t g, uint8_t b);
void Power_SetBuzzer(uint8_t on);
void Power_UpdateStatusIndicators(void);
void Power_UpdateStatusLed(void);

uint8_t Power_GetPhysicalR(void);
uint8_t Power_GetPhysicalG(void);
uint8_t Power_GetPhysicalB(void);
uint8_t Power_GetPhysicalBuzzer(void);
void Power_TriggerBootBeep(void);

/* IAP 无缝热接力支持接口 */
uint8_t Power_GetActiveDischargeMask(void);
void Power_CheckAndHandleHotBoot(void);
uint8_t Power_IsHotBoot(void);
uint8_t Power_GetHotBootBatMask(void);
void Power_HotBootGpioInit(uint8_t batMask);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__ GPIO_H__ */

