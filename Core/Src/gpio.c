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
static uint8_t chargeCurrentOnlyMode = 0U;
static uint8_t chargeReplyBatteryIndex = 0U;
static uint32_t chargeReplyLastTxTick = 0U;
static uint8_t battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
static uint8_t battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;

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

static uint8_t Power_IsBatteryCanAlive(uint8_t batteryIndex);
static uint8_t Power_IsBatteryBmsDischargeAllowed(uint8_t batteryIndex);
static uint8_t Power_IsBatteryBmsDischargeProhibited(uint8_t batteryIndex);
static uint8_t Power_IsBatteryBmsDischargeUnknown(uint8_t batteryIndex);
static uint8_t Power_IsBatteryBmsMosFresh(uint8_t batteryIndex);
static uint8_t Power_IsBatteryCanReady(uint8_t batteryIndex);
static uint8_t Power_IsBatteryCanPresent(uint8_t batteryIndex);
static float Power_GetBatteryCanVoltage(uint8_t batteryIndex);
__attribute__((unused))
static float Power_GetBatteryEffectiveVoltage(uint8_t batteryIndex);
static float Power_GetBalancedVoltageDiff(void);
static void Power_MaintainChargeModeVbusPower(uint8_t battery1Present, uint8_t battery2Present);
static uint8_t Power_GetRechargeMaskByCanVoltage(uint8_t battery1Ready, uint8_t battery2Ready);
static void Power_SetRechargeMos(uint8_t mask);
static void Power_SetChargeMos(uint8_t mask);
static void Power_UpdateChargeMode(uint8_t battery1Present, uint8_t battery2Present);
static void Power_UpdatePeripheralPower(uint8_t battery1Present, uint8_t battery2Present);
static void Power_StartBattery1PreDischarge(void);
static void Power_StartBattery2PreDischarge(void);
static void Power_FinishBattery1PreDischarge(void);
static void Power_FinishBattery2PreDischarge(void);
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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_2
                          |GPIO_PIN_4|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5|GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_6
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

  /*Configure GPIO pins : PB10 PB11 PB12 PB6
                           PB7 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_6
                          |GPIO_PIN_7|GPIO_PIN_9;
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
  bat_charge_mos_state = (HAL_GPIO_ReadPin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_charge_mos_state = bat_charge_mos_state;
  bat1_discharge_mos_state = (HAL_GPIO_ReadPin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_recharge_mos_state = (HAL_GPIO_ReadPin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat1_pre_discharge_mos_state = (HAL_GPIO_ReadPin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  bat2_charge_mos_state = bat_charge_mos_state;
  bat2_discharge_mos_state = (HAL_GPIO_ReadPin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_recharge_mos_state = (HAL_GPIO_ReadPin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_pre_discharge_mos_state = (HAL_GPIO_ReadPin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  peripheral_power_state = (HAL_GPIO_ReadPin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  back_emf_absorb_1_state = (HAL_GPIO_ReadPin(BACK_EMF_ABSORB_1_GPIO_Port, BACK_EMF_ABSORB_1_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  back_emf_absorb_2_state = (HAL_GPIO_ReadPin(BACK_EMF_ABSORB_2_GPIO_Port, BACK_EMF_ABSORB_2_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  back_emf_absorb_state = ((back_emf_absorb_1_state != 0U) || (back_emf_absorb_2_state != 0U)) ? 1U : 0U;
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

  /* 两路反电势吸收泄放 MOS 均关断 */
  HAL_GPIO_WritePin(BACK_EMF_ABSORB_1_GPIO_Port, BACK_EMF_ABSORB_1_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BACK_EMF_ABSORB_2_GPIO_Port, BACK_EMF_ABSORB_2_Pin, POWER_SWITCH_OFF);
  backEmfAbsorbReleased = 0U;

  powerPreDischargeBatteryIndex = 0U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);
}

HAL_StatusTypeDef Power_EnterDischargeMode(void)
{
  dischargeModeEnabled = 1U;
  powerWorkMode = POWER_WORK_MODE_DISCHARGE;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  battery1AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  chargeReplyBatteryIndex = 0U;
  chargeReplyLastTxTick = HAL_GetTick();

  Power_ModeTask();
  return HAL_OK;
}

void Power_DischargeModeTask(void)
{
  uint8_t bat1CanAlive;
  uint8_t bat1BmsAllowed;
  uint8_t bat1BmsProhibited;
  uint8_t bat1BmsUnknown;
  uint8_t bat1CanReady;
  uint8_t bat1PhysOnline;
  uint8_t bat1Enable;

  uint8_t bat2CanAlive;
  uint8_t bat2BmsAllowed;
  uint8_t bat2BmsProhibited;
  uint8_t bat2BmsUnknown;
  uint8_t bat2CanReady;
  uint8_t bat2PhysOnline;
  uint8_t bat2Enable;

  if (dischargeModeEnabled == 0U)
  {
    return;
  }

  bat1CanAlive = Power_IsBatteryCanAlive(1U);
  bat1BmsAllowed = Power_IsBatteryBmsDischargeAllowed(1U);
  bat1BmsProhibited = Power_IsBatteryBmsDischargeProhibited(1U);
  bat1BmsUnknown = Power_IsBatteryBmsDischargeUnknown(1U);
  bat1CanReady = Power_IsBatteryCanReady(1U);
  bat1PhysOnline = Power_IsBatteryPhysicallyPresent(1U);

  bat2CanAlive = Power_IsBatteryCanAlive(2U);
  bat2BmsAllowed = Power_IsBatteryBmsDischargeAllowed(2U);
  bat2BmsProhibited = Power_IsBatteryBmsDischargeProhibited(2U);
  bat2BmsUnknown = Power_IsBatteryBmsDischargeUnknown(2U);
  bat2CanReady = Power_IsBatteryCanReady(2U);
  bat2PhysOnline = Power_IsBatteryPhysicallyPresent(2U);

  /*
   * 电池 1 报警状态判定：
   * 1. 最近的 MOS 帧明确禁放或状态无效：优先关闭本地路径；
   * 2. 物理电压低：判定为物理拔出；
   * 3. 物理在线但任一必要 CAN 状态流超时：判定为通信掉线；
   * 4. 物理在线、CAN 正常且 BMS 明确允许放电：判定为正常。
   */
  if (bat1BmsProhibited != 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_BMS_PROHIBIT_DISCHARGE;
  }
  else if (bat1BmsUnknown != 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN;
  }
  else if (bat1PhysOnline == 0U)
  {
    battery1AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
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
    battery1AlarmStatus = BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN;
  }

  /* 电池 2 报警状态判定（同上） */
  if (bat2BmsProhibited != 0U)
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_BMS_PROHIBIT_DISCHARGE;
  }
  else if (bat2BmsUnknown != 0U)
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN;
  }
  else if (bat2PhysOnline == 0U)
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  }
  else if (bat2CanAlive == 0U)
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_CAN_COMM_LOST;
  }
  else if (bat2BmsAllowed != 0U)
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_NORMAL;
  }
  else
  {
    battery2AlarmStatus = BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN;
  }

  /*
   * 安全底线检查：若两块电池物理电压均低于阈值，主动力来源彻底丧失，
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
   * 1. 若 BMS 明确禁放、物理拔出、或状态异常：立即禁止放电 (bat1Enable = 0)；
   * 2. 若 CAN 正常且 BMS 明确允许放电 (bat1CanReady != 0)：在物理在位时使能放电 (bat1Enable = bat1PhysOnline)；
   * 3. 若 CAN 通信掉线但物理在线且已处于放电态：保持放电不断动力 (bat1Enable = 1)；
   * 4. 其余情况均不使能。
   */
  if (battery1AlarmStatus == BATTERY_ALARM_STATUS_BMS_PROHIBIT_DISCHARGE)
  {
    bat1Enable = 0U;
  }
  else if (battery1AlarmStatus == BATTERY_ALARM_STATUS_REMOVED)
  {
    bat1Enable = 0U;
  }
  else if (battery1AlarmStatus == BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN)
  {
    bat1Enable = 0U;
  }
  else if (bat1CanReady != 0U)
  {
    bat1Enable = bat1PhysOnline;
  }
  else if ((bat1CanAlive == 0U) &&
           (bat1PhysOnline != 0U) &&
           (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE))
  {
    bat1Enable = 1U;
  }
  else
  {
    bat1Enable = 0U;
  }

  /* 决策电池 2 本地放电路径使能（同上） */
  if (battery2AlarmStatus == BATTERY_ALARM_STATUS_BMS_PROHIBIT_DISCHARGE)
  {
    bat2Enable = 0U;
  }
  else if (battery2AlarmStatus == BATTERY_ALARM_STATUS_REMOVED)
  {
    bat2Enable = 0U;
  }
  else if (battery2AlarmStatus == BATTERY_ALARM_STATUS_BMS_STATE_UNKNOWN)
  {
    bat2Enable = 0U;
  }
  else if (bat2CanReady != 0U)
  {
    bat2Enable = bat2PhysOnline;
  }
  else if ((bat2CanAlive == 0U) &&
           (bat2PhysOnline != 0U) &&
           (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE))
  {
    bat2Enable = 1U;
  }
  else
  {
    bat2Enable = 0U;
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
  chargeReplyBatteryIndex = 0U;
  chargeReplyLastTxTick = HAL_GetTick();
  Power_AllMosOff();
}

void Power_UpdateVbusPowerRelease(void)
{
  float currentVbus = ADC_GetVbusVoltage();

  /*
   * 反电势吸收保护控制规则：
   * 仅当 VBUS 电压超过 83V 时，才会打开对应处于放电态电池的泄放 MOS；
   * 当 VBUS 电压回落到 82V（1V 滞回防抖）或对应电池退出放电态时，关闭对应泄放 MOS。
   */

  /* 电池1 对应泄放回路 1 (PB12) */
  if (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE)
  {
    if (currentVbus >= POWER_VBUS_RELEASE_OPEN_VOLTAGE)
    {
      HAL_GPIO_WritePin(BACK_EMF_ABSORB_1_GPIO_Port, BACK_EMF_ABSORB_1_Pin, POWER_SWITCH_ON);
    }
    else if (currentVbus <= POWER_VBUS_RELEASE_CLOSE_VOLTAGE)
    {
      HAL_GPIO_WritePin(BACK_EMF_ABSORB_1_GPIO_Port, BACK_EMF_ABSORB_1_Pin, POWER_SWITCH_OFF);
    }
  }
  else
  {
    HAL_GPIO_WritePin(BACK_EMF_ABSORB_1_GPIO_Port, BACK_EMF_ABSORB_1_Pin, POWER_SWITCH_OFF);
  }

  /* 电池2 对应泄放回路 2 (PB11) */
  if (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE)
  {
    if (currentVbus >= POWER_VBUS_RELEASE_OPEN_VOLTAGE)
    {
      HAL_GPIO_WritePin(BACK_EMF_ABSORB_2_GPIO_Port, BACK_EMF_ABSORB_2_Pin, POWER_SWITCH_ON);
    }
    else if (currentVbus <= POWER_VBUS_RELEASE_CLOSE_VOLTAGE)
    {
      HAL_GPIO_WritePin(BACK_EMF_ABSORB_2_GPIO_Port, BACK_EMF_ABSORB_2_Pin, POWER_SWITCH_OFF);
    }
  }
  else
  {
    HAL_GPIO_WritePin(BACK_EMF_ABSORB_2_GPIO_Port, BACK_EMF_ABSORB_2_Pin, POWER_SWITCH_OFF);
  }
}

void Power_ModeTask(void)
{
  uint8_t battery1Present;
  uint8_t battery2Present;

  if (dischargeModeEnabled == 0U)
  {
    return;
  }

  if (powerWorkMode == POWER_WORK_MODE_CHARGE)
  {
    battery1Present = Power_IsBatteryCanPresent(1U);
    battery2Present = Power_IsBatteryCanPresent(2U);
    Power_UpdateChargeMode(battery1Present, battery2Present);
    Power_UpdatePeripheralPower(battery1Present, battery2Present);
    Power_UpdateVbusPowerRelease();
    return;
  }

  Power_DischargeModeTask();
  Power_UpdateVbusPowerRelease();
}

static void Power_MaintainChargeModeVbusPower(uint8_t battery1Present, uint8_t battery2Present)
{
  /*
   * 充电模式下仅维持单路在位电池的主放电 MOS 导通，为 VBUS 供电以驱动 DCDC U52 保障小脑（RK3588）稳定运行。
   * 严禁双路放电 MOS 同时导通，杜绝两块电池在 VBUS 放电母线上硬并联产生跨母线倒灌短路环流！
   * 维持供电的在位电池保持 DISCHARGE 状态（保小脑运行不断电）；
   * 未供电的电池设为 OFF，退出充电后并网放电时需经过标准预放电软启动流程。
   */
  if ((battery1Present != 0U) && (battery2Present != 0U))
  {
    if (Power_GetBalancedVoltageDiff() >= 0.0f)
    {
      HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
      battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
      HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
      battery2Control.state = POWER_BATTERY_STATE_OFF;
    }
    else
    {
      HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
      battery1Control.state = POWER_BATTERY_STATE_OFF;
      HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
      battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
    }
  }
  else if (battery1Present != 0U)
  {
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    battery2Control.state = POWER_BATTERY_STATE_OFF;
  }
  else if (battery2Present != 0U)
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
  if (powerWorkMode == POWER_WORK_MODE_CHARGE)
  {
    return HAL_OK;
  }

  /* 1. 切入充电模式：强制切断回充 MOS 与预放电 MOS，杜绝跨母线倒灌环流并消除预充支路损耗 */
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  powerPreDischargeBatteryIndex = 0U;

  /* 2. 维持单路高电压在位电池主放电 MOS 导通以保 VBUS 供电，杜绝双路并联倒灌 */
  Power_MaintainChargeModeVbusPower(Power_IsBatteryCanPresent(1U), Power_IsBatteryCanPresent(2U));

  dischargeModeEnabled = 1U;
  powerWorkMode = POWER_WORK_MODE_CHARGE;
  rechargeCurrentOnlyMode = 0U;
  chargeCurrentOnlyMode = 0U;
  chargeReplyBatteryIndex = 0U;
  chargeReplyLastTxTick = HAL_GetTick() - POWER_CHARGE_REPLY_PERIOD_MS;

  Power_ModeTask();
  return HAL_OK;
}

void Power_ExitChargeMode(void)
{
  if (powerWorkMode == POWER_WORK_MODE_CHARGE)
  {
    HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port, BAT_CHARGE_MOS_Pin, POWER_SWITCH_OFF);

    powerWorkMode = POWER_WORK_MODE_DISCHARGE;
    rechargeCurrentOnlyMode = 0U;
    chargeCurrentOnlyMode = 0U;
    chargeReplyBatteryIndex = 0U;
    chargeReplyLastTxTick = HAL_GetTick();

    Power_ModeTask();
  }
}

static uint8_t Power_IsBatteryCanPresent(uint8_t batteryIndex)
{
  uint32_t now = HAL_GetTick();

  if (batteryIndex == 1U)
  {
    return ((battery1_can_rx_count != 0U) &&
            ((now - battery1_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
  }

  return ((battery2_can_rx_count != 0U) &&
          ((now - battery2_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
}

static void Power_SetChargeMos(uint8_t mask)
{
  HAL_GPIO_WritePin(BAT_CHARGE_MOS_GPIO_Port,
                    BAT_CHARGE_MOS_Pin,
                    (mask != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
}

static void Power_UpdateChargeMode(uint8_t battery1Present, uint8_t battery2Present)
{
  float battery1Voltage;
  float battery2Voltage;
  float voltageDiff;
  uint8_t chargeMask = 0U;
  uint8_t replyBatteryIndex = 0U;
  uint32_t now = HAL_GetTick();

  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);

  /* 维持单路在位电池的主放电 MOS 导通以保 VBUS 供电，杜绝双路同时导通 */
  Power_MaintainChargeModeVbusPower(battery1Present, battery2Present);

  if ((battery1Present != 0U) && (battery2Present != 0U))
  {
    battery1Voltage = Power_GetBatteryCanVoltage(1U);
    battery2Voltage = Power_GetBatteryCanVoltage(2U);
    voltageDiff = battery1Voltage - battery2Voltage;

    if (chargeCurrentOnlyMode == 0U)
    {
      if (voltageDiff > POWER_CHARGE_BALANCE_DIFF)
      {
        chargeMask = 0x02U;
        replyBatteryIndex = 2U;
      }
      else if (voltageDiff < -POWER_CHARGE_BALANCE_DIFF)
      {
        chargeMask = 0x01U;
        replyBatteryIndex = 1U;
      }
      else
      {
        chargeCurrentOnlyMode = 1U;
      }
    }

    if (chargeCurrentOnlyMode != 0U)
    {
      chargeMask = 0x03U;
      replyBatteryIndex = (battery1Voltage <= battery2Voltage) ? 1U : 2U;
    }
  }
  else if (battery1Present != 0U)
  {
    chargeCurrentOnlyMode = 0U;
    chargeMask = 0x01U;
    replyBatteryIndex = 1U;
  }
  else if (battery2Present != 0U)
  {
    chargeCurrentOnlyMode = 0U;
    chargeMask = 0x02U;
    replyBatteryIndex = 2U;
  }
  else
  {
    chargeCurrentOnlyMode = 0U;
  }

  Power_SetChargeMos(chargeMask);

  if ((replyBatteryIndex != 0U) &&
      ((replyBatteryIndex != chargeReplyBatteryIndex) ||
       ((now - chargeReplyLastTxTick) >= POWER_CHARGE_REPLY_PERIOD_MS)))
  {
    chargeReplyLastTxTick = now;
    if (FDCAN_SendChargeReplyToCan2(replyBatteryIndex) == HAL_OK)
    {
      chargeReplyBatteryIndex = replyBatteryIndex;
    }
  }

  if (chargeMask == 0U)
  {
    chargeCurrentOnlyMode = 0U;
    chargeReplyBatteryIndex = 0U;
    chargeReplyLastTxTick = now;
  }
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
  uint8_t canAlive;

  if ((batteryIndex < 1U) || (batteryIndex > 2U))
  {
    return 0U;
  }

  canAlive = Power_IsBatteryCanAlive(batteryIndex);

  /*
   * 电池物理在位判定：
   * 新硬件取消了 PA6/PA7 单电池模拟分压采样，无法直接通过板载 ADC 测量电池端子开路电压。
   * 1. 只要 CAN 保持在线通信，说明电池物理插入且处于开机通信状态，判定为在位 (1)；
   * 2. 若 CAN 掉线，但该电池当前已经处于放电状态 (POWER_BATTERY_STATE_DISCHARGE)：
   *    说明电池物理连接依然存在且正在为主机供电。
   *    此时判定为物理在位 (1)，从而让 Power_DischargeModeTask 准确进入：
   *    batteryAlarmStatus = BATTERY_ALARM_STATUS_CAN_COMM_LOST (0x01)，保持放电不断动力，
   *    并通过 FDCAN3 上报 0x01 给小脑让机器狗受控阻尼下蹲，杜绝粗暴断电摔狗；
   * 3. 其它状态下（未启动放电）CAN 超时则判定为不在位 (0)。
   */
  if (canAlive != 0U)
  {
    return 1U;
  }

  if (((batteryIndex == 1U) ? battery1Control.state : battery2Control.state) == POWER_BATTERY_STATE_DISCHARGE)
  {
    return 1U;
  }

  return 0U;
}

static uint8_t Power_IsBatteryCanAlive(uint8_t batteryIndex)
{
  uint32_t now = HAL_GetTick();

  if (batteryIndex == 1U)
  {
    return ((battery1_can_rx_count != 0U) &&
            (battery1_can_mos_rx_count != 0U) &&
            ((now - battery1_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
            ((now - battery1_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
  }

  return ((battery2_can_rx_count != 0U) &&
            (battery2_can_mos_rx_count != 0U) &&
            ((now - battery2_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
            ((now - battery2_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
}

static uint8_t Power_IsBatteryBmsDischargeAllowed(uint8_t batteryIndex)
{
  BmsDischargeMosState_t state = (batteryIndex == 1U) ? battery1_bms_discharge_state : battery2_bms_discharge_state;
  return (state == BMS_DISCHARGE_MOS_ALLOWED) ? 1U : 0U;
}

static uint8_t Power_IsBatteryBmsDischargeProhibited(uint8_t batteryIndex)
{
  BmsDischargeMosState_t state = (batteryIndex == 1U) ? battery1_bms_discharge_state : battery2_bms_discharge_state;
  return ((Power_IsBatteryBmsMosFresh(batteryIndex) != 0U) &&
          (state == BMS_DISCHARGE_MOS_PROHIBITED)) ? 1U : 0U;
}

static uint8_t Power_IsBatteryBmsDischargeUnknown(uint8_t batteryIndex)
{
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

static uint8_t Power_IsBatteryCanReady(uint8_t batteryIndex)
{
  return ((Power_IsBatteryCanAlive(batteryIndex) != 0U) &&
          (Power_IsBatteryBmsDischargeAllowed(batteryIndex) != 0U)) ? 1U : 0U;
}

static float Power_GetBatteryCanVoltage(uint8_t batteryIndex)
{
  return (batteryIndex == 1U) ? battery1_can_sum_voltage : battery2_can_sum_voltage;
}

__attribute__((unused))
static float Power_GetBatteryEffectiveVoltage(uint8_t batteryIndex)
{
  return Power_GetBatteryCanVoltage(batteryIndex);
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

  if ((battery1Ready != 0U) && (battery2Ready != 0U))
  {
    if (rechargeCurrentOnlyMode == 0U)
    {
      voltageDiff = Power_GetBalancedVoltageDiff();

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
  powerPreDischargeBatteryIndex = 0U;
}

static void Power_FinishBattery2PreDischarge(void)
{
  Power_EnableBattery2DischargePath();
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  powerPreDischargeBatteryIndex = 0U;
}

static void Power_EnableBattery1DischargePath(void)
{
  Power_DisableLowerRechargeMosBeforeNewDischarge(1U);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

static void Power_EnableBattery2DischargePath(void)
{
  Power_DisableLowerRechargeMosBeforeNewDischarge(2U);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

static void Power_DisableLowerRechargeMosBeforeNewDischarge(uint8_t newBatteryIndex)
{
  float voltageDiff = Power_GetBalancedVoltageDiff();

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
    if (powerPreDischargeBatteryIndex == 0U)
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
    if (powerPreDischargeBatteryIndex == 0U)
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
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);

  if (powerPreDischargeBatteryIndex == 1U)
  {
    powerPreDischargeBatteryIndex = 0U;
  }

  battery1Control.state = POWER_BATTERY_STATE_OFF;
}

static void Power_DisableBattery2Path(void)
{
  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);

  if (powerPreDischargeBatteryIndex == 2U)
  {
    powerPreDischargeBatteryIndex = 0U;
  }

  battery2Control.state = POWER_BATTERY_STATE_OFF;
}

static void Power_UpdateRechargeMos(uint8_t battery1Ready, uint8_t battery2Ready)
{
  Power_SetRechargeMos(Power_GetRechargeMaskByCanVoltage(battery1Ready, battery2Ready));
}

void Power_SetDcdc12V(uint8_t enable)
{
  HAL_GPIO_WritePin(DCDC_EN_12V_GPIO_Port, DCDC_EN_12V_Pin, (enable != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Power_SetDcdc24V(uint8_t enable)
{
  HAL_GPIO_WritePin(DCDC_EN_24V_GPIO_Port, DCDC_EN_24V_Pin, (enable != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Power_SetRgbLed(uint8_t r, uint8_t g, uint8_t b)
{
  HAL_GPIO_WritePin(RGB_LED_R_GPIO_Port, RGB_LED_R_Pin, (r != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RGB_LED_G_GPIO_Port, RGB_LED_G_Pin, (g != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RGB_LED_B_GPIO_Port, RGB_LED_B_Pin, (b != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Power_SetBuzzer(uint8_t on)
{
  HAL_GPIO_WritePin(BUZZER_ALARM_GPIO_Port, BUZZER_ALARM_Pin, (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Power_UpdateStatusLed(void)
{
  /* 1. 最高优先级：CAN Bus-Off 故障，红灯以 200ms 周期闪烁，蜂鸣器同步鸣响报警 */
  if (FDCAN_IsAnyBusOff() != 0U)
  {
    if (((HAL_GetTick() / POWER_STATUS_LED_BUSOFF_BLINK_MS) % 2U) == 0U)
    {
      Power_SetRgbLed(1U, 0U, 0U); /* 红灯亮 */
      Power_SetBuzzer(1U);         /* 蜂鸣器响 */
    }
    else
    {
      Power_SetRgbLed(0U, 0U, 0U); /* 全灭 */
      Power_SetBuzzer(0U);         /* 蜂鸣器静音 */
    }
    return;
  }

  /* 非 Bus-Off 故障态，确保蜂鸣器关闭 */
  Power_SetBuzzer(0U);

  /* 2. 急停开关触发：红灯常亮报警 */
  if (Power_IsEmergencyStopActive() != 0U)
  {
    Power_SetRgbLed(1U, 0U, 0U);
    return;
  }

  /* 3. 获取双电池有效 SOC（木桶短板原则，取较低者） */
  uint8_t hasSoc1 = FDCAN_IsBatterySocValid(1U);
  uint8_t hasSoc2 = FDCAN_IsBatterySocValid(2U);
  float effectiveSoc = 100.0f;
  uint8_t hasValidSoc = 0U;

  if ((hasSoc1 != 0U) && (hasSoc2 != 0U))
  {
    float soc1 = FDCAN_GetBatterySoc(1U);
    float soc2 = FDCAN_GetBatterySoc(2U);
    effectiveSoc = (soc1 < soc2) ? soc1 : soc2;
    hasValidSoc = 1U;
  }
  else if (hasSoc1 != 0U)
  {
    effectiveSoc = FDCAN_GetBatterySoc(1U);
    hasValidSoc = 1U;
  }
  else if (hasSoc2 != 0U)
  {
    effectiveSoc = FDCAN_GetBatterySoc(2U);
    hasValidSoc = 1U;
  }

  /* 
   * 4. 电源指示灯三级电量显示：
   * - 刚上电未收到有效电量帧：默认绿灯常亮
   * - 电量 > 50%：绿灯常亮
   * - 20% <= 电量 <= 50%：蓝灯常亮
   * - 电量 < 20%：红灯常亮
   */
  if (hasValidSoc == 0U)
  {
    Power_SetRgbLed(0U, 1U, 0U); /* 刚上电默认绿灯常亮 */
  }
  else if (effectiveSoc > POWER_STATUS_LED_SOC_HIGH_THRESHOLD)
  {
    Power_SetRgbLed(0U, 1U, 0U); /* 电量 > 50%：绿灯常亮 */
  }
  else if (effectiveSoc >= POWER_STATUS_LED_SOC_LOW_THRESHOLD)
  {
    Power_SetRgbLed(0U, 0U, 1U); /* 20% <= 电量 <= 50%：蓝灯常亮 */
  }
  else
  {
    Power_SetRgbLed(1U, 0U, 0U); /* 电量 < 20%：红灯常亮 */
  }
}
/* USER CODE END 2 */
