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
static uint32_t battery2PreDischargeStartTick = 0U;
static uint8_t rechargeCurrentOnlyMode = 0U;
static uint32_t rechargeBalanceSteadyStartTick = 0U;
static uint8_t chargeCurrentOnlyMode = 0U;
static uint32_t chargeBalanceSteadyStartTick = 0U;
static uint32_t chargeSingleSwitchSteadyStartTick = 0U;
static uint8_t chargeActiveSingleBat = 0U;
static uint8_t chargeSingleSwitchedOnce = 0U;
static uint8_t chargeReplyBatteryIndex = 0U;
static uint32_t chargeReplyLastTxTick = 0U;
static uint8_t battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
static uint8_t battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
static uint8_t powerBootBeepTriggered = 0U;
static uint32_t powerBootBeepStartTick = 0U;

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
static uint8_t Power_IsBatteryCanPresent(uint8_t batteryIndex);
static float Power_GetBatteryCanVoltage(uint8_t batteryIndex);
static void Power_UpdateBatteryAlarmStatuses(void);
static float Power_GetBalancedVoltageDiff(void);
static void Power_MaintainChargeModeVbusPower(uint8_t battery1Present, uint8_t battery2Present);
static uint8_t Power_GetRechargeMaskByCanVoltage(uint8_t battery1Ready, uint8_t battery2Ready);
static void Power_SetRechargeMos(uint8_t mask);
static void Power_SetChargeMos(uint8_t mask, uint8_t battery1Present, uint8_t battery2Present);
static void Power_UpdateChargeMode(uint8_t battery1Present, uint8_t battery2Present);
static void Power_UpdatePeripheralPower(uint8_t battery1Present, uint8_t battery2Present);
static void Power_StartBattery1PreDischarge(void);
static void Power_StartBattery2PreDischarge(void);
static void Power_FinishBattery1PreDischarge(void);
static void Power_FinishBattery2PreDischarge(void);
static void Power_EnableBattery1DischargePath(void);
static void Power_EnableBattery2DischargePath(void);
static void Power_UpdateBattery1Path(uint8_t present);
static void Power_UpdateBattery2Path(uint8_t present);
static void Power_DisableBattery1Path(void);
static void Power_DisableBattery2Path(void);
static void Power_UpdateRechargeMos(uint8_t battery1Ready, uint8_t battery2Ready);

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
  __HAL_RCC_GPIOD_CLK_ENABLE();

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

  /*Configure GPIO pin : EMERGENCY_STOP_Pin */
  GPIO_InitStruct.Pin = EMERGENCY_STOP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(EMERGENCY_STOP_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */
void Power_UpdateGpioDebugStates(void)
{
  uint8_t totalChargeMosOn = (HAL_GPIO_ReadPin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  bat1_discharge_mos_state = (HAL_GPIO_ReadPin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_recharge_mos_state = (HAL_GPIO_ReadPin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_pre_discharge_mos_state = (HAL_GPIO_ReadPin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  bat2_discharge_mos_state = (HAL_GPIO_ReadPin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_recharge_mos_state = (HAL_GPIO_ReadPin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_pre_discharge_mos_state = (HAL_GPIO_ReadPin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  if (totalChargeMosOn != 0U)
  {
    bat1_charge_mos_state = ((bat1_discharge_mos_state != 0U) && (bat1_recharge_mos_state != 0U)) ? 1U : 0U;
    bat2_charge_mos_state = ((bat2_discharge_mos_state != 0U) && (bat2_recharge_mos_state != 0U)) ? 1U : 0U;
    bat_charge_mos_state = (uint8_t)(bat1_charge_mos_state | (uint8_t)(bat2_charge_mos_state << 1U));
  }
  else
  {
    bat1_charge_mos_state = 0U;
    bat2_charge_mos_state = 0U;
    bat_charge_mos_state = 0U;
  }

  peripheral_power_state = (HAL_GPIO_ReadPin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  dcdc_12v_state = (HAL_GPIO_ReadPin(DCDC_EN_12V_GPIO_Port, DCDC_EN_12V_Pin) == POWER_DCDC_ON) ? 1U : 0U;
  dcdc_24v_state = (HAL_GPIO_ReadPin(DCDC_EN_24V_GPIO_Port, DCDC_EN_24V_Pin) == POWER_DCDC_ON) ? 1U : 0U;
#if (!POWER_VBUS_RELEASE_ENABLE)
  back_emf_absorb_2_state = 0U;
  back_emf_absorb_1_state = 0U;
  back_emf_absorb_state = 0U;
#else
  back_emf_absorb_2_state = (((htim8.Instance->BDTR & TIM_BDTR_MOE) != 0U) &&
                             (__HAL_TIM_GET_COMPARE(&htim8, TIM_CHANNEL_2) > 0U)) ? 1U : 0U;
  back_emf_absorb_1_state = back_emf_absorb_2_state;
  back_emf_absorb_state = back_emf_absorb_2_state;
#endif
}

GPIO_PinState Power_ReadEmergencyStop(void)
{
  return HAL_GPIO_ReadPin(EMERGENCY_STOP_GPIO_Port, EMERGENCY_STOP_Pin);
}

uint8_t Power_IsEmergencyStopActive(void)
{
  static GPIO_PinState debouncedState = GPIO_PIN_SET;
  static GPIO_PinState lastRawState = GPIO_PIN_SET;
  static uint32_t lastChangeTick = 0U;
  static uint8_t initialized = 0U;

  GPIO_PinState rawState = Power_ReadEmergencyStop();
  uint32_t now = HAL_GetTick();

  if (initialized == 0U)
  {
    debouncedState = rawState;
    lastRawState = rawState;
    lastChangeTick = now;
    initialized = 1U;
  }
  else
  {
    if (rawState != lastRawState)
    {
      lastRawState = rawState;
      lastChangeTick = now;
    }

    if ((now - lastChangeTick) >= POWER_ESTOP_DEBOUNCE_MS)
    {
      debouncedState = lastRawState;
    }
  }

  return (debouncedState == POWER_ESTOP_ACTIVE_STATE) ? 1U : 0U;
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

  /* 反电势吸收泄放关闭：无条件禁用 TIM8 主输出 (MOE) 并将占空比置 0，两路输出硬件拉低至 GND */
  __HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim8);
  __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
  back_emf_absorb_1_state = 0U;
  back_emf_absorb_2_state = 0U;
  back_emf_absorb_state = 0U;
  backEmfAbsorbReleased = 0U;

  powerPreDischargeBatteryIndex = 0U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  rechargeCurrentOnlyMode = 0U;
  rechargeBalanceSteadyStartTick = 0U;
  chargeCurrentOnlyMode = 0U;
  chargeBalanceSteadyStartTick = 0U;
  chargeSingleSwitchSteadyStartTick = 0U;
  chargeActiveSingleBat = 0U;
  chargeSingleSwitchedOnce = 0U;
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);
  Power_SetDcdc12V(0U);
  Power_SetDcdc24V(0U);
}

HAL_StatusTypeDef Power_EnterDischargeMode(void)
{
  dischargeModeEnabled = 1U;
  powerWorkMode = POWER_WORK_MODE_DISCHARGE;
  battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  chargeActiveSingleBat = 0U;
  chargeSingleSwitchedOnce = 0U;
  chargeReplyBatteryIndex = 0U;
  chargeReplyLastTxTick = HAL_GetTick();

  if (g_isHotBoot != 0U)
  {
    /* 热接力模式：跳过关断与 400ms 预充，直接接管当前已导通的放电状态 */
    if ((g_hotBootBatMask & 0x01U) != 0U)
    {
      battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
      HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    }
    else
    {
      battery1Control.state = POWER_BATTERY_STATE_OFF;
    }

    if ((g_hotBootBatMask & 0x02U) != 0U)
    {
      battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
      HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    }
    else
    {
      battery2Control.state = POWER_BATTERY_STATE_OFF;
    }

    Power_SetDcdc12V(1U);
    Power_SetDcdc24V(1U);
    g_isHotBoot = 0U; /* 接力完成，恢复常规状态机管理 */
  }
  else
  {
    battery1Control.state = POWER_BATTERY_STATE_OFF;
    battery2Control.state = POWER_BATTERY_STATE_OFF;
  }

  Power_ModeTask();
  return HAL_OK;
}

static void Power_UpdateBatteryAlarmStatuses(void)
{
  uint8_t bat1CanAlive = Power_IsBatteryCanAlive(1U);
  uint8_t bat1BmsAllowed = Power_IsBatteryBmsDischargeAllowed(1U);
  uint8_t bat1BmsProhibited = Power_IsBatteryBmsDischargeProhibited(1U);
  uint8_t bat1BmsUnknown = Power_IsBatteryBmsDischargeUnknown(1U);

  uint8_t bat2CanAlive = Power_IsBatteryCanAlive(2U);
  uint8_t bat2BmsAllowed = Power_IsBatteryBmsDischargeAllowed(2U);
  uint8_t bat2BmsProhibited = Power_IsBatteryBmsDischargeProhibited(2U);
  uint8_t bat2BmsUnknown = Power_IsBatteryBmsDischargeUnknown(2U);

  /*
   * 电池 1 状态判定 (V1.5 极简 3 态)：
   * 1. CAN 通信掉线（超时无报文或未达到最低有效物理电压）：判定为离线/未连接 (BATTERY_ALARM_STATUS_OFFLINE, 0x01)；
   * 2. CAN 正常但 BMS 明确禁放或状态异常/未知：判定为 BMS 故障禁放 (BATTERY_ALARM_STATUS_PROHIBITED, 0x02)；
   * 3. CAN 正常且 BMS 明确允许放电：判定为正常就绪 (BATTERY_ALARM_STATUS_NORMAL, 0x00)。
   */
  if (bat1CanAlive == 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_OFFLINE;
  }
  else if ((bat1BmsProhibited != 0U) || (bat1BmsUnknown != 0U) || (bat1BmsAllowed == 0U))
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_PROHIBITED;
  }
  else
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  }

  /* 电池 2 状态判定 (同上) */
  if (bat2CanAlive == 0U)
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_OFFLINE;
  }
  else if ((bat2BmsProhibited != 0U) || (bat2BmsUnknown != 0U) || (bat2BmsAllowed == 0U))
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_PROHIBITED;
  }
  else
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  }
}

void Power_DischargeModeTask(void)
{
  uint8_t bat1BmsProhibited;
  uint8_t bat1BmsUnknown;
  uint8_t bat1PhysOnline;
  uint8_t bat1Enable;

  uint8_t bat2BmsProhibited;
  uint8_t bat2BmsUnknown;
  uint8_t bat2PhysOnline;
  uint8_t bat2Enable;

  if (dischargeModeEnabled == 0U)
  {
    return;
  }

  bat1BmsProhibited = Power_IsBatteryBmsDischargeProhibited(1U);
  bat1BmsUnknown = Power_IsBatteryBmsDischargeUnknown(1U);
  bat1PhysOnline = Power_IsBatteryPhysicallyPresent(1U);

  bat2BmsProhibited = Power_IsBatteryBmsDischargeProhibited(2U);
  bat2BmsUnknown = Power_IsBatteryBmsDischargeUnknown(2U);
  bat2PhysOnline = Power_IsBatteryPhysicallyPresent(2U);

  /*
   * 安全底线检查：若两块电池均判定为物理离线（无有效 CAN 心跳且非主放电态），主动力来源彻底丧失，
   * 立即执行全板安全关断（所有放电/回充/预充关断，反电动势吸收开启，状态机复位为 OFF）。
   */
  if ((bat1PhysOnline == 0U) && (bat2PhysOnline == 0U))
  {
    Power_AllMosOff();
    Power_UpdatePeripheralPower(bat1PhysOnline, bat2PhysOnline);
    return;
  }

  /*
   * 决策电池 1 本地放电路径使能：
   * 1. 若 BMS 明确禁放 (bat1BmsProhibited)：坚决立即禁止放电 (bat1Enable = 0)；
   * 2. 若 BMS 状态未知/无效 (bat1BmsUnknown)：若物理在位且已处于放电态，执行锁死保护保持放电不断动力 (bat1Enable = 1)，
   *    避免因偶发单帧干扰导致空中断电摔狗；若处于未就绪/待机态则严把准入 (bat1Enable = 0)；
   * 3. 若 正常就绪 (NORMAL)：在物理在位时使能放电 (bat1Enable = bat1PhysOnline)；
   * 4. 若 离线/掉线 (OFFLINE)：
   *    - 若处于单电放电且物理在位（Power_IsBatteryPhysicallyPresent返回1）：保持放电不断动力 (bat1Enable = 1) 并上报离线，供小脑受控下蹲；
   *    - 其余情况（双电掉线、待机掉线）：关断放电 (bat1Enable = 0)。
   */
  if (bat1BmsProhibited != 0U)
  {
    bat1Enable = 0U;
  }
  else if (bat1BmsUnknown != 0U)
  {
    if ((bat1PhysOnline != 0U) && (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE))
    {
      bat1Enable = 1U;
    }
    else
    {
      bat1Enable = 0U;
    }
  }
  else if (battery1AlarmStatus == BATTERY_ALARM_STATUS_NORMAL)
  {
    bat1Enable = bat1PhysOnline;
  }
  else /* BATTERY_ALARM_STATUS_OFFLINE */
  {
    if ((bat1PhysOnline != 0U) && (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE))
    {
      bat1Enable = 1U;
    }
    else
    {
      bat1Enable = 0U;
    }
  }

  /* 决策电池 2 本地放电路径使能（同上） */
  if (bat2BmsProhibited != 0U)
  {
    bat2Enable = 0U;
  }
  else if (bat2BmsUnknown != 0U)
  {
    if ((bat2PhysOnline != 0U) && (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE))
    {
      bat2Enable = 1U;
    }
    else
    {
      bat2Enable = 0U;
    }
  }
  else if (battery2AlarmStatus == BATTERY_ALARM_STATUS_NORMAL)
  {
    bat2Enable = bat2PhysOnline;
  }
  else /* BATTERY_ALARM_STATUS_OFFLINE */
  {
    if ((bat2PhysOnline != 0U) && (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE))
    {
      bat2Enable = 1U;
    }
    else
    {
      bat2Enable = 0U;
    }
  }

  Power_UpdateBattery1Path(bat1Enable);
  Power_UpdateBattery2Path(bat2Enable);

  Power_UpdateRechargeMos((battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U,
                          (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U);
  Power_UpdatePeripheralPower(bat1PhysOnline, bat2PhysOnline);
}

void Power_ExitDischargeMode(void)
{
  dischargeModeEnabled = 0U;
  powerWorkMode = POWER_WORK_MODE_DISCHARGE;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  chargeActiveSingleBat = 0U;
  chargeSingleSwitchedOnce = 0U;
  chargeReplyBatteryIndex = 0U;
  chargeReplyLastTxTick = HAL_GetTick();
  Power_AllMosOff();
}

void Power_UpdateVbusPowerRelease(void)
{
#if (!POWER_VBUS_RELEASE_ENABLE)
  uint8_t prevAbsorbState = back_emf_absorb_state;

  /* 软件完全关闭泄放功能：无条件关闭 TIM8 主输出 (MOE) 并将占空比置 0，PC7/PB0 硬件强制拉低至 GND */
  __HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim8);
  __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
  back_emf_absorb_2_state = 0U;
  back_emf_absorb_1_state = 0U;
  back_emf_absorb_state = 0U;

  /* 泄放状态跳变时立即同步更新声光报警指示 */
  if (prevAbsorbState != back_emf_absorb_state)
  {
    Power_UpdateStatusIndicators();
  }
#else
  float currentVbus = ADC_GetVbusVoltage();
  uint8_t prevAbsorbState = back_emf_absorb_state;

  /*
   * 反电势吸收保护控制规则 (PC7/PB0 TIM8_CH2/CH2N 互补 PWM 50% 斩波泄放)：
   * 1. 仅当系统处于放电工作模式（任一电池处于主放电态 POWER_BATTERY_STATE_DISCHARGE）时才允许泄放；
   * 2. 当 VBUS 电压超过 85.0V (POWER_VBUS_RELEASE_OPEN_VOLTAGE) 时，
   *    通过 TIM8 通道 2 开启互补 PWM (PC7/PB0) 进行 50% 占空比控流泄放；
   * 3. 当 VBUS 电压回落至 84.0V (POWER_VBUS_RELEASE_CLOSE_VOLTAGE，1V 滞回防抖)
   *    或退出放电模式时，关断 TIM8 PWM 泄放输出。
   */
#if POWER_TEST_BENCH_SUPPLY_MODE
  /* 稳压电源测试模式：直接使能泄放判定，不受电池放电状态机限制（兼容 BAT1 输入或 VBUS 直接注电） */
  uint8_t isDischarging = 1U;
#else
  uint8_t isDischarging = ((battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ||
                           (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE)) ? 1U : 0U;
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
      __HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim8);
      __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
      back_emf_absorb_2_state = 0U;
    }
    /* 处于 84.0V ~ 85.0V 滞回区间时保持当前 PWM 输出状态 */
  }
  else
  {
    __HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim8);
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
#endif
}

void Power_ModeTask(void)
{
  uint8_t battery1Present;
  uint8_t battery2Present;

  if (dischargeModeEnabled == 0U)
  {
    return;
  }

  /* 统一刷新双电池告警状态 (无论放电还是充电模式，均保持对上位机与板载 LED 实时上报) */
  Power_UpdateBatteryAlarmStatuses();

  if (powerWorkMode == POWER_WORK_MODE_CHARGE)
  {
    battery1Present = Power_IsBatteryCanPresent(1U);
    battery2Present = Power_IsBatteryCanPresent(2U);
    Power_UpdateChargeMode(battery1Present, battery2Present);
    Power_UpdatePeripheralPower(battery1Present, battery2Present);
    Power_UpdateVbusPowerRelease();
    Power_UpdateGpioDebugStates();
    return;
  }

  Power_DischargeModeTask();
  Power_UpdateVbusPowerRelease();
  Power_UpdateGpioDebugStates();
}

static void Power_MaintainChargeModeVbusPower(uint8_t battery1Present, uint8_t battery2Present)
{
  uint8_t bat1CanSupply = (battery1Present != 0U) && (Power_IsBatteryBmsDischargeAllowed(1U) != 0U);
  uint8_t bat2CanSupply = (battery2Present != 0U) && (Power_IsBatteryBmsDischargeAllowed(2U) != 0U);

  /*
   * 充电模式下仅维持单路在位且开启放电按钮（BMS 明确允许放电）的电池主放电 MOS 导通，为 VBUS 供电以驱动 DCDC 保障小脑（RK3588）稳定运行。
   * 严禁双路放电 MOS 同时导通，杜绝两块电池在 VBUS 放电母线上硬并联产生跨母线倒灌短路环流！
   * 维持供电的电池保持 DISCHARGE 状态（保小脑运行不断电）；
   * 未供电的电池设为 OFF，退出充电后并网放电时需经过标准预放电软启动流程。
   */
  if (bat1CanSupply && bat2CanSupply)
  {
    float v1 = Power_GetBatteryCanVoltage(1U);
    float v2 = Power_GetBatteryCanVoltage(2U);
    uint8_t selectBat1;

    /*
     * 迟滞回差防抖 (Hysteresis 0.3V)：
     * 1. 若当前 BAT1 处于放电维持供电中，仅当 BAT2 电压显著高出 BAT1 超过 0.3V 时才切换至 BAT2；
     * 2. 若当前 BAT2 处于放电维持供电中，仅当 BAT1 电压显著高出 BAT2 超过 0.3V 时才切换至 BAT1；
     * 3. 若初始进入均未放电，以电压更高者为准 (v1 >= v2 选择 BAT1)。
     */
    if (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE)
    {
      selectBat1 = ((v2 - v1) > POWER_CHARGE_VBUS_HYSTERESIS_VOLTAGE) ? 0U : 1U;
    }
    else if (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE)
    {
      selectBat1 = ((v1 - v2) > POWER_CHARGE_VBUS_HYSTERESIS_VOLTAGE) ? 1U : 0U;
    }
    else
    {
      selectBat1 = (v1 >= v2) ? 1U : 0U;
    }

    if (selectBat1 != 0U)
    {
      /* 先关断非供电侧 BAT2，再导通目标供电侧 BAT1，严格遵循先断后通 */
      HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
      battery2Control.state = POWER_BATTERY_STATE_OFF;
      HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
      battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
    }
    else
    {
      /* 先关断非供电侧 BAT1，再导通目标供电侧 BAT2，严格遵循先断后通 */
      HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
      battery1Control.state = POWER_BATTERY_STATE_OFF;
      HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
      battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
    }
  }
  else if (bat1CanSupply)
  {
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    battery2Control.state = POWER_BATTERY_STATE_OFF;
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
  }
  else if (bat2CanSupply)
  {
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    battery1Control.state = POWER_BATTERY_STATE_OFF;
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
  }
  else
  {
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    battery1Control.state = POWER_BATTERY_STATE_OFF;
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    battery2Control.state = POWER_BATTERY_STATE_OFF;
  }
}

HAL_StatusTypeDef Power_EnterChargeMode(void)
{
  uint8_t bat1Ready;
  uint8_t bat2Ready;
  uint8_t inheritParallelCharge = 0U;

  if (powerWorkMode == POWER_WORK_MODE_CHARGE)
  {
    return HAL_OK;
  }

  bat1Ready = Power_IsBatteryCanPresent(1U);
  bat2Ready = Power_IsBatteryCanPresent(2U);
  if ((bat1Ready == 0U) && (bat2Ready == 0U))
  {
    return HAL_ERROR;
  }

  /*
   * 若进入充电前双电池均就绪，且已处于并联放电平衡态 (rechargeCurrentOnlyMode != 0)
   * 或两电池压差已在 0.15V 平衡阈值内，直接继承双充并网状态 (chargeCurrentOnlyMode = 1)，
   * 杜绝切入充电瞬间无故关断某侧电池 MOS 造成跳变振荡。
   */
  if ((bat1Ready != 0U) && (bat2Ready != 0U))
  {
    float diff = Power_GetBatteryCanVoltage(1U) - Power_GetBatteryCanVoltage(2U);
    float absDiff = (diff >= 0.0f) ? diff : -diff;
    if ((rechargeCurrentOnlyMode != 0U) || (absDiff <= POWER_CHARGE_BALANCE_ENTER_DIFF))
    {
      inheritParallelCharge = 1U;
    }
  }

  /* 1. 切入充电模式：关断预放电支路，释放预放电互斥索引 */
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  powerPreDischargeBatteryIndex = 0U;

  /* 2. 切入充电工作模式并初始化平衡与消抖状态 */
  dischargeModeEnabled = 1U;
  powerWorkMode = POWER_WORK_MODE_CHARGE;
  rechargeCurrentOnlyMode = 0U;
  rechargeBalanceSteadyStartTick = 0U;
  chargeCurrentOnlyMode = inheritParallelCharge;
  chargeBalanceSteadyStartTick = 0U;
  chargeSingleSwitchSteadyStartTick = 0U;
  chargeActiveSingleBat = 0U;
  chargeSingleSwitchedOnce = 0U;
  chargeReplyBatteryIndex = 0U;
  chargeReplyLastTxTick = HAL_GetTick() - POWER_CHARGE_REPLY_PERIOD_MS;

  /* 3. 立即执行模式任务，由 Power_UpdateChargeMode 原子化协同配置目标 MOS */
  Power_ModeTask();
  return HAL_OK;
}

void Power_ExitChargeMode(void)
{
  if (powerWorkMode == POWER_WORK_MODE_CHARGE)
  {
    HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);

    powerWorkMode = POWER_WORK_MODE_DISCHARGE;
    rechargeCurrentOnlyMode = 0U;
    rechargeBalanceSteadyStartTick = 0U;
    chargeCurrentOnlyMode = 0U;
    chargeBalanceSteadyStartTick = 0U;
    chargeSingleSwitchSteadyStartTick = 0U;
    chargeActiveSingleBat = 0U;
    chargeSingleSwitchedOnce = 0U;
    chargeReplyBatteryIndex = 0U;
    chargeReplyLastTxTick = HAL_GetTick();

    Power_ModeTask();
  }
}

static uint8_t Power_IsBatteryCanPresent(uint8_t batteryIndex)
{
#if POWER_TEST_BENCH_SUPPLY_MODE
  if ((POWER_TEST_SUPPLY_INPUT_CHANNEL != 0U) && (batteryIndex == POWER_TEST_SUPPLY_INPUT_CHANNEL))
  {
    return 1U;
  }
#endif
  /*
   * 充电模式电池准入条件：
   * 必须同时满足 CAN 通信在线（含状态帧与 MOS 帧有效、总压 >= 10V）且 BMS 明确允许放电（电源键已按下开启）。
   * 若电池未按电源开关（处于 BMS 禁放状态 0x02），其内部 BMS 功率管关断且板载高边驱动芯片无偏置供电，
   * 严禁将其纳入充电目标，保持其板载放电与回充 MOS 关断隔离。
   */
  return ((Power_IsBatteryCanAlive(batteryIndex) != 0U) &&
          (Power_IsBatteryBmsDischargeAllowed(batteryIndex) != 0U)) ? 1U : 0U;
}

/*
 * 充电态全回路 MOS 协同管理器：
 * 统一协同控制外部总充电 MOS (PB10)、各路回充与主放电 MOS：
 * - 并联双充 (mask == 0x03): 导通双路回充与主放电 MOS，支持双向大电流与吸收反电势
 * - 单充 BAT1 (mask == 0x01): 导通 BAT1 回充与主放电 MOS (保 VBUS 供电); 彻底关断 BAT2 回充与放电 MOS 隔离防倒灌
 * - 单充 BAT2 (mask == 0x02): 导通 BAT2 回充与主放电 MOS (保 VBUS 供电); 彻底关断 BAT1 回充与放电 MOS 隔离防倒灌
 * - 无充断开 (mask == 0x00): 关断双路回充 MOS，放电保电交由 Power_MaintainChargeModeVbusPower 维持单侧放电
 */
static void Power_SetChargeMos(uint8_t mask, uint8_t battery1Present, uint8_t battery2Present)
{
  /* 1. 总充电 MOS (PB10): 外部充电桩经背对背 MOS 直通 VBUS，只要有充电需求即导通 */
  HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port,
                    BAT_CHARGE_MOS_Pin,
                    (mask != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  if (mask == 0x03U)
  {
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;

    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
  }
  else if (mask == 0x01U)
  {
    /* 先彻底关断非充电侧 BAT2 (回充与放电 MOS)，防止微秒级跨电池短路倒灌 */
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    battery2Control.state = POWER_BATTERY_STATE_OFF;

    /* 再导通目标充电侧 BAT1 (回充与放电 MOS) */
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
  }
  else if (mask == 0x02U)
  {
    /* 先彻底关断非充电侧 BAT1 (回充与放电 MOS)，防止微秒级跨电池短路倒灌 */
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    battery1Control.state = POWER_BATTERY_STATE_OFF;

    /* 再导通目标充电侧 BAT2 (回充与放电 MOS) */
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
  }
  else
  {
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
    Power_MaintainChargeModeVbusPower(battery1Present, battery2Present);
  }
}

static void Power_UpdateChargeMode(uint8_t battery1Present, uint8_t battery2Present)
{
  float battery1Voltage;
  float battery2Voltage;
  float voltageDiff;
  uint8_t chargeMask = 0U;
  uint8_t replyBatteryIndex = 0U;
  uint32_t now = HAL_GetTick();

  if ((battery1Present != 0U) && (battery2Present != 0U))
  {
    battery1Voltage = Power_GetBatteryCanVoltage(1U);
    battery2Voltage = Power_GetBatteryCanVoltage(2U);
    voltageDiff = battery1Voltage - battery2Voltage;

    if (chargeCurrentOnlyMode == 0U)
    {
      if (voltageDiff > POWER_CHARGE_BALANCE_ENTER_DIFF)
      {
        chargeBalanceSteadyStartTick = 0U;

        /* 若当前正在单充 BAT1，切换到 BAT2 需持续稳定 500ms 消抖，防止 IR 抬升瞬态交替跳切 */
        if (chargeActiveSingleBat == 1U)
        {
          if (chargeSingleSwitchSteadyStartTick == 0U)
          {
            chargeSingleSwitchSteadyStartTick = now;
          }

          if ((now - chargeSingleSwitchSteadyStartTick) >= POWER_BALANCE_DEBOUNCE_MS)
          {
            chargeSingleSwitchSteadyStartTick = 0U;
            if (chargeSingleSwitchedOnce != 0U)
            {
              /* 此前已发生过单充换边，再次出现反向压差说明双电池开路电压已在内阻压降范围内达成平衡，直接锁存双充 */
              chargeCurrentOnlyMode = 1U;
              chargeActiveSingleBat = 0U;
              chargeSingleSwitchedOnce = 0U;
            }
            else
            {
              chargeSingleSwitchedOnce = 1U;
              chargeActiveSingleBat = 2U;
              chargeMask = 0x02U;
              replyBatteryIndex = 2U;
            }
          }
          else
          {
            /* 消抖期间维持原充电侧 BAT1 */
            chargeMask = 0x01U;
            replyBatteryIndex = 1U;
          }
        }
        else
        {
          chargeSingleSwitchSteadyStartTick = 0U;
          chargeActiveSingleBat = 2U;
          chargeMask = 0x02U;
          replyBatteryIndex = 2U;
        }
      }
      else if (voltageDiff < -POWER_CHARGE_BALANCE_ENTER_DIFF)
      {
        chargeBalanceSteadyStartTick = 0U;

        /* 若当前正在单充 BAT2，切换到 BAT1 需持续稳定 500ms 消抖，防止 IR 抬升瞬态交替跳切 */
        if (chargeActiveSingleBat == 2U)
        {
          if (chargeSingleSwitchSteadyStartTick == 0U)
          {
            chargeSingleSwitchSteadyStartTick = now;
          }

          if ((now - chargeSingleSwitchSteadyStartTick) >= POWER_BALANCE_DEBOUNCE_MS)
          {
            chargeSingleSwitchSteadyStartTick = 0U;
            if (chargeSingleSwitchedOnce != 0U)
            {
              /* 此前已发生过单充换边，再次出现反向压差说明双电池开路电压已在内阻压降范围内达成平衡，直接锁存双充 */
              chargeCurrentOnlyMode = 1U;
              chargeActiveSingleBat = 0U;
              chargeSingleSwitchedOnce = 0U;
            }
            else
            {
              chargeSingleSwitchedOnce = 1U;
              chargeActiveSingleBat = 1U;
              chargeMask = 0x01U;
              replyBatteryIndex = 1U;
            }
          }
          else
          {
            /* 消抖期间维持原充电侧 BAT2 */
            chargeMask = 0x02U;
            replyBatteryIndex = 2U;
          }
        }
        else
        {
          chargeSingleSwitchSteadyStartTick = 0U;
          chargeActiveSingleBat = 1U;
          chargeMask = 0x01U;
          replyBatteryIndex = 1U;
        }
      }
      else
      {
        /* 压差缩小至 0.15V 平衡区间，需持续稳定 500ms 消抖确认才锁存双充并网 */
        chargeSingleSwitchSteadyStartTick = 0U;
        if (chargeBalanceSteadyStartTick == 0U)
        {
          chargeBalanceSteadyStartTick = now;
        }

        if ((now - chargeBalanceSteadyStartTick) >= POWER_BALANCE_DEBOUNCE_MS)
        {
          chargeCurrentOnlyMode = 1U;
          chargeBalanceSteadyStartTick = 0U;
          chargeActiveSingleBat = 0U;
          chargeSingleSwitchedOnce = 0U;
        }
        else
        {
          /* 消抖等待期间维持原有充电侧，杜绝 0.0V 零点振荡 */
          if ((chargeActiveSingleBat == 1U) || (chargeActiveSingleBat == 2U))
          {
            replyBatteryIndex = chargeActiveSingleBat;
          }
          else
          {
            replyBatteryIndex = (voltageDiff <= 0.0f) ? 1U : 2U;
            chargeActiveSingleBat = replyBatteryIndex;
          }
          chargeMask = (replyBatteryIndex == 1U) ? 0x01U : 0x02U;
        }
      }
    }

    if (chargeCurrentOnlyMode != 0U)
    {
      /* 已达成压差平衡进入双充并网状态：锁存维持并联双充 (0x03)，不再因充电电流引起瞬态压差而回退单充 */
      chargeSingleSwitchSteadyStartTick = 0U;
      chargeBalanceSteadyStartTick = 0U;
      chargeActiveSingleBat = 0U;
      chargeSingleSwitchedOnce = 0U;
      chargeMask = 0x03U;
      replyBatteryIndex = (battery1Voltage <= battery2Voltage) ? 1U : 2U;
    }
  }
  else if (battery1Present != 0U)
  {
    chargeCurrentOnlyMode = 0U;
    chargeBalanceSteadyStartTick = 0U;
    chargeSingleSwitchSteadyStartTick = 0U;
    chargeActiveSingleBat = 0U;
    chargeSingleSwitchedOnce = 0U;
    chargeMask = 0x01U;
    replyBatteryIndex = 1U;
  }
  else if (battery2Present != 0U)
  {
    chargeCurrentOnlyMode = 0U;
    chargeBalanceSteadyStartTick = 0U;
    chargeSingleSwitchSteadyStartTick = 0U;
    chargeActiveSingleBat = 0U;
    chargeSingleSwitchedOnce = 0U;
    chargeMask = 0x02U;
    replyBatteryIndex = 2U;
  }
  else
  {
    chargeCurrentOnlyMode = 0U;
    chargeBalanceSteadyStartTick = 0U;
    chargeSingleSwitchSteadyStartTick = 0U;
    chargeActiveSingleBat = 0U;
    chargeSingleSwitchedOnce = 0U;
  }

  /* 统一控制总充 MOS、各路回充与主放电 MOS */
  Power_SetChargeMos(chargeMask, battery1Present, battery2Present);

  if ((replyBatteryIndex != 0U) &&
      ((replyBatteryIndex != chargeReplyBatteryIndex) ||
       ((now - chargeReplyLastTxTick) >= POWER_CHARGE_REPLY_PERIOD_MS)))
  {
    if (FDCAN_SendChargeReplyToCharger(replyBatteryIndex) == HAL_OK)
    {
      chargeReplyLastTxTick = now;
      chargeReplyBatteryIndex = replyBatteryIndex;
    }
  }

  if (chargeMask == 0U)
  {
    chargeCurrentOnlyMode = 0U;
    chargeBalanceSteadyStartTick = 0U;
    chargeSingleSwitchSteadyStartTick = 0U;
    chargeActiveSingleBat = 0U;
    chargeSingleSwitchedOnce = 0U;
    chargeReplyBatteryIndex = 0U;
    chargeReplyLastTxTick = now;
  }
}



uint8_t Power_GetBattery1AlarmStatus(void)
{
  return battery1AlarmStatus;
}

uint8_t Power_GetBattery2AlarmStatus(void)
{
  return battery2AlarmStatus;
}

uint8_t Power_GetAuxPowerStatusMask(void)
{
  uint8_t mask = 0U;

  if (peripheral_power_state != 0U)
  {
    mask |= POWER_AUX_STATUS_PERIPHERAL_MOS_BIT;
  }
  if (dcdc_12v_state != 0U)
  {
    mask |= POWER_AUX_STATUS_DCDC_12V_BIT;
  }
  if (dcdc_24v_state != 0U)
  {
    mask |= POWER_AUX_STATUS_DCDC_24V_BIT;
  }

  return mask;
}

uint8_t Power_IsBatteryPhysicallyPresent(uint8_t batteryIndex)
{
  uint8_t canAlive;
  uint8_t otherBatteryIndex;
  uint8_t otherDischarging;

  if ((batteryIndex < 1U) || (batteryIndex > 2U))
  {
    return 0U;
  }

  canAlive = Power_IsBatteryCanAlive(batteryIndex);

  /*
   * 电池物理在位判定：
   * 1. 只要 CAN 保持在线通信，说明电池物理插入且处于开机通信状态，判定为在位 (1)；
   * 2. 若 CAN 掉线，需区分单电池放电与双电池并联放电：
   *    - 若当前处于放电状态 (POWER_BATTERY_STATE_DISCHARGE)，且另一路电池未处于放电态（单电池放电）：
   *      这是系统唯一的动力来源，判定为物理在位 (1)，让状态机进入 CAN_COMM_LOST (0x01)
   *      保持放电动力不断电，并通知小脑受控下蹲，杜绝粗暴断电摔狗；
   *    - 若双路电池此前均处于放电状态 (双电池并联放电)：
   *      某路 CAN 掉线说明该电池极大概率已被物理热拔出。
   *      此时另一路电池健康在线，足以独立支撑系统运行；为杜绝放电/回充 MOS 持续导通导致空插座带电
   *      以及后续热插入失去压差防倒灌保护，此处坚决判定为不在位 (0)，
   *      促使状态机立即关断该路全部 MOS 并复位为 OFF；
   * 3. 其它非放电状态下 CAN 超时均判定为不在位 (0)。
   */
  if (canAlive != 0U)
  {
    return 1U;
  }

  otherBatteryIndex = (batteryIndex == 1U) ? 2U : 1U;
  otherDischarging = (((otherBatteryIndex == 1U) ? battery1Control.state : battery2Control.state) == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U;

  if ((((batteryIndex == 1U) ? battery1Control.state : battery2Control.state) == POWER_BATTERY_STATE_DISCHARGE) &&
      (otherDischarging == 0U))
  {
    return 1U;
  }

  return 0U;
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
    return ((battery1_can_rx_count != 0U) &&
            (battery1_can_mos_rx_count != 0U) &&
            (battery1_can_sum_voltage >= FDCAN_BATTERY_VOLTAGE_MIN) &&
            ((now - battery1_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
            ((now - battery1_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
  }

  return ((battery2_can_rx_count != 0U) &&
            (battery2_can_mos_rx_count != 0U) &&
            (battery2_can_sum_voltage >= FDCAN_BATTERY_VOLTAGE_MIN) &&
            ((now - battery2_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
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

static float Power_GetBatteryCanVoltage(uint8_t batteryIndex)
{
  return (batteryIndex == 1U) ? battery1_can_sum_voltage : battery2_can_sum_voltage;
}

static float Power_GetBalancedVoltageDiff(void)
{
  /*
   * 双电池压差基准：采用双电池内部 BMS 采集的电芯总压作差
   */
  if ((Power_IsBatteryCanAlive(1U) != 0U) && (Power_IsBatteryCanAlive(2U) != 0U))
  {
    return Power_GetBatteryCanVoltage(1U) - Power_GetBatteryCanVoltage(2U);
  }

  return 0.0f;
}

static uint8_t Power_GetRechargeMaskByCanVoltage(uint8_t battery1Ready, uint8_t battery2Ready)
{
  float voltageDiff;
  uint32_t now = HAL_GetTick();

  if ((battery1Ready != 0U) && (battery2Ready != 0U))
  {
    /* 必须双路 CAN 均在线，平衡态与电芯压差才具备防倒灌评估价值 */
    if ((Power_IsBatteryCanAlive(1U) != 0U) && (Power_IsBatteryCanAlive(2U) != 0U))
    {
      /* 若此前在双路在线状态下已确认达成压差平衡，维持全回充模式 */
      if (rechargeCurrentOnlyMode != 0U)
      {
        return 0x03U;
      }

      voltageDiff = Power_GetBalancedVoltageDiff();

      if (voltageDiff > POWER_RECHARGE_BALANCE_DIFF)
      {
        rechargeBalanceSteadyStartTick = 0U;
        return 0x01U;
      }

      if (voltageDiff < -POWER_RECHARGE_BALANCE_DIFF)
      {
        rechargeBalanceSteadyStartTick = 0U;
        return 0x02U;
      }

      /* 压差进入平衡区间 (|V1 - V2| <= 0.10V)，需持续稳定 500ms 消抖确认 */
      if (rechargeBalanceSteadyStartTick == 0U)
      {
        rechargeBalanceSteadyStartTick = now;
      }

      if ((now - rechargeBalanceSteadyStartTick) >= POWER_BALANCE_DEBOUNCE_MS)
      {
        rechargeCurrentOnlyMode = 1U;
        return 0x03U;
      }

      /* 消抖确认等待期间，暂时开通电压略高一侧的回充 MOS */
      return (voltageDiff >= 0.0f) ? 0x01U : 0x02U;
    }

    /*
     * 边界保护：若此前处于双电放电，但其中任一电池 CAN 掉线：
     * 1. 立即重置平衡标志与消抖计时器，禁止全开双路回充；
     * 2. 仅关断掉线电池的回充 MOS，保持健康在线电池的回充 MOS 导通（用于吸收电机反电动势）；
     * 3. 掉线电池放电/回充 MOS 均被关断，母线间绝无环流风险，在线电池可安全吸收再生制动回充能量。
     */
    rechargeCurrentOnlyMode = 0U;
    rechargeBalanceSteadyStartTick = 0U;

    if (Power_IsBatteryCanAlive(1U) != 0U)
    {
      return 0x01U;
    }
    if (Power_IsBatteryCanAlive(2U) != 0U)
    {
      return 0x02U;
    }
    return 0x00U;
  }

  rechargeCurrentOnlyMode = 0U;
  rechargeBalanceSteadyStartTick = 0U;

  if ((battery1Ready != 0U) && (Power_IsBatteryCanAlive(1U) != 0U))
  {
    return 0x01U;
  }

  if ((battery2Ready != 0U) && (Power_IsBatteryCanAlive(2U) != 0U))
  {
    return 0x02U;
  }

  return 0U;
}

static void Power_SetRechargeMos(uint8_t mask)
{
  /* 先关断需关闭的回充 MOS，再导通需开启的回充 MOS，遵循先断后通原则 */
  if ((mask & 0x01U) == 0U)
  {
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  }
  if ((mask & 0x02U) == 0U)
  {
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  }
  if ((mask & 0x01U) != 0U)
  {
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  }
  if ((mask & 0x02U) != 0U)
  {
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  }
}

static void Power_UpdatePeripheralPower(uint8_t battery1Present, uint8_t battery2Present)
{
  (void)battery1Present;
  (void)battery2Present;

  /*
   * 外设供电 MOS（PA5）控制规则：
   * 电池进入预充电（POWER_BATTERY_STATE_PRE_DISCHARGE）时直接打开外设供电 MOS，
   * 并在主放电（POWER_BATTERY_STATE_DISCHARGE）或充电模式（POWER_WORK_MODE_CHARGE）下持续保持导通；
   * 仅当双路电池均关闭且非充电模式时关闭外设供电。
   */
  if ((battery1Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE) ||
      (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ||
      (battery2Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE) ||
      (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE) ||
      (powerWorkMode == POWER_WORK_MODE_CHARGE))
  {
    HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
  }
  else
  {
    HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);
  }

  /*
   * 12V/24V 隔离 DCDC（PA7/PC4）控制规则：
   * 仅当母线切实有动力来源时开启 DCDC，待母线上电后再给小脑（12V）与声光面板（24V）通电：
   * 1. 至少有一路电池实际处于主放电工作状态 (POWER_BATTERY_STATE_DISCHARGE)；
   * 2. 或处于充电模式 (POWER_WORK_MODE_CHARGE)，外部充电桩已为母线供电。
   */
  if ((battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ||
      (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE) ||
      (powerWorkMode == POWER_WORK_MODE_CHARGE))
  {
    Power_SetDcdc12V(1U);
    Power_SetDcdc24V(1U);
    Power_TriggerBootBeep();  
  }
  else
  {
    Power_SetDcdc12V(0U);
    Power_SetDcdc24V(0U);
  }
}




static void Power_StartBattery1PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

  battery1PreDischargeStartTick = HAL_GetTick();
  battery1Control.state = POWER_BATTERY_STATE_PRE_DISCHARGE;
  powerPreDischargeBatteryIndex = 1U;
}

static void Power_StartBattery2PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

  battery2PreDischargeStartTick = HAL_GetTick();
  battery2Control.state = POWER_BATTERY_STATE_PRE_DISCHARGE;
  powerPreDischargeBatteryIndex = 2U;
}

static void Power_FinishBattery1PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  powerPreDischargeBatteryIndex = 0U;
}

static void Power_FinishBattery2PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  powerPreDischargeBatteryIndex = 0U;
}

static void Power_EnableBattery1DischargePath(void)
{
  /* 新增主放电前，依据双电池压差直接同步配置回充 MOS，杜绝低压电池回充 MOS 瞬态误开通 */
  Power_UpdateRechargeMos(1U, (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

static void Power_EnableBattery2DischargePath(void)
{
  /* 新增主放电前，依据双电池压差直接同步配置回充 MOS，杜绝低压电池回充 MOS 瞬态误开通 */
  Power_UpdateRechargeMos((battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U, 1U);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

static void Power_UpdateBattery1Path(uint8_t present)
{
  if (present == 0U)
  {
    Power_DisableBattery1Path();
    return;
  }

  if (battery1Control.state == POWER_BATTERY_STATE_OFF)
  {
    if (powerPreDischargeBatteryIndex == 0U)
    {
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
  else
  {
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

    /* 开启主放电 MOS 后延迟 200ms 关闭预充 MOS，实现平滑重叠导通 */
    if (powerPreDischargeBatteryIndex == 1U)
    {
      if ((HAL_GetTick() - battery1PreDischargeStartTick) >= (POWER_PRE_DISCHARGE_DELAY_MS + POWER_PRE_DISCHARGE_OVERLAP_MS))
      {
        Power_FinishBattery1PreDischarge();
      }
    }
  }
}

static void Power_UpdateBattery2Path(uint8_t present)
{
  if (present == 0U)
  {
    Power_DisableBattery2Path();
    return;
  }

  if (battery2Control.state == POWER_BATTERY_STATE_OFF)
  {
    if (powerPreDischargeBatteryIndex == 0U)
    {
      Power_StartBattery2PreDischarge();
    }
  }
  else if (battery2Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
  {
    if ((HAL_GetTick() - battery2PreDischargeStartTick) >= POWER_PRE_DISCHARGE_DELAY_MS)
    {
      Power_EnableBattery2DischargePath();
    }
  }
  else
  {
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

    /* 开启主放电 MOS 后延迟 200ms 关闭预充 MOS，实现平滑重叠导通 */
    if (powerPreDischargeBatteryIndex == 2U)
    {
      if ((HAL_GetTick() - battery2PreDischargeStartTick) >= (POWER_PRE_DISCHARGE_DELAY_MS + POWER_PRE_DISCHARGE_OVERLAP_MS))
      {
        Power_FinishBattery2PreDischarge();
      }
    }
  }
}

static void Power_DisableBattery1Path(void)
{
  HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);

  if (powerPreDischargeBatteryIndex == 1U)
  {
    powerPreDischargeBatteryIndex = 0U;
  }

  battery1Control.state = POWER_BATTERY_STATE_OFF;
  rechargeCurrentOnlyMode = 0U;
  rechargeBalanceSteadyStartTick = 0U;
}

static void Power_DisableBattery2Path(void)
{
  HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);

  if (powerPreDischargeBatteryIndex == 2U)
  {
    powerPreDischargeBatteryIndex = 0U;
  }

  battery2Control.state = POWER_BATTERY_STATE_OFF;
  rechargeCurrentOnlyMode = 0U;
  rechargeBalanceSteadyStartTick = 0U;
}

static void Power_UpdateRechargeMos(uint8_t battery1Ready, uint8_t battery2Ready)
{
  Power_SetRechargeMos(Power_GetRechargeMaskByCanVoltage(battery1Ready, battery2Ready));
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
   * 2. 小脑控制模式 (若未被调试屏蔽且指令有效)
   * 3. 本地开机提示音模式
   */
  if (back_emf_absorb_state != 0U)
  {
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

void Power_UpdateStatusLed(void)
{
  Power_UpdateStatusIndicators();
}

void Power_UpdateBoardLeds(void)
{
  uint32_t now = HAL_GetTick();
  GPIO_PinState led2State = BOARD_LED_OFF;
  GPIO_PinState led3State = BOARD_LED_OFF;

  /* --- LED2: 板载系统运行心跳灯 (PB1) --- */
  /* 若存在 CAN Bus-Off 错误、双电全无动力离线或电池 BMS 禁放故障，5Hz 快速闪烁 (100ms 翻转) 告警；
   * 至少一路正常供电且无故障时（单电运行为常态健康），以 1Hz 慢闪 (500ms 翻转) 维持健康心跳 */
  uint8_t hasAlarm = ((FDCAN_IsAnyBusOff() != 0U) ||
                      (battery1AlarmStatus == BATTERY_ALARM_STATUS_PROHIBITED) ||
                      (battery2AlarmStatus == BATTERY_ALARM_STATUS_PROHIBITED) ||
                      ((battery1AlarmStatus == BATTERY_ALARM_STATUS_OFFLINE) &&
                       (battery2AlarmStatus == BATTERY_ALARM_STATUS_OFFLINE))) ? 1U : 0U;
  if (hasAlarm != 0U)
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
  else if ((battery1Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE) ||
           (battery2Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE))
  {
    /* 上电预充中：2Hz 闪烁 (250ms 翻转) 提示正在预充缓启动 */
    led3State = (((now / 250U) % 2U) == 0U) ? BOARD_LED_ON : BOARD_LED_OFF;
  }
  else if (((battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ||
            (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE) ||
            (powerWorkMode == POWER_WORK_MODE_CHARGE)) &&
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
    g_hotBootBatMask = (uint8_t)(bkp1 & 0xFFFFU);
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
  if (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE)
  {
    mask |= 0x02U;
  }

  /* 兜底：若状态机未标明 DISCHARGE，回读物理引脚电平 */
  if (mask == 0U)
  {
    if (HAL_GPIO_ReadPin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON)
    {
      mask |= 0x01U;
    }
    if (HAL_GPIO_ReadPin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON)
    {
      mask |= 0x02U;
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
  /* GPIOC: PC4 (24V DCDC, 低使能), PC2 (绿灯), PC11 (Bat2主放电, 高开) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, POWER_DCDC_ON); /* 保持 24V DCDC 导通 (Active Low) */
#if defined(POWER_STATUS_LED_FLOWING_TEST_ENABLE) && (POWER_STATUS_LED_FLOWING_TEST_ENABLE != 0U)
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);  /* IAP 流水灯模式：热启动初始不常亮绿灯 */
#else
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);    /* 恢复绿灯指示正常工作态 */
#endif
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, ((batMask & 0x02U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_RESET);

  /* GPIOA: PA7 (12V DCDC, 低使能), PA5 (外设供电, 关) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, POWER_DCDC_ON); /* 保持 12V DCDC 持续不断电 (Active Low) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /* GPIOB: PB6 (Bat1主放电, 高开), 板载 LED2/LED3 置默认电平, 其余关 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, ((batMask & 0x01U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(GPIOB, LED2_Pin | LED3_Pin | GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_RESET);

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
