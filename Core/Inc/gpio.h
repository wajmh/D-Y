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

/* ==================== 反电势吸收保护 (TIM8 互补 PWM) ==================== */
#define BACK_EMF_ABSORB_CH2_GPIO_Port      GPIOC
#define BACK_EMF_ABSORB_CH2_Pin            GPIO_PIN_7   /* PC7: TIM8_CH2 泄放2 MOS (Q27) */
#define BACK_EMF_ABSORB_CH2N_GPIO_Port     GPIOB
#define BACK_EMF_ABSORB_CH2N_Pin           GPIO_PIN_0   /* PB0: TIM8_CH2N 泄放1 MOS (Q7) */

/* 兼容旧宏定义 */
#define BACK_EMF_ABSORB_1_GPIO_Port        BACK_EMF_ABSORB_CH2N_GPIO_Port
#define BACK_EMF_ABSORB_1_Pin              BACK_EMF_ABSORB_CH2N_Pin
#define BACK_EMF_ABSORB_2_GPIO_Port        BACK_EMF_ABSORB_CH2_GPIO_Port
#define BACK_EMF_ABSORB_2_Pin              BACK_EMF_ABSORB_CH2_Pin
#define BACK_EMF_ABSORB_GPIO_Port          BACK_EMF_ABSORB_2_GPIO_Port
#define BACK_EMF_ABSORB_Pin                BACK_EMF_ABSORB_2_Pin

/* ==================== 板载 3.3V 独立状态指示灯 (V1.3 新增) ==================== */
#define BOARD_LED2_GPIO_Port               GPIOB
#define BOARD_LED2_Pin                     GPIO_PIN_1   /* PB1: 板载 LED2 (翠绿心跳) */
#define BOARD_LED3_GPIO_Port               GPIOB
#define BOARD_LED3_Pin                     GPIO_PIN_2   /* PB2: 板载 LED3 (翠绿动力状态) */

#define BOARD_LED_ON                       GPIO_PIN_SET
#define BOARD_LED_OFF                      GPIO_PIN_RESET

/* ==================== 新增：隔离 DCDC 使能控制 ==================== */
#define DCDC_EN_12V_GPIO_Port              GPIOA
#define DCDC_EN_12V_Pin                    GPIO_PIN_7   /* PA7: 12V 使能 */
#define DCDC_EN_24V_GPIO_Port              GPIOC
#define DCDC_EN_24V_Pin                    GPIO_PIN_4   /* PC4: 24V 使能 */

/* ==================== 新增：三色状态指示灯 (红/绿/黄) 与蜂鸣器控制 ====================
 * 硬件说明：实际板载/外部指示灯为红、绿、黄 (R/G/Y) 独立三色，无法混色 (无白光)；
 * 原理图 Page 9 误将黄灯标为 RGB_B，实际物理引脚 PC14 驱动的是黄色 LED。
 */
#define RGB_LED_R_GPIO_Port                GPIOC
#define RGB_LED_R_Pin                      GPIO_PIN_15  /* PC15: 红灯 */
#define RGB_LED_G_GPIO_Port                GPIOC
#define RGB_LED_G_Pin                      GPIO_PIN_2   /* PC2: 绿灯 */
#define RGB_LED_Y_GPIO_Port                GPIOC
#define RGB_LED_Y_Pin                      GPIO_PIN_14  /* PC14: 黄灯 (原理图误标为RGB_B) */
#define RGB_LED_B_GPIO_Port                RGB_LED_Y_GPIO_Port /* 兼容旧宏别名 */
#define RGB_LED_B_Pin                      RGB_LED_Y_Pin
#define BUZZER_ALARM_GPIO_Port             GPIOC
#define BUZZER_ALARM_Pin                   GPIO_PIN_13  /* PC13: 蜂鸣器 */

/* 默认按高电平打开 MOS；如果硬件是低电平有效，需要改这里 */
#define POWER_SWITCH_ON                    GPIO_PIN_SET
#define POWER_SWITCH_OFF                   GPIO_PIN_RESET
/* DCDC 电源使能控制：硬件为低电平使能 (Active-Low) */
#define POWER_DCDC_ON                      GPIO_PIN_RESET
#define POWER_DCDC_OFF                     GPIO_PIN_SET

/* 预放电等待时间，单位 ms */
#define POWER_PRE_DISCHARGE_DELAY_MS       400U
/* 主放电 MOS 打开后预充 MOS 延时关闭时间（重叠导通缓冲），单位 ms */
#define POWER_PRE_DISCHARGE_OVERLAP_MS     200U
#define POWER_PRE_DISCHARGE_DELAY          POWER_PRE_DISCHARGE_OVERLAP_MS
/* CAN 超过该时间未收到状态帧时，认为电池通信掉线 */
#define POWER_BATTERY_CAN_TIMEOUT_MS       2000U
/* 上电开机宽限期 (ms)：上电初始 4000ms 内等待 BMS 首包到达，避免刚上电瞬态误报警 */
#define POWER_BATTERY_BOOT_GRACE_PERIOD_MS 4000U

/* VBUS 泄放动作阈值：放电模式下超过 85V 通过 TIM8 互补 PWM (50%) 开启交替斩波泄放；回落至 84V 关闭 (1V滞回防抖) */
#define POWER_VBUS_RELEASE_OPEN_VOLTAGE    999.0f
#define POWER_VBUS_RELEASE_CLOSE_VOLTAGE   999.0f
#define POWER_VBUS_RELEASE_PWM_DUTY        0U   /* TIM8_CH2/CH2N 互补 PWM 占空比 50% (ARR=99, Pulse=50) */

/* 电池异常报警状态定义 */
#define BATTERY_ALARM_STATUS_NORMAL                  0x00U /* 正常在线 */
#define BATTERY_ALARM_STATUS_CAN_COMM_LOST           0x01U /* CAN 通信掉线；已处于放电态时保持本地路径 */
#define BATTERY_ALARM_STATUS_REMOVED                 0x02U /* 电池物理拔出 / 无电压 */
#define BATTERY_ALARM_STATUS_BMS_PROHIBIT_DISCHARGE  0x03U /* BMS 明确禁止放电，对应本地放电 MOS 和回充 MOS 已关闭 */
#define BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN       0x04U /* BMS 放电 MOS 状态未知 / 无效；放电态维持本地路径供电防空中断电，非放电态禁止准入 */

/* 开机自检完成就绪蜂鸣时长 (ms)：硬件为间歇脉冲型蜂鸣器，90ms 刚好触发单次短“滴”并避免触发第二声 */
#define POWER_BOOT_BEEP_DURATION_MS                  90U

/* ==================== 小脑 RGB 指示灯动态模式时序参数定义 (ms) ====================
 * RGB 指示灯完全由小脑通过 CAN2 功能码控制，单片机按如下时序驱动：
 * P0 (0x01): 红 爆闪 6Hz (周期≈167ms, 亮83ms/灭84ms)
 * P1 (0x02): 红 快闪 3Hz (周期≈333ms, 亮167ms/灭166ms)
 * P3 (0x03): 黄 双连闪 0.5Hz (周期2000ms, 亮150ms/灭150ms/亮150ms/灭1550ms)
 * P4 (0x04): 黄 标准闪 1Hz (周期1000ms, 亮500ms/灭500ms)
 * P5 (0x05): 绿 短促闪 3Hz (周期≈333ms, 亮80ms/灭253ms)
 * P6 (0x06): RGB 跑马灯 2Hz (周期500ms, 红167ms -> 绿167ms -> 蓝166ms)
 */
#define RGB_FLASH_P0_PERIOD_MS                       167U
#define RGB_FLASH_P0_ON_MS                           83U
#define RGB_FLASH_P1_PERIOD_MS                       333U
#define RGB_FLASH_P1_ON_MS                           167U
#define RGB_FLASH_P3_PERIOD_MS                       2000U
#define RGB_FLASH_P3_PULSE_MS                        150U
#define RGB_FLASH_P4_PERIOD_MS                       1000U
#define RGB_FLASH_P4_ON_MS                           500U
#define RGB_FLASH_P5_PERIOD_MS                       333U
#define RGB_FLASH_P5_ON_MS                           80U
#define RGB_FLASH_P6_PERIOD_MS                       500U
#define RGB_FLASH_P6_STEP1_MS                        167U
#define RGB_FLASH_P6_STEP2_MS                        334U

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
uint8_t Power_GetBattery1AlarmStatus(void);
uint8_t Power_GetBattery2AlarmStatus(void);
uint8_t Power_IsLocalFaultAlarmActive(void);

/* 辅助外设控制 API */
void Power_SetDcdc12V(uint8_t enable);
void Power_SetDcdc24V(uint8_t enable);
void Power_SetRgyLed(uint8_t r, uint8_t g, uint8_t y);
void Power_SetRgbLed(uint8_t r, uint8_t g, uint8_t b);
void Power_SetBuzzer(uint8_t on);
void Power_UpdateStatusIndicators(void);
void Power_UpdateBoardLeds(void);

uint8_t Power_GetPhysicalR(void);
uint8_t Power_GetPhysicalG(void);
uint8_t Power_GetPhysicalB(void);
uint8_t Power_GetPhysicalY(void);
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

