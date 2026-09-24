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
static uint8_t chargeCurrentOnlyMode = 0U;
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
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_9, GPIO_PIN_RESET);

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

  /*Configure GPIO pins : PB10 PB6 PB7 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_9;
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

  bat2_charge_mos_state = bat_charge_mos_state;
  bat2_discharge_mos_state = (HAL_GPIO_ReadPin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_recharge_mos_state = (HAL_GPIO_ReadPin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  bat2_pre_discharge_mos_state = (HAL_GPIO_ReadPin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin) == POWER_SWITCH_ON) ? 1U : 0U;

  peripheral_power_state = (HAL_GPIO_ReadPin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin) == POWER_SWITCH_ON) ? 1U : 0U;
  dcdc_12v_state = (HAL_GPIO_ReadPin(DCDC_EN_12V_GPIO_Port, DCDC_EN_12V_Pin) == POWER_DCDC_ON) ? 1U : 0U;
  dcdc_24v_state = (HAL_GPIO_ReadPin(DCDC_EN_24V_GPIO_Port, DCDC_EN_24V_Pin) == POWER_DCDC_ON) ? 1U : 0U;
  back_emf_absorb_1_state = 0U; /* PB12 未使用 */
  back_emf_absorb_2_state = (__HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_4) > 0U) ? 1U : 0U;
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

  /* 反电势吸收泄放关闭：PB11 TIM2_CH4 占空比设为 0；PB12 保持关断 */
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, 0U);
  HAL_GPIO_WritePin(BACK_EMF_ABSORB_1_GPIO_Port, BACK_EMF_ABSORB_1_Pin, POWER_SWITCH_OFF);
  back_emf_absorb_1_state = 0U;
  back_emf_absorb_2_state = 0U;
  back_emf_absorb_state = 0U;
  backEmfAbsorbReleased = 0U;

  powerPreDischargeBatteryIndex = 0U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
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
   * 1. 若 BMS 明确禁放或物理拔出：立即禁止放电 (bat1Enable = 0)；
   * 2. 若 BMS 状态未知/无效：若物理在线且已处于放电态，保持动力不断电 (bat1Enable = 1) 并上报报警，
   *    避免因偶发单帧干扰导致断电摔狗；若处于未就绪态则严把准入 (bat1Enable = 0)；
   * 3. 若 CAN 正常且 BMS 明确允许放电 (bat1CanReady != 0)：在物理在位时使能放电 (bat1Enable = bat1PhysOnline)；
   * 4. 若 CAN 通信掉线但物理在线且已处于放电态：保持放电不断动力 (bat1Enable = 1)；
   * 5. 其余情况均不使能。
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
    if ((bat1PhysOnline != 0U) &&
        (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE))
    {
      bat1Enable = 1U;
    }
    else
    {
      bat1Enable = 0U;
    }
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
    if ((bat2PhysOnline != 0U) &&
        (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE))
    {
      bat2Enable = 1U;
    }
    else
    {
      bat2Enable = 0U;
    }
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
  uint8_t prevAbsorbState = back_emf_absorb_state;

  /*
   * 反电势吸收保护控制规则 (PB11 TIM2_CH4 PWM 50% 斩波泄放)：
   * 1. 仅当系统处于放电工作模式（任一电池处于主放电态 POWER_BATTERY_STATE_DISCHARGE）时才允许泄放；
   * 2. 当 VBUS 电压超过 85.0V (POWER_VBUS_RELEASE_OPEN_VOLTAGE) 时，
   *    通过 PB11 (TIM2 通道 4) 输出 50% 占空比 PWM 进行控流泄放；
   * 3. 当 VBUS 电压回落至 84.0V (POWER_VBUS_RELEASE_CLOSE_VOLTAGE，1V 滞回防抖)
   *    或退出放电模式时，将占空比置 0 关断泄放；
   * 4. PB12 泄放回路未使用，保持关断。
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
      __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, POWER_VBUS_RELEASE_PWM_DUTY);
      back_emf_absorb_2_state = 1U;
    }
    else if (currentVbus <= POWER_VBUS_RELEASE_CLOSE_VOLTAGE)
    {
      __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, 0U);
      back_emf_absorb_2_state = 0U;
    }
    /* 处于 84.0V ~ 85.0V 滞回区间时保持当前 PWM 输出状态 */
  }
  else
  {
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, 0U);
    back_emf_absorb_2_state = 0U;
  }

  back_emf_absorb_1_state = 0U;
  back_emf_absorb_state = back_emf_absorb_2_state;

  /* 泄放状态跳变时立即同步更新声光报警指示 */
  if (prevAbsorbState != back_emf_absorb_state)
  {
    Power_UpdateStatusIndicators();
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
  else if (bat1CanSupply)
  {
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
    battery2Control.state = POWER_BATTERY_STATE_OFF;
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
    if (FDCAN_SendChargeReplyToCharger(replyBatteryIndex) == HAL_OK)
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
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, 0U);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_Delay(POWER_PRE_DISCHARGE_DELAY_MS);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_Delay(POWER_PRE_DISCHARGE_OVERLAP_MS);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  Power_SetDcdc12V(1U);
  Power_SetDcdc24V(1U);
}//测试函数

/* [急停功能已停用，硬件未连接，PD2已在CubeMX关闭]
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
*/

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
  HAL_GPIO_WritePin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
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
  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
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
  HAL_GPIO_WritePin(DCDC_EN_12V_GPIO_Port, DCDC_EN_12V_Pin, (enable != 0U) ? POWER_DCDC_ON : POWER_DCDC_OFF);
}

void Power_SetDcdc24V(uint8_t enable)
{
  HAL_GPIO_WritePin(DCDC_EN_24V_GPIO_Port, DCDC_EN_24V_Pin, (enable != 0U) ? POWER_DCDC_ON : POWER_DCDC_OFF);
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
static uint8_t physicalB = 0U;
static uint8_t physicalBuzzer = 0U;

uint8_t Power_GetPhysicalR(void) { return physicalR; }
uint8_t Power_GetPhysicalG(void) { return physicalG; }
uint8_t Power_GetPhysicalB(void) { return physicalB; }
uint8_t Power_GetPhysicalBuzzer(void) { return physicalBuzzer; }

void Power_UpdateStatusIndicators(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t outR = 0U;
  uint8_t outG = 0U;
  uint8_t outB = 0U;
  uint8_t outBuzzer = 0U;

  /* 1. 绝对最高安全优先级：反电势吸收泄放激活（VBUS >= 85V），强制蜂鸣器常鸣 + 红灯常亮 */
  if (back_emf_absorb_state != 0U)
  {
    outR = 1U;
    outG = 0U;
    outB = 0U;
    outBuzzer = 1U;
  }
  /* 2. 次高优先级：CAN Bus-Off 故障，红灯以 200ms 周期闪烁，蜂鸣器静音关断 */
  else if (FDCAN_IsAnyBusOff() != 0U)
  {
    if (((now / POWER_STATUS_LED_BUSOFF_BLINK_MS) % 2U) == 0U)
    {
      outR = 1U;
    }
    outG = 0U;
    outB = 0U;
    outBuzzer = 0U;
  }
  /* 2. 次高优先级：急停开关触发，红灯常亮报警，蜂鸣器静音关断 */
  /* [急停功能已停用，硬件未连接]
  else if (Power_IsEmergencyStopActive() != 0U)
  {
    outR = 1U;
    outG = 0U;
    outB = 0U;
    outBuzzer = 0U;
  }
  */
  else
  {
    /* 3. RGB 控制决策 */
    if (FDCAN_IsRkRgbActive() != 0U)
    {
      /* 小脑接管 RGB 控制权 */
      if (rkIndicatorCtrl.rgbMode == FDCAN_INDICATOR_RGB_MODE_BLINK)
      {
        uint32_t hp = (rkIndicatorCtrl.halfPeriodMs > 0U) ? rkIndicatorCtrl.halfPeriodMs : 500U;
        if (((now / hp) % 2U) == 0U)
        {
          outR = rkIndicatorCtrl.r;
          outG = rkIndicatorCtrl.g;
          outB = rkIndicatorCtrl.b;
        }
        else
        {
          outR = 0U;
          outG = 0U;
          outB = 0U;
        }
      }
      else
      {
        /* 常亮模式 */
        outR = rkIndicatorCtrl.r;
        outG = rkIndicatorCtrl.g;
        outB = rkIndicatorCtrl.b;
      }
    }
    else
    {
      /* 本地自动模式：根据电池 SOC 呈现三级电量指示 */
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

      if (hasValidSoc == 0U)
      {
        outG = 1U; /* 刚上电未收到有效电量帧：默认绿灯常亮 */
      }
      else if (effectiveSoc > POWER_STATUS_LED_SOC_HIGH_THRESHOLD)
      {
        outG = 1U; /* 电量 > 50%：绿灯常亮 */
      }
      else if (effectiveSoc >= POWER_STATUS_LED_SOC_LOW_THRESHOLD)
      {
        outB = 1U; /* 20% <= 电量 <= 50%：蓝灯常亮 */
      }
      else
      {
        outR = 1U; /* 电量 < 20%：红灯常亮 */
      }
    }

    /* 4. 蜂鸣器控制决策 */
    if (FDCAN_IsRkBuzzerActive() != 0U)
    {
      /* 小脑接管蜂鸣器 */
      switch (rkIndicatorCtrl.buzzerMode)
      {
        case FDCAN_INDICATOR_BUZZER_MODE_ON:
          outBuzzer = 1U;
          break;

        case FDCAN_INDICATOR_BUZZER_MODE_BEEP_ONCE:
          if (rkIndicatorCtrl.beepActive != 0U)
          {
            if ((now - rkIndicatorCtrl.beepStartTick) < FDCAN_INDICATOR_BEEP_ONCE_DURATION_MS)
            {
              outBuzzer = 1U;
            }
            else
            {
              outBuzzer = 0U;
              rkIndicatorCtrl.beepActive = 0U;
            }
          }
          else
          {
            outBuzzer = 0U;
          }
          break;

        case FDCAN_INDICATOR_BUZZER_MODE_BLINK:
          {
            uint32_t hp = (rkIndicatorCtrl.halfPeriodMs > 0U) ? rkIndicatorCtrl.halfPeriodMs : 500U;
            outBuzzer = (((now / hp) % 2U) == 0U) ? 1U : 0U;
          }
          break;

        case FDCAN_INDICATOR_BUZZER_MODE_OFF:
        default:
          outBuzzer = 0U;
          break;
      }
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
  }

  /* 5. 驱动硬件输出并更新记录 */
  Power_SetRgbLed(outR, outG, outB);
  Power_SetBuzzer(outBuzzer);

  physicalR = outR;
  physicalG = outG;
  physicalB = outB;
  physicalBuzzer = outBuzzer;
}

void Power_UpdateStatusLed(void)
{
  Power_UpdateStatusIndicators();
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
  /* GPIOC: PC4 (24V DCDC, 低使能), PC2 (绿灯, 高亮), PC11 (Bat2主放电, 高开) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, POWER_DCDC_ON); /* 保持 24V DCDC 导通 (Active Low) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);  /* 恢复绿灯指示正常工作态 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, ((batMask & 0x02U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_RESET);

  /* GPIOA: PA7 (12V DCDC, 低使能), PA5 (外设供电, 关) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, POWER_DCDC_ON); /* 保持 12V DCDC 持续不断电 (Active Low) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /* GPIOB: PB6 (Bat1主放电, 高开), 其余关 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, ((batMask & 0x01U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_RESET);

  /* 2. 配置引脚为推挽输出模式 */
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15 | GPIO_PIN_2
                        | GPIO_PIN_4 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
/* USER CODE END 2 */
