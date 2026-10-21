/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */
#include "adc.h"
#include "fdcan.h"
#include "tim.h"

typedef enum
{
  POWER_BATTERY_STATE_OFF = 0,
  POWER_BATTERY_STATE_PRE_DISCHARGE,
  POWER_BATTERY_STATE_DISCHARGE
} PowerBatteryState_t;

typedef enum
{
  POWER_WORK_MODE_DISCHARGE = 0,
  POWER_WORK_MODE_CHARGE
} PowerWorkMode_t;

typedef struct
{
  PowerBatteryState_t state;
} PowerBatteryControl_t;

static PowerBatteryControl_t battery1Control = {POWER_BATTERY_STATE_OFF};
static PowerBatteryControl_t battery2Control = {POWER_BATTERY_STATE_OFF};
static uint8_t dischargeModeEnabled = 0U;
static PowerWorkMode_t powerWorkMode = POWER_WORK_MODE_DISCHARGE;
static uint8_t powerPreDischargeBatteryIndex = 0U;
static uint8_t backEmfAbsorbReleased = 0U;
static uint32_t battery1PreDischargeStartTick = 0U;
static uint8_t battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
static uint8_t battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
static uint8_t powerBootBeepTriggered = 0U;
static uint32_t powerBootBeepStartTick = 0U;
static uint32_t vbusLowVoltageStartTick = 0U;
static uint8_t battery1AbnormalDisconnected = 0U;
static uint8_t vbusLowVoltageDetected = 0U;

volatile uint8_t bat_charge_mos_state = 0U;
volatile uint8_t bat1_charge_mos_state = 0U;
volatile uint8_t bat1_discharge_mos_state = 0U;
volatile uint8_t bat1_recharge_mos_state = 0U;
volatile uint8_t bat1_pre_discharge_mos_state = 0U;
volatile uint8_t bat2_charge_mos_state = 0U;
volatile uint8_t bat2_discharge_mos_state = 0U;
volatile uint8_t bat2_recharge_mos_state = 0U;
volatile uint8_t bat2_pre_discharge_mos_state = 0U;
volatile uint8_t peripheral_power_state = 0U;
volatile uint8_t back_emf_absorb_state = 0U;
volatile uint8_t back_emf_absorb_1_state = 0U;
volatile uint8_t back_emf_absorb_2_state = 0U;
volatile uint8_t dcdc_12v_state = 0U;
volatile uint8_t dcdc_24v_state = 0U;

static uint8_t Power_IsBatteryCanAlive(uint8_t batteryIndex);
static uint8_t Power_IsBatteryBmsDischargeAllowed(uint8_t batteryIndex);
static uint8_t Power_IsBatteryBmsDischargeProhibited(uint8_t batteryIndex);
static uint8_t Power_IsBatteryBmsDischargeUnknown(uint8_t batteryIndex);
static uint8_t Power_IsBatteryBmsMosFresh(uint8_t batteryIndex);
static void Power_StartBattery1PreDischarge(void);
static void Power_FinishBattery1PreDischarge(void);
static void Power_EnableBattery1DischargePath(void);
static void Power_TriggerAbnormalDisconnect(void);

static uint8_t g_isHotBoot = 0U;
static uint8_t g_hotBootBatMask = 0U;
/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{
  if (Power_IsHotBoot() != 0U)
  {
    Power_HotBootGpioInit(Power_GetHotBootBatMask());
    return;
  }

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_2
                          |GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED2_Pin|LED3_Pin|GPIO_PIN_10|GPIO_PIN_6
                          |GPIO_PIN_7|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC13 PC14 PC15 PC2
                           PC4 PC10 PC11 PC12 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_2
                          |GPIO_PIN_4|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA5 PA7 */
  GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LED2_Pin LED3_Pin PB10 PB6
                           PB7 PB9 */
  GPIO_InitStruct.Pin = LED2_Pin|LED3_Pin|GPIO_PIN_10|GPIO_PIN_6
                          |GPIO_PIN_7|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */
void Power_UpdateGpioDebugStates(void)
{
  bat_charge_mos_state = (HAL_GPIO_ReadPin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_charge_mos_state = bat_charge_mos_state;
  bat1_discharge_mos_state = (HAL_GPIO_ReadPin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_recharge_mos_state = (HAL_GPIO_ReadPin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_pre_discharge_mos_state = (HAL_GPIO_ReadPin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  bat2_charge_mos_state = 0U;
  bat2_discharge_mos_state = 0U;
  bat2_recharge_mos_state = 0U;
  bat2_pre_discharge_mos_state = 0U;

  peripheral_power_state = (HAL_GPIO_ReadPin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  dcdc_12v_state = (HAL_GPIO_ReadPin(DCDC_EN_12V_GPIO_Port, DCDC_EN_12V_Pin) == POWER_DCDC_ON) ? 1U : 0U;
  dcdc_24v_state = (HAL_GPIO_ReadPin(DCDC_EN_24V_GPIO_Port, DCDC_EN_24V_Pin) == POWER_DCDC_ON) ? 1U : 0U;
  back_emf_absorb_2_state = (((htim8.Instance->BDTR & TIM_BDTR_MOE) != 0U) &&
                             (__HAL_TIM_GET_COMPARE(&htim8, TIM_CHANNEL_2) > 0U)) ? 1U : 0U;
  back_emf_absorb_1_state = back_emf_absorb_2_state;
  back_emf_absorb_state = back_emf_absorb_2_state;
}

void Power_AllMosOff(void)
{
  HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);

  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);

  /* 反电势吸收泄放关闭：禁用 TIM8 主输出 (MOE) 并将占空比置 0，两路输出硬件拉低至 GND */
  __HAL_TIM_MOE_DISABLE(&htim8);
  __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
  back_emf_absorb_1_state = 0U;
  back_emf_absorb_2_state = 0U;
  back_emf_absorb_state = 0U;
  backEmfAbsorbReleased = 0U;

  powerPreDischargeBatteryIndex = 0U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  vbusLowVoltageStartTick = 0U;
  battery1AbnormalDisconnected = 0U;
  vbusLowVoltageDetected = 0U;
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);
  Power_SetDcdc12V(0U);
  Power_SetDcdc24V(0U);
}

HAL_StatusTypeDef Power_EnterDischargeMode(void)
{
  dischargeModeEnabled = 1U;
  powerWorkMode = POWER_WORK_MODE_DISCHARGE;
  battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  battery2AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  vbusLowVoltageStartTick = 0U;
  battery1AbnormalDisconnected = 0U;
  vbusLowVoltageDetected = 0U;

  /* 单电池版本：电池 2 硬件未接入，彻底关闭电池 2 所有 MOS */
  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  battery2Control.state = POWER_BATTERY_STATE_OFF;

  /* 立即开通外设供电，确保小脑/主控及辅助设备上电 */
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);

  if (g_isHotBoot != 0U)
  {
    /* 热接力模式：跳过关断与 400ms 预充，直接接管当前已导通的放电状态 */
    if ((g_hotBootBatMask & 0x01U) != 0U)
    {
      battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
      HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
      HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    }
    else
    {
      battery1Control.state = POWER_BATTERY_STATE_OFF;
    }

    Power_SetDcdc12V(1U);
    Power_SetDcdc24V(1U);
    g_isHotBoot = 0U; /* 接力完成，恢复常规状态机管理 */
  }
  else
  {
    battery1Control.state = POWER_BATTERY_STATE_OFF;
  }

  Power_ModeTask();
  return HAL_OK;
}

void Power_DischargeModeTask(void)
{
  uint8_t bat1CanAlive;
  uint8_t bat1BmsAllowed;
  uint8_t bat1BmsProhibited;
  uint8_t bat1BmsUnknown;

  if (dischargeModeEnabled == 0U)
  {
    return;
  }

  bat1CanAlive = Power_IsBatteryCanAlive(1U);
  bat1BmsAllowed = Power_IsBatteryBmsDischargeAllowed(1U);
  bat1BmsProhibited = Power_IsBatteryBmsDischargeProhibited(1U);
  bat1BmsUnknown = Power_IsBatteryBmsDischargeUnknown(1U);

  /*
   * 单电池版本遥测报警状态判定（仅用于上报小脑，绝不联动切断本地动力供电）：
   * 0. 处于母线异常断开状态：标记为电池物理拔出/无电压；
   * 1. 最近的 MOS 帧明确禁放：标记为 BMS 禁放；
   * 2. 最近的 MOS 帧状态未知：标记为 BMS 状态未知；
   * 3. 必要 CAN 状态流超时：标记为通信掉线；
   * 4. 正常允许放电或就绪：标记为正常。
   */
  if (battery1AbnormalDisconnected != 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  }
  else if (bat1BmsProhibited != 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_BMS_PROHIBIT_DISCHARGE;
  }
  else if (bat1BmsUnknown != 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN;
  }
  else if (bat1CanAlive == 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_CAN_COMM_LOST;
  }
  else if (bat1BmsAllowed != 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  }
  else
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  }

  /* 单电池版本：电池 2 硬件未接入，标为未连接/拔出 (0x02)，准确告知上位机单电池物理配置 */
  battery2AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  battery2Control.state = POWER_BATTERY_STATE_OFF;

  /*
   * 单电池版本放电状态机控制策略：
   * 1. 上电冷启动执行 400ms 预放电软启动防浪涌，完成后闭合主放电 MOS、回充 MOS 并开通 12V/24V DCDC；
   * 2. 进入放电态 (DISCHARGE) 后，实时监测母线电压 VBUS；若持续 20ms 低于 20V，判定为电池异常断开/关断，
   *    立即切断主放电/回充/预充 MOS 以及 DCDC 和外设供电，回退至 OFF 状态并置位异常断开标志；
   * 3. 在 OFF 状态下，若经历过异常断开，必须严格等待 BMS 通信恢复且允许放电，才重新启动 400ms 预放电，
   *    彻底消除快速断开又合闸瞬间无预充导致的硬短路冲击与打火。
   */
  if (battery1Control.state == POWER_BATTERY_STATE_OFF)
  {
    if (battery1AbnormalDisconnected != 0U)
    {
      /* 经历过异常断开：严格等待 BMS 通信就绪且允许放电才重新启动预放电软启动 */
      if ((bat1CanAlive != 0U) && (bat1BmsAllowed != 0U))
      {
        battery1AbnormalDisconnected = 0U;
        Power_StartBattery1PreDischarge();
      }
    }
    else
    {
      /* 冷启动模式：在宽限期内直接启动软启动预放电 */
      Power_StartBattery1PreDischarge();
    }
  }
  else if (battery1Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
  {
    if ((HAL_GetTick() - battery1PreDischargeStartTick) >= POWER_PRE_DISCHARGE_DELAY_MS)
    {
      Power_EnableBattery1DischargePath();
    }
  }
  else /* POWER_BATTERY_STATE_DISCHARGE */
  {
    float currentVbus = ADC_GetVbusVoltage();
    uint32_t now = HAL_GetTick();

    /* 监测母线欠压（VBUS < 30V 持续 10ms），判定为电池异常断开/关断 */
    if (currentVbus < POWER_VBUS_UNDER_VOLTAGE_THRESHOLD)
    {
      if (vbusLowVoltageStartTick == 0U)
      {
        vbusLowVoltageStartTick = now;
        vbusLowVoltageDetected = 1U; /* 标记检测到欠压 */
      }
      else if ((now - vbusLowVoltageStartTick) >= POWER_VBUS_DISCONNECT_DEBOUNCE_MS)
      {
        /* 欠压持续达到消抖时间：立即执行异常断开关断保护并回退至 OFF 状态 */
        Power_TriggerAbnormalDisconnect();
        return;
      }
    }
    else if (vbusLowVoltageDetected != 0U)
    {
      /* 曾经检测到欠压但在消抖期内 VBUS 恢复：说明电池接触不良合闸，立即强制切断并重新预充防硬短路 */
      Power_TriggerAbnormalDisconnect();
      return;
    }
    else
    {
      vbusLowVoltageStartTick = 0U;
    }

    /* 处于放电状态时，持续确保动力放电 MOS、回充 MOS 和外设供电导通 */
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
    Power_SetDcdc12V(1U);
    Power_SetDcdc24V(1U);

    /* 开启主放电 MOS 后延迟 200ms 关闭预充 MOS，实现平滑重叠导通 */
    if (powerPreDischargeBatteryIndex == 1U)
    {
      if ((HAL_GetTick() - battery1PreDischargeStartTick) >= (POWER_PRE_DISCHARGE_DELAY_MS + POWER_PRE_DISCHARGE_OVERLAP_MS))
      {
        Power_FinishBattery1PreDischarge();
      }
    }
  }

  /* 电池 2 恒为关闭状态 */
  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
}

void Power_ExitDischargeMode(void)
{
  dischargeModeEnabled = 0U;
  powerWorkMode = POWER_WORK_MODE_DISCHARGE;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  battery2AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  vbusLowVoltageStartTick = 0U;
  battery1AbnormalDisconnected = 0U;
  vbusLowVoltageDetected = 0U;
  Power_AllMosOff();
}

void Power_UpdateVbusPowerRelease(void)
{
  float currentVbus = ADC_GetVbusVoltage();
  uint8_t prevAbsorbState = back_emf_absorb_state;

  /*
   * 反电势吸收保护控制规则 (TIM8 互补通道斩波泄放)：
   * 1. 仅当系统处于放电工作模式（电池 1 处于主放电态 POWER_BATTERY_STATE_DISCHARGE）时才允许泄放；
   * 2. 当 VBUS 电压超过 85.0V (POWER_VBUS_RELEASE_OPEN_VOLTAGE) 时，
   *    通过 TIM8_CH2/CH2N (PC7/PB0) 输出 50% 占空比互补 PWM 进行交替控流泄放；
   * 3. 当 VBUS 电压回落至 84.0V (POWER_VBUS_RELEASE_CLOSE_VOLTAGE，1V 滞回防抖)
   *    或退出放电模式时，将占空比置 0 关断泄放。
   */
#if POWER_TEST_BENCH_SUPPLY_MODE
  /* 稳压电源测试模式：直接使能泄放判定，不受电池放电状态机限制 */
  uint8_t isDischarging = 1U;
#else
  uint8_t isDischarging = (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U;
#endif

  if (isDischarging != 0U)
  {
    if (currentVbus >= POWER_VBUS_RELEASE_OPEN_VOLTAGE)
    {
      __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, POWER_VBUS_RELEASE_PWM_DUTY);
      __HAL_TIM_MOE_ENABLE(&htim8);
      back_emf_absorb_2_state = 1U;
    }
    else if (currentVbus <= POWER_VBUS_RELEASE_CLOSE_VOLTAGE)
    {
      __HAL_TIM_MOE_DISABLE(&htim8);
      __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
      back_emf_absorb_2_state = 0U;
    }
    /* 处于 84.0V ~ 85.0V 滞回区间时保持当前 PWM 输出状态 */
  }
  else
  {
    __HAL_TIM_MOE_DISABLE(&htim8);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
    back_emf_absorb_2_state = 0U;
  }

  back_emf_absorb_1_state = back_emf_absorb_2_state;
  back_emf_absorb_state = back_emf_absorb_2_state;

  /* 泄放状态跳变时立即同步更新声光报警指示 */
  if (prevAbsorbState != back_emf_absorb_state)
  {
    Power_UpdateStatusIndicators();
  }
}

void Power_ModeTask(void)
{
  if (dischargeModeEnabled == 0U)
  {
    return;
  }

  Power_DischargeModeTask();
  Power_UpdateVbusPowerRelease();
}

HAL_StatusTypeDef Power_EnterChargeMode(void)
{
  /* 单电池版本暂不需要充电桩与充电模式，直接旁路 */
  return HAL_OK;
}

void Power_ExitChargeMode(void)
{
  /* 单电池版本暂不需要充电桩与充电模式，直接旁路 */
}

uint8_t Power_GetBattery1AlarmStatus(void)
{
  return battery1AlarmStatus;
}

uint8_t Power_GetBattery2AlarmStatus(void)
{
  return battery2AlarmStatus;
}

uint8_t Power_IsLocalFaultAlarmActive(void)
{
  return ((FDCAN_IsAnyBusOff() != 0U) ||
          (battery1AlarmStatus != BATTERY_ALARM_STATUS_NORMAL)) ? 1U : 0U;
}

static uint8_t Power_IsBatteryCanAlive(uint8_t batteryIndex)
{
#if POWER_TEST_BENCH_SUPPLY_MODE
  if ((POWER_TEST_SUPPLY_INPUT_CHANNEL != 0U) && (batteryIndex == POWER_TEST_SUPPLY_INPUT_CHANNEL))
  {
    return 1U;
  }
#endif
  uint32_t now = HAL_GetTick();

  if (batteryIndex == 1U)
  {
    if ((battery1_can_rx_count == 0U) || (battery1_can_mos_rx_count == 0U))
    {
      /* 上电宽限期内未收到首包时，视为正常等待首包到达，不误判掉线 */
      if (now < POWER_BATTERY_BOOT_GRACE_PERIOD_MS)
      {
        return 1U;
      }
      return 0U;
    }

    return (((now - battery1_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
            ((now - battery1_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
  }

  if ((battery2_can_rx_count == 0U) || (battery2_can_mos_rx_count == 0U))
  {
    if (now < POWER_BATTERY_BOOT_GRACE_PERIOD_MS)
    {
      return 1U;
    }
    return 0U;
  }

  return (((now - battery2_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
          ((now - battery2_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
}

static uint8_t Power_IsBatteryBmsDischargeAllowed(uint8_t batteryIndex)
{
#if POWER_TEST_BENCH_SUPPLY_MODE
  if ((POWER_TEST_SUPPLY_INPUT_CHANNEL != 0U) && (batteryIndex == POWER_TEST_SUPPLY_INPUT_CHANNEL))
  {
    return 1U;
  }
#endif
  BmsDischargeMosState_t state = (batteryIndex == 1U) ? battery1_bms_discharge_state : battery2_bms_discharge_state;
  return (state == BMS_DISCHARGE_MOS_ALLOWED) ? 1U : 0U;
}

static uint8_t Power_IsBatteryBmsDischargeProhibited(uint8_t batteryIndex)
{
#if POWER_TEST_BENCH_SUPPLY_MODE
  if ((POWER_TEST_SUPPLY_INPUT_CHANNEL != 0U) && (batteryIndex == POWER_TEST_SUPPLY_INPUT_CHANNEL))
  {
    return 0U;
  }
#endif
  BmsDischargeMosState_t state = (batteryIndex == 1U) ? battery1_bms_discharge_state : battery2_bms_discharge_state;
  return ((Power_IsBatteryBmsMosFresh(batteryIndex) != 0U) &&
          (state == BMS_DISCHARGE_MOS_PROHIBITED)) ? 1U : 0U;
}

static uint8_t Power_IsBatteryBmsDischargeUnknown(uint8_t batteryIndex)
{
#if POWER_TEST_BENCH_SUPPLY_MODE
  if ((POWER_TEST_SUPPLY_INPUT_CHANNEL != 0U) && (batteryIndex == POWER_TEST_SUPPLY_INPUT_CHANNEL))
  {
    return 0U;
  }
#endif
  BmsDischargeMosState_t state = (batteryIndex == 1U) ? battery1_bms_discharge_state : battery2_bms_discharge_state;
  return ((Power_IsBatteryBmsMosFresh(batteryIndex) != 0U) &&
          (state == BMS_DISCHARGE_MOS_UNKNOWN)) ? 1U : 0U;
}

static uint8_t Power_IsBatteryBmsMosFresh(uint8_t batteryIndex)
{
  uint32_t now = HAL_GetTick();

  if (batteryIndex == 1U)
  {
    return ((battery1_can_mos_rx_count != 0U) &&
            ((now - battery1_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
  }

  return ((battery2_can_mos_rx_count != 0U) &&
          ((now - battery2_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
}



static void Power_TriggerAbnormalDisconnect(void)
{
  /* 确认异常断开：切断主放电、回充、预充 MOS，并关断 12V/24V DCDC 与外设供电 */
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);
  Power_SetDcdc12V(0U);
  Power_SetDcdc24V(0U);

  /* 关闭反电势泄放 */
  __HAL_TIM_MOE_DISABLE(&htim8);
  __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
  back_emf_absorb_1_state = 0U;
  back_emf_absorb_2_state = 0U;
  back_emf_absorb_state = 0U;
  backEmfAbsorbReleased = 0U;

  powerPreDischargeBatteryIndex = 0U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery1AbnormalDisconnected = 1U;
  battery1AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  vbusLowVoltageStartTick = 0U;
  vbusLowVoltageDetected = 0U;
}

static void Power_StartBattery1PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

  battery1PreDischargeStartTick = HAL_GetTick();
  battery1Control.state = POWER_BATTERY_STATE_PRE_DISCHARGE;
  powerPreDischargeBatteryIndex = 1U;
}

static void Power_FinishBattery1PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  powerPreDischargeBatteryIndex = 0U;
}

static void Power_EnableBattery1DischargePath(void)
{
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  Power_SetDcdc12V(1U);
  Power_SetDcdc24V(1U);
  Power_TriggerBootBeep();
  battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

void Power_SetDcdc12V(uint8_t enable)
{
  HAL_GPIO_WritePin(DCDC_EN_12V_GPIO_Port, DCDC_EN_12V_Pin, (enable != 0U) ? POWER_DCDC_ON : POWER_DCDC_OFF);
}

void Power_SetDcdc24V(uint8_t enable)
{
  HAL_GPIO_WritePin(DCDC_EN_24V_GPIO_Port, DCDC_EN_24V_Pin, (enable != 0U) ? POWER_DCDC_ON : POWER_DCDC_OFF);
}

void Power_SetRgyLed(uint8_t r, uint8_t g, uint8_t y)
{
  HAL_GPIO_WritePin(RGB_LED_R_GPIO_Port, RGB_LED_R_Pin, (r != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RGB_LED_G_GPIO_Port, RGB_LED_G_Pin, (g != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RGB_LED_Y_GPIO_Port, RGB_LED_Y_Pin, (y != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Power_SetRgbLed(uint8_t r, uint8_t g, uint8_t b)
{
  /* 兼容旧 API：第3路参数在实际物理硬件上驱动的是黄灯 (PC14) */
  Power_SetRgyLed(r, g, b);
}

void Power_SetBuzzer(uint8_t on)
{
  HAL_GPIO_WritePin(BUZZER_ALARM_GPIO_Port, BUZZER_ALARM_Pin, (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Power_TriggerBootBeep(void)
{
  if (powerBootBeepTriggered == 0U)
  {
    powerBootBeepTriggered = 1U;
    powerBootBeepStartTick = HAL_GetTick();
  }
}

static uint8_t physicalR = 0U;
static uint8_t physicalG = 0U;
static uint8_t physicalY = 0U;
static uint8_t physicalBuzzer = 0U;

uint8_t Power_GetPhysicalR(void) { return physicalR; }
uint8_t Power_GetPhysicalG(void) { return physicalG; }
uint8_t Power_GetPhysicalY(void) { return physicalY; }
uint8_t Power_GetPhysicalB(void) { return physicalY; } /* 兼容旧接口命名 */
uint8_t Power_GetPhysicalBuzzer(void) { return physicalBuzzer; }

void Power_UpdateStatusIndicators(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t outR = 0U;
  uint8_t outG = 0U;
  uint8_t outY = 0U;
  uint8_t outBuzzer = 0U;

  /* ==================== 1. 指示灯控制决策 ====================
   * 指示灯完全由小脑下发的功能码控制；若通信超时或功能码为 0 则全灭 (OFF)。
   * 硬件特性：仅有红(PC15)、绿(PC2)、黄(PC14)三色独立通道，无法混色 (无白光)。
   * 单片机本地安全告警由板载 LED2/LED3 与蜂鸣器处理，不占用外部三色灯。
   */
  uint8_t funcCode = FDCAN_GetRgbFunctionCode();

  switch (funcCode)
  {
    case FDCAN_RGB_FUNC_P0_RED_BURST_6HZ:
      /* P0: 急停 / 碰撞 / BMS报警 (红 爆闪 6Hz, 周期 167ms, 亮83ms / 灭84ms) */
      if ((now % RGB_FLASH_P0_PERIOD_MS) < RGB_FLASH_P0_ON_MS)
      {
        outR = 1U;
      }
      break;

    case FDCAN_RGB_FUNC_P1_RED_FAST_3HZ:
      /* P1: 严重故障 (红 快闪 3Hz, 周期 333ms, 亮167ms / 灭166ms) */
      if ((now % RGB_FLASH_P1_PERIOD_MS) < RGB_FLASH_P1_ON_MS)
      {
        outR = 1U;
      }
      break;

    case FDCAN_RGB_FUNC_P3_YELLOW_DOUBLE_0_5HZ:
      /* P3: 倾倒、跌落 (黄 双连闪 0.5Hz, 周期 2000ms: 0~150ms亮, 150~300ms灭, 300~450ms亮, 450~2000ms灭) */
      {
        uint32_t phase = now % RGB_FLASH_P3_PERIOD_MS;
        if ((phase < RGB_FLASH_P3_PULSE_MS) ||
            ((phase >= (RGB_FLASH_P3_PULSE_MS * 2U)) && (phase < (RGB_FLASH_P3_PULSE_MS * 3U))))
        {
          outY = 1U; /* 独立驱动黄灯 PC14 */
        }
      }
      break;

    case FDCAN_RGB_FUNC_P4_YELLOW_STD_1HZ:
      /* P4: 一般警告 (黄 标准闪 1Hz, 周期 1000ms, 亮500ms / 灭500ms) */
      if ((now % RGB_FLASH_P4_PERIOD_MS) < RGB_FLASH_P4_ON_MS)
      {
        outY = 1U; /* 独立驱动黄灯 PC14 */
      }
      break;

    case FDCAN_RGB_FUNC_P5_GREEN_PULSE_3HZ:
      /* P5: 语音监听 / 指令确认 (绿 短促闪 3Hz, 周期 333ms, 短亮80ms / 灭253ms) */
      if ((now % RGB_FLASH_P5_PERIOD_MS) < RGB_FLASH_P5_ON_MS)
      {
        outG = 1U;
      }
      break;

    case FDCAN_RGB_FUNC_P6_MARQUEE_2HZ:
      /* P6: 自主巡检 / 建图 (红黄绿三色跑马灯 2Hz, 周期 500ms: 红 167ms -> 黄 167ms -> 绿 166ms) */
      {
        uint32_t phase = now % RGB_FLASH_P6_PERIOD_MS;
        if (phase < RGB_FLASH_P6_STEP1_MS)
        {
          outR = 1U;
        }
        else if (phase < RGB_FLASH_P6_STEP2_MS)
        {
          outY = 1U; /* 独立驱动黄灯 PC14 */
        }
        else
        {
          outG = 1U;
        }
      }
      break;

    case FDCAN_RGB_FUNC_SOLID_RED:
      /* 红灯常亮 */
      outR = 1U;
      break;

    case FDCAN_RGB_FUNC_SOLID_GREEN:
      /* 绿灯常亮 */
      outG = 1U;
      break;

    case FDCAN_RGB_FUNC_SOLID_YELLOW:
    case FDCAN_RGB_FUNC_SOLID_YELLOW_ALIAS:
      /* 黄灯常亮 (独立 PC14) */
      outY = 1U;
      break;

    case FDCAN_RGB_FUNC_OFF:
    default:
      /* 关灯或未识别功能码：全灭 */
      outR = 0U;
      outG = 0U;
      outY = 0U;
      break;
  }

  /* ==================== 2. 蜂鸣器控制决策 ====================
   * 1. 绝对最高硬件安全优先级：反电势吸收泄放激活 (VBUS >= 85V)，强制蜂鸣器长鸣报警
   * 2. 本地底层安全告警：电池掉线/异常 (battery1AlarmStatus != NORMAL) 或 CAN Bus-Off，强制 Alarm 报警 (上位机无法覆盖静音)
   * 3. 小脑控制模式 (若未被调试屏蔽且指令有效)
   * 4. 本地开机提示音模式
   */
  if (back_emf_absorb_state != 0U)
  {
    outBuzzer = 1U;
  }
  else if (Power_IsLocalFaultAlarmActive() != 0U)
  {
    /* 本地底层安全告警：电池掉线/异常或 CAN Bus-Off，强制启动 Alarm 报警器，小脑下发关指令无法覆盖 */
    outBuzzer = 1U;
  }
  else if (FDCAN_IsRkBuzzerActive() != 0U)
  {
    /* 小脑接管蜂鸣器：硬件具备自激间隔鸣叫特性，MCU 仅需输出 0(关) 或 1(开) */
    outBuzzer = (FDCAN_GetRkAlarmState() != 0U) ? 1U : 0U;
  }
  else
  {
    /* 本地模式：若有开机就绪提示音则响，否则静音 */
    if ((powerBootBeepTriggered != 0U) &&
        ((now - powerBootBeepStartTick) < POWER_BOOT_BEEP_DURATION_MS))
    {
      outBuzzer = 1U;
    }
    else
    {
      outBuzzer = 0U;
    }
  }

  /* 3. 驱动硬件输出并更新记录 */
  Power_SetRgyLed(outR, outG, outY);
  Power_SetBuzzer(outBuzzer);

  /* 4. 更新板载 3.3V 状态指示灯 (LED2 心跳 / LED3 动力状态) */
  Power_UpdateBoardLeds();

  physicalR = outR;
  physicalG = outG;
  physicalY = outY;
  physicalBuzzer = outBuzzer;
}

void Power_UpdateBoardLeds(void)
{
  uint32_t now = HAL_GetTick();
  GPIO_PinState led2State = BOARD_LED_OFF;
  GPIO_PinState led3State = BOARD_LED_OFF;

  /* --- LED2: 板载系统运行心跳灯 (PB1) --- */
  /* 单电池版本：仅当存在 FDCAN1/2 Bus-Off 或电池 1 发生通信异常/故障时，5Hz 快速闪烁告警；
   * 正常运行时以 1Hz 慢闪 (500ms 翻转) */
  if (Power_IsLocalFaultAlarmActive() != 0U)
  {
    led2State = (((now / 100U) % 2U) == 0U) ? BOARD_LED_ON : BOARD_LED_OFF;
  }
  else
  {
    led2State = (((now / 500U) % 2U) == 0U) ? BOARD_LED_ON : BOARD_LED_OFF;
  }

  /* --- LED3: 板载动力电源状态指示 (PB2) --- */
  if (back_emf_absorb_state != 0U)
  {
    /* 泄放动作激活：高频爆闪 (50ms 翻转) 提示大功率泄放中 */
    led3State = (((now / 50U) % 2U) == 0U) ? BOARD_LED_ON : BOARD_LED_OFF;
  }
  else if (battery1Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
  {
    /* 上电预充中：2Hz 闪烁 (250ms 翻转) 提示正在预充缓启动 */
    led3State = (((now / 250U) % 2U) == 0U) ? BOARD_LED_ON : BOARD_LED_OFF;
  }
  else if ((battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) &&
           (dcdc_24v_state != 0U))
  {
    /* 动力输出就绪 (主放电 MOS 导通且 24V DCDC 已打开)：常亮 */
    led3State = BOARD_LED_ON;
  }
  else
  {
    /* 待机 / 完全未就绪：熄灭 */
    led3State = BOARD_LED_OFF;
  }

  HAL_GPIO_WritePin(BOARD_LED2_GPIO_Port, BOARD_LED2_Pin, led2State);
  HAL_GPIO_WritePin(BOARD_LED3_GPIO_Port, BOARD_LED3_Pin, led3State);
}

/* ==================== IAP 无缝热接力状态机与硬件控制 ==================== */

void Power_CheckAndHandleHotBoot(void)
{
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  uint32_t bkp1 = TAMP->BKP1R;
  if ((bkp1 & 0xFFFF0000U) == IAP_HOT_BOOT_MAGIC)
  {
    g_isHotBoot = 1U;
    g_hotBootBatMask = (uint8_t)(bkp1 & 0x0001U); /* 单电池仅允许接管电池 1 */
    TAMP->BKP1R = 0U; /* 读取后清零热启动标志 */
  }
  else
  {
    g_isHotBoot = 0U;
    g_hotBootBatMask = 0U;
  }
}

uint8_t Power_IsHotBoot(void)
{
  return g_isHotBoot;
}

uint8_t Power_GetHotBootBatMask(void)
{
  return g_hotBootBatMask;
}

uint8_t Power_GetActiveDischargeMask(void)
{
  uint8_t mask = 0U;
  if (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE)
  {
    mask |= 0x01U;
  }

  /* 兜底：若状态机未标明 DISCHARGE，回读物理引脚电平 */
  if (mask == 0U)
  {
    if (HAL_GPIO_ReadPin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON)
    {
      mask |= 0x01U;
    }
  }

  /* 安全兜底：如果完全未检测到，默认使能 Bat1 保障小脑供电 */
  if (mask == 0U)
  {
    mask = 0x01U;
  }
  return mask;
}

void Power_HotBootGpioInit(uint8_t batMask)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 使能 GPIO 时钟 */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* 1. 先设置输出数据寄存器 (ODR/BSRR)，确保引脚配置为输出时不出现任何电平跌落 */
  /* GPIOC: PC4 (24V DCDC, 低使能), PC2 (绿灯), PC11 (Bat2主放电, 单电池版本强制关) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, POWER_DCDC_ON); /* 保持 24V DCDC 导通 (Active Low) */
#if defined(POWER_STATUS_LED_FLOWING_TEST_ENABLE) && (POWER_STATUS_LED_FLOWING_TEST_ENABLE != 0U)
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);  /* IAP 流水灯模式：热启动初始不常亮绿灯 */
#else
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);    /* 恢复绿灯指示正常工作态 */
#endif
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, POWER_SWITCH_OFF); /* 电池 2 彻底关断 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_RESET);

  /* GPIOA: PA7 (12V DCDC, 低使能), PA5 (外设供电, 高开) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, POWER_DCDC_ON); /* 保持 12V DCDC 持续不断电 (Active Low) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, POWER_SWITCH_ON);

  /* GPIOB: PB6 (Bat1主放电, 高开), PB7 (Bat1回充, 高开), 板载 LED2/LED3 置默认电平, 其余关 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, ((batMask & 0x01U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, ((batMask & 0x01U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(GPIOB, LED2_Pin | LED3_Pin | GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_RESET);

  /* 2. 配置引脚为推挽输出模式 */
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15 | GPIO_PIN_2
                        | GPIO_PIN_4 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LED2_Pin | LED3_Pin | GPIO_PIN_10 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
/* USER CODE END 2 */
