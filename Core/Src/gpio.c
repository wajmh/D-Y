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

typedef enum
{
  POWER_BATTERY_STATE_OFF = 0,
  POWER_BATTERY_STATE_PRE_DISCHARGE,
  POWER_BATTERY_STATE_DISCHARGE
} PowerBatteryState_t;

typedef struct
{
  PowerBatteryState_t state;
} PowerBatteryControl_t;

static PowerBatteryControl_t battery1Control = {POWER_BATTERY_STATE_OFF};
static PowerBatteryControl_t battery2Control = {POWER_BATTERY_STATE_OFF};
static uint8_t dischargeModeEnabled = 0U;
static uint8_t powerPreDischargeDone = 0U;
static uint8_t powerPreDischargeBatteryIndex = 0U;
static uint8_t backEmfAbsorbReleased = 0U;
static uint32_t battery1PreDischargeStartTick = 0U;
static uint32_t battery2PreDischargeStartTick = 0U;
static uint8_t rechargeCurrentOnlyMode = 0U;
static uint8_t battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
static uint8_t battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;

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

static uint8_t Power_IsBatteryCanReady(uint8_t batteryIndex);
static float Power_GetBatteryCanVoltage(uint8_t batteryIndex);
static uint8_t Power_GetRechargeMaskByCanVoltage(uint8_t battery1Ready, uint8_t battery2Ready);
static void Power_SetRechargeMos(uint8_t mask);
static void Power_UpdatePeripheralPower(uint8_t battery1Present, uint8_t battery2Present);
static void Power_StartBattery1PreDischarge(void);
static void Power_StartBattery2PreDischarge(void);
static void Power_FinishBattery1PreDischarge(void);
static void Power_FinishBattery2PreDischarge(void);
static void Power_ReleaseBackEmfAbsorbOnce(void);
static void Power_EnableBattery1DischargePath(void);
static void Power_EnableBattery2DischargePath(void);
static void Power_DisableLowerRechargeMosBeforeNewDischarge(uint8_t newBatteryIndex);
static void Power_UpdateBattery1Path(uint8_t present);
static void Power_UpdateBattery2Path(uint8_t present);
static void Power_DisableBattery1Path(void);
static void Power_DisableBattery2Path(void);
static void Power_UpdateRechargeMos(uint8_t battery1Ready, uint8_t battery2Ready);
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

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4|GPIO_PIN_11|GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA0 PA4 PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PC4 PC11 PC12 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB10 PB11 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PD2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */
void Power_UpdateGpioDebugStates(void)
{
  bat1_charge_mos_state = (HAL_GPIO_ReadPin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_discharge_mos_state = (HAL_GPIO_ReadPin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_recharge_mos_state = (HAL_GPIO_ReadPin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_pre_discharge_mos_state = (HAL_GPIO_ReadPin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  bat2_charge_mos_state = (HAL_GPIO_ReadPin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_discharge_mos_state = (HAL_GPIO_ReadPin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_recharge_mos_state = (HAL_GPIO_ReadPin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_pre_discharge_mos_state = (HAL_GPIO_ReadPin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  peripheral_power_state = (HAL_GPIO_ReadPin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  back_emf_absorb_state = (HAL_GPIO_ReadPin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
}

void Power_AllMosOff(void)
{
  HAL_GPIO_WritePin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin, POWER_SWITCH_ON);//线接错了，反接下
  backEmfAbsorbReleased = 0U;

  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);

  if (powerPreDischargeDone == 0U)
  {
    HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    powerPreDischargeBatteryIndex = 0U;
  }
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);
}

HAL_StatusTypeDef Power_EnterDischargeMode(void)
{
  dischargeModeEnabled = 1U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;

  Power_DischargeModeTask();
  return HAL_OK;
}

void Power_DischargeModeTask(void)
{
  uint8_t bat1CanReady;
  uint8_t bat2CanReady;
  uint8_t bat1PhysOnline;
  uint8_t bat2PhysOnline;
  uint8_t bat1KeepEnable;
  uint8_t bat2KeepEnable;

  if (dischargeModeEnabled == 0U)
  {
    return;
  }

  bat1CanReady = Power_IsBatteryCanReady(1U);
  bat2CanReady = Power_IsBatteryCanReady(2U);
  bat1PhysOnline = Power_IsBatteryPhysicallyPresent(1U);
  bat2PhysOnline = Power_IsBatteryPhysicallyPresent(2U);

  /* 更新电池 1 报警状态：拔出优先于掉线 */
  if (bat1PhysOnline == 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  }
  else if (bat1CanReady == 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_CAN_COMM_LOST;
  }
  else
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  }

  /* 更新电池 2 报警状态：拔出优先于掉线 */
  if (bat2PhysOnline == 0U)
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  }
  else if (bat2CanReady == 0U)
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_CAN_COMM_LOST;
  }
  else
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  }

  /* 只有在两块电池均被物理拔出/均无物理电压时，才彻底关断所有 MOS */
  if ((bat1PhysOnline == 0U) && (bat2PhysOnline == 0U))
  {
    Power_AllMosOff();
    return;
  }

  /* 若 CAN 就绪，或物理电压在线且已处于放电态(真·通信掉线)，均保持放电回路 */
  bat1KeepEnable = (bat1CanReady != 0U) || ((bat1PhysOnline != 0U) && (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE));
  bat2KeepEnable = (bat2CanReady != 0U) || ((bat2PhysOnline != 0U) && (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE));

  Power_UpdateBattery1Path(bat1KeepEnable);
  Power_UpdateBattery2Path(bat2KeepEnable);
  Power_UpdateRechargeMos((battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U,
                          (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U);
  Power_UpdatePeripheralPower(bat1PhysOnline, bat2PhysOnline);
}

void Power_ExitDischargeMode(void)
{
  dischargeModeEnabled = 0U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  Power_AllMosOff();
}

void Power_TestBattery1DischargeSequence(void)
{
  
  Power_AllMosOff();

  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_Delay(POWER_PRE_DISCHARGE_DELAY_MS);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
}//测试函数

GPIO_PinState Power_ReadEmergencyStop(void)
{
  return HAL_GPIO_ReadPin(EMERGENCY_STOP_GPIO_Port, EMERGENCY_STOP_Pin);
}

uint8_t Power_IsEmergencyStopActive(void)
{
  static GPIO_PinState lastRawState = GPIO_PIN_SET;
  static GPIO_PinState debouncedState = GPIO_PIN_SET;
  static uint32_t lastChangeTick = 0U;
  GPIO_PinState rawState = Power_ReadEmergencyStop();
  uint32_t now = HAL_GetTick();

  if (rawState != lastRawState)
  {
    lastRawState = rawState;
    lastChangeTick = now;
  }

  if ((now - lastChangeTick) >= POWER_ESTOP_DEBOUNCE_MS)
  {
    debouncedState = lastRawState;
  }

  return (debouncedState == POWER_ESTOP_ACTIVE_STATE) ? 1U : 0U;
}

uint8_t Power_GetBattery1AlarmStatus(void)
{
  return battery1AlarmStatus;
}

uint8_t Power_GetBattery2AlarmStatus(void)
{
  return battery2AlarmStatus;
}

uint8_t Power_IsBatteryPhysicallyPresent(uint8_t batteryIndex)
{
  float voltage = (batteryIndex == 1U) ? battery1_voltage : battery2_voltage;
  return (voltage >= BATTERY_PHYSICAL_PRESENT_VOLTAGE) ? 1U : 0U;
}

static uint8_t Power_IsBatteryCanReady(uint8_t batteryIndex)
{
  uint32_t now = HAL_GetTick();

  if (batteryIndex == 1U)
  {
    return ((battery1_can_rx_count != 0U) &&
            (battery1_can_mos_rx_count != 0U) &&
            ((now - battery1_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
            ((now - battery1_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
            (battery1_can_discharge_mos_state != 0U)) ? 1U : 0U;
  }

  return ((battery2_can_rx_count != 0U) &&
          (battery2_can_mos_rx_count != 0U) &&
          ((now - battery2_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
          ((now - battery2_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
          (battery2_can_discharge_mos_state != 0U)) ? 1U : 0U;
}

static float Power_GetBatteryCanVoltage(uint8_t batteryIndex)
{
  return (batteryIndex == 1U) ? battery1_can_sum_voltage : battery2_can_sum_voltage;
}

static uint8_t Power_GetRechargeMaskByCanVoltage(uint8_t battery1Ready, uint8_t battery2Ready)
{
  float voltageDiff;

  if ((battery1Ready != 0U) && (battery2Ready != 0U))
  {
    if (rechargeCurrentOnlyMode == 0U)
    {
      voltageDiff = Power_GetBatteryCanVoltage(1U) - Power_GetBatteryCanVoltage(2U);

      if (voltageDiff > POWER_RECHARGE_BALANCE_DIFF)
      {
        return 0x01U;
      }

      if (voltageDiff < -POWER_RECHARGE_BALANCE_DIFF)
      {
        return 0x02U;
      }

      rechargeCurrentOnlyMode = 1U;
    }

    return 0x03U;
  }

  rechargeCurrentOnlyMode = 0U;

  if (battery1Ready != 0U)
  {
    return 0x01U;
  }

  if (battery2Ready != 0U)
  {
    return 0x02U;
  }

  return 0U;
}

static void Power_SetRechargeMos(uint8_t mask)
{
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port,
                    BAT1_RECHARGE_MOS_Pin,
                    ((mask & 0x01U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port,
                    BAT2_RECHARGE_MOS_Pin,
                    ((mask & 0x02U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
}

static void Power_UpdatePeripheralPower(uint8_t battery1Present, uint8_t battery2Present)
{
  if ((battery1Present != 0U) || (battery2Present != 0U))
  {
    HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
  }
  else
  {
    HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);
  }
}




static void Power_StartBattery1PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

  battery1PreDischargeStartTick = HAL_GetTick();
  battery1Control.state = POWER_BATTERY_STATE_PRE_DISCHARGE;
  powerPreDischargeBatteryIndex = 1U;
}

static void Power_StartBattery2PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

  battery2PreDischargeStartTick = HAL_GetTick();
  battery2Control.state = POWER_BATTERY_STATE_PRE_DISCHARGE;
  powerPreDischargeBatteryIndex = 2U;
}

static void Power_FinishBattery1PreDischarge(void)
{
  Power_EnableBattery1DischargePath();
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  powerPreDischargeDone = 1U;
  powerPreDischargeBatteryIndex = 0U;
}

static void Power_FinishBattery2PreDischarge(void)
{
  Power_EnableBattery2DischargePath();
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  powerPreDischargeDone = 1U;
  powerPreDischargeBatteryIndex = 0U;
}

static void Power_ReleaseBackEmfAbsorbOnce(void)
{
  if (backEmfAbsorbReleased == 0U)
  {
    HAL_Delay(POWER_PRE_DISCHARGE_DELAY);
    HAL_GPIO_WritePin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin, POWER_SWITCH_OFF);//反接
    backEmfAbsorbReleased = 1U;
  }//只操作一次
}

static void Power_EnableBattery1DischargePath(void)
{
  Power_DisableLowerRechargeMosBeforeNewDischarge(1U);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  Power_ReleaseBackEmfAbsorbOnce();
  battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

static void Power_EnableBattery2DischargePath(void)
{
  Power_DisableLowerRechargeMosBeforeNewDischarge(2U);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  Power_ReleaseBackEmfAbsorbOnce();
  battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

static void Power_DisableLowerRechargeMosBeforeNewDischarge(uint8_t newBatteryIndex)
{
  float voltageDiff = Power_GetBatteryCanVoltage(1U) - Power_GetBatteryCanVoltage(2U);

  if ((newBatteryIndex == 1U) &&
      (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE) &&
      (voltageDiff > POWER_RECHARGE_BALANCE_DIFF))
  {
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  }
  else if ((newBatteryIndex == 2U) &&
           (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) &&
           (voltageDiff < -POWER_RECHARGE_BALANCE_DIFF))
  {
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  }
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
    if (powerPreDischargeDone != 0U)//预放电结束
    {
      Power_EnableBattery1DischargePath();
    }
    else if (powerPreDischargeBatteryIndex == 0U)//刚开始预放电
    {
      Power_StartBattery1PreDischarge();
    }
  }
  else if (battery1Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
  {
    if ((HAL_GetTick() - battery1PreDischargeStartTick) >= POWER_PRE_DISCHARGE_DELAY_MS)
    {
      Power_FinishBattery1PreDischarge();
    }
  }
  else
  {
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
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
    if (powerPreDischargeDone != 0U)
    {
      Power_EnableBattery2DischargePath();
    }
    else if (powerPreDischargeBatteryIndex == 0U)
    {
      Power_StartBattery2PreDischarge();
    }
  }
  else if (battery2Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
  {
    if ((HAL_GetTick() - battery2PreDischargeStartTick) >= POWER_PRE_DISCHARGE_DELAY_MS)
    {
      Power_FinishBattery2PreDischarge();
    }
  }
  else
  {
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  }
}

static void Power_DisableBattery1Path(void)
{
  HAL_GPIO_WritePin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);

  if (powerPreDischargeDone == 0U)
  {
    HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    if (powerPreDischargeBatteryIndex == 1U)
    {
      powerPreDischargeBatteryIndex = 0U;
    }
  }

  battery1Control.state = POWER_BATTERY_STATE_OFF;
}

static void Power_DisableBattery2Path(void)
{
  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);

  if (powerPreDischargeDone == 0U)
  {
    HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    if (powerPreDischargeBatteryIndex == 2U)
    {
      powerPreDischargeBatteryIndex = 0U;
    }
  }

  battery2Control.state = POWER_BATTERY_STATE_OFF;
}

static void Power_UpdateRechargeMos(uint8_t battery1Ready, uint8_t battery2Ready)
{
  Power_SetRechargeMos(Power_GetRechargeMaskByCanVoltage(battery1Ready, battery2Ready));
}
/* USER CODE END 2 */
