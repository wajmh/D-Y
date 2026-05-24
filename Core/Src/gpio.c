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

typedef enum
{
  /* 未接入/已掉线/未启用：该电池包所有 MOS 都应保持关闭 */
  POWER_BATTERY_STATE_OFF = 0,
  /* 已检测到电池包，正在预放电计时：只允许预放电 MOS 打开 */
  POWER_BATTERY_STATE_PRE_DISCHARGE,
  /* 预放电完成：主放电 MOS 已打开，预放电 MOS 已关闭 */
  POWER_BATTERY_STATE_DISCHARGE
} PowerBatteryState_t;

typedef struct
{
  /* 当前电池包的放电流程状态 */
  PowerBatteryState_t state;
} PowerBatteryControl_t;

static PowerBatteryControl_t battery1Control = {POWER_BATTERY_STATE_OFF};
static PowerBatteryControl_t battery2Control = {POWER_BATTERY_STATE_OFF};
/* 放电模式总开关：为 0 时 Power_DischargeModeTask 不会主动控制 MOS */
static uint8_t dischargeModeEnabled = 0U;

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

static uint8_t Power_IsBatteryPresent(float voltage);
static void Power_StartBattery1PreDischarge(void);
static void Power_StartBattery2PreDischarge(void);
static void Power_FinishBattery1PreDischarge(void);
static void Power_FinishBattery2PreDischarge(void);
static void Power_UpdateBattery1Path(uint8_t present);
static void Power_UpdateBattery2Path(uint8_t present);
static void Power_DisableBattery1Path(void);
static void Power_DisableBattery2Path(void);
static void Power_UpdateRechargeMos(uint8_t battery1Present, uint8_t battery2Present, uint8_t battery1Ready, uint8_t battery2Ready);

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
/* 关闭所有 MOS 和公共电源控制输出，用于上电默认状态、无电池在线和模式退出 */
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
  /*
   * 这个函数是保护性全关：
   * 1. 退出放电模式时调用；
   * 2. 没有任何电池在线时调用。
   * 注意：它会同时关闭两个电池包的所有 MOS，也会关闭外设供电和反电动势吸收。
   */
  HAL_GPIO_WritePin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin, POWER_SWITCH_OFF);//充电MOS
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);//放电MOS
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);//回充MOS
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);//预放电MOS

  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);//充电MOS
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);//放电MOS
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);//回充MOS
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);//预放电MOS

  HAL_GPIO_WritePin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin, POWER_SWITCH_OFF);//反电动势吸收
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);//外设供电
}

HAL_StatusTypeDef Power_EnterDischargeMode(void)
{
  /*
   * 进入放电模式只负责“允许状态机工作”，不直接一次性打开主放电 MOS。
   * 原因是上电时可能只有一个电池在线，也可能两个电池都在线，
   * 还可能某个电池稍后才接入；所以具体 MOS 顺序交给 Power_DischargeModeTask 周期处理。
   */
  dischargeModeEnabled = 1U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;

  Power_DischargeModeTask();
  return HAL_OK;
}

void Power_DischargeModeTask(void)
{
  uint8_t battery1Present;//电池包 1 是否在线
  uint8_t battery2Present;//电池包 2 是否在线
  uint8_t needPreDischargeDelay = 0U;

  /*
   * 放电模式周期任务：
   * 主循环需要持续调用它。它会根据 ADC 电压判断电池是否存在，
   * 并把每个电池包独立推进到 OFF -> PRE_DISCHARGE -> DISCHARGE。
   */
  if (dischargeModeEnabled == 0U)
  {
    return;
  }//这句是保护开关。
//如果 dischargeModeEnabled 等于 0，说明当前没有进入放电模式，那这个函数就直接退出，后面的 MOS 控制全部不执行

  battery1Present = Power_IsBatteryPresent(ADC2_GetBattery1Voltage());//判断电池1大于50V认为电池在线
  battery2Present = Power_IsBatteryPresent(ADC2_GetBattery2Voltage());//判断电池2大于50V认为电池在线
  /*
   * 只要至少有一个电池包在线，就打开公共外设供电。
   * PB11 反向电动势吸收目前先不参与控制，保持关闭。
   * 如果两个电池包都不在线，就关闭所有 MOS/公共输出，保持安全空闲状态。
   */
  if ((battery1Present != 0U) || (battery2Present != 0U))
  {
    HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin, POWER_SWITCH_OFF);
  }
  else
  {
    Power_AllMosOff();
  }

  Power_UpdateBattery1Path(battery1Present);
  Power_UpdateBattery2Path(battery2Present);

  /*
   * 如果有新接入的电池进入预放电状态，就直接用 HAL_Delay 等待 1s。
   * 两块电池同时新接入时，上面已经先把两路预放电 MOS 都打开，
   * 所以这里只 delay 一次，然后再一起切到主放电 MOS。
   */
  if ((battery1Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE) ||
      (battery2Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE))
  {
    needPreDischargeDelay = 1U;
  }

  if (needPreDischargeDelay != 0U)
  {
    HAL_Delay(POWER_PRE_DISCHARGE_DELAY_MS);

    if (battery1Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
    {
      Power_FinishBattery1PreDischarge();
    }

    if (battery2Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
    {
      Power_FinishBattery2PreDischarge();
    }
  }

  /*
   * 回充 MOS 依赖“电池已经完成预放电并进入主放电状态”。
   * 这样可以避免某个电池刚插入、还在预放电阶段时，回充 MOS 提前打开。
   */
  Power_UpdateRechargeMos(battery1Present,
                          battery2Present,
                          (battery1Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U,
                          (battery2Control.state == POWER_BATTERY_STATE_DISCHARGE) ? 1U : 0U);
}

void Power_ExitDischargeMode(void)
{
  /* 退出放电时关闭放电相关 MOS、吸收通路和外设供电 */
  dischargeModeEnabled = 0U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery2Control.state = POWER_BATTERY_STATE_OFF;
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);
}

void Power_TestBattery1DischargeSequence(void)
{
  /*
   * 电池包1放电通路测试：
   * 1. 先关闭所有 MOS，保证测试从已知安全状态开始；
   * 2. 打开电池包1预放电 MOS；
   * 3. 延时 1s；
   * 4. 打开电池包1主放电 MOS；
   * 5. 打开电池包1回充 MOS。
   */
  Power_AllMosOff();

  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_Delay(POWER_PRE_DISCHARGE_DELAY_MS);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
}

static uint8_t Power_IsBatteryPresent(float voltage)
{
  /*
   * 电池存在判断只看 ADC 换算电压。
   * 如果后续需要更稳，可以在这里加入滞回或连续多次确认，
   * 防止电压在阈值附近抖动导致 MOS 反复开关。
   */
  return (voltage >= POWER_BATTERY_PRESENT_VOLTAGE) ? 1U : 0U;//如果电压大于等于 POWER_BATTERY_PRESENT_VOLTAGE，认为电池在线，返回 1；否则返回 0。
}

static void Power_StartBattery1PreDischarge(void)
{
  /*
   * 电池包1刚检测到在线：
   * 1. 关闭电池包1主放电 MOS，避免未预充就直接接入；
   * 2. 关闭电池包1回充 MOS，避免预放电阶段形成额外通路；
   * 3. 打开电池包1预放电 MOS；
   * 4. 由 Power_DischargeModeTask 统一 HAL_Delay 1s 后再打开主放电。
   */
  HAL_GPIO_WritePin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

  battery1Control.state = POWER_BATTERY_STATE_PRE_DISCHARGE;
}

static void Power_StartBattery2PreDischarge(void)
{
  /*
   * 电池包2刚检测到在线，处理顺序与电池包1一致。
   * 这样支持运行过程中后插入第二块电池：先预放电 1s，再接入主放电。
   */
  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);

  battery2Control.state = POWER_BATTERY_STATE_PRE_DISCHARGE;
}

static void Power_FinishBattery1PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
//  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

static void Power_FinishBattery2PreDischarge(void)
{
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
//  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  battery2Control.state = POWER_BATTERY_STATE_DISCHARGE;
}

static void Power_UpdateBattery1Path(uint8_t present)
{
  /*
   * 电池包1放电路径状态推进：
   * - present = 0：认为电池包1掉线，立即关闭电池包1相关 MOS；
   * - OFF：检测到重新上电/新接入，进入预放电；
   * - PRE_DISCHARGE：等待 1s，时间到后打开主放电 MOS；
   * - DISCHARGE：保持主放 电MOS 打开，预放电 MOS 关闭。
   */
  if (present == 0U)
  {
    Power_DisableBattery1Path();
    return;
  }

  if (battery1Control.state == POWER_BATTERY_STATE_OFF)
  {
    Power_StartBattery1PreDischarge();
  }
  else if (battery1Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
  {
  //  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  }
  else
  {
    HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  }
}

static void Power_UpdateBattery2Path(uint8_t present)
{
  /*
   * 电池包2放电路径状态推进：
   * 与电池包1完全独立，所以单电池运行时另一路可以保持关闭；
   * 第二块电池后续接入时，也会单独走预放电流程。
   */
  if (present == 0U)
  {
    Power_DisableBattery2Path();
    return;
  }

  if (battery2Control.state == POWER_BATTERY_STATE_OFF)
  {
    Power_StartBattery2PreDischarge();
  }
  else if (battery2Control.state == POWER_BATTERY_STATE_PRE_DISCHARGE)
  {
  //  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  }
  else
  {
    HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  }
}

static void Power_DisableBattery1Path(void)
{
  /*
   * 电池包1掉线处理：
   * 只关闭电池包1自己的充电/放电/回充/预放电 MOS，
   * 不影响电池包2继续放电。
   * 状态重置为 OFF，后面如果电池包1重新上电，会重新走预放电流程。
   */
  HAL_GPIO_WritePin(BAT1_CHARGE_MOS_GPIO_Port, BAT1_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);

  battery1Control.state = POWER_BATTERY_STATE_OFF;
}

static void Power_DisableBattery2Path(void)
{
  /*
   * 电池包2掉线处理：
   * 只关闭电池包2自己的 MOS，并重置状态。
   */
  HAL_GPIO_WritePin(BAT2_CHARGE_MOS_GPIO_Port, BAT2_CHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_DISCHARGE_MOS_GPIO_Port, BAT2_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT2_PRE_DISCHARGE_MOS_GPIO_Port, BAT2_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);

  battery2Control.state = POWER_BATTERY_STATE_OFF;
}

static void Power_UpdateRechargeMos(uint8_t battery1Present, uint8_t battery2Present, uint8_t battery1Ready, uint8_t battery2Ready)
{//电池在线和电池是否在放电
  float battery1Voltage = ADC2_GetBattery1Voltage();
  float battery2Voltage = ADC2_GetBattery2Voltage();
  float voltageDiff;

  (void)battery1Present;
  (void)battery2Present;

  /*
   * 回充 MOS 控制策略：
   * 1. 两个电池都已完成预放电：
   *    - 电池1电压比电池2高超过 1V：只开电池1回充 MOS；
   *    - 电池2电压比电池1高超过 1V：只开电池2回充 MOS；
   *    - 两者压差小于等于 1V：两个回充 MOS 都打开。
   * 2. 只有一个电池已完成预放电：
   *    - 只打开该电池自己的回充 MOS。
   * 3. 没有电池完成预放电：
   *    - 两个回充 MOS 都关闭。
   */
  if ((battery1Ready != 0U) && (battery2Ready != 0U))
  {
    voltageDiff = battery1Voltage - battery2Voltage;

    if (voltageDiff > POWER_RECHARGE_BALANCE_DIFF)
    {
      HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
      HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
    }
    else if (voltageDiff < -POWER_RECHARGE_BALANCE_DIFF)
    {
      HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
      HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    }
    else
    {
      HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
      HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    }
  }
  else if (battery1Ready != 0U)
  {
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  }
  else if (battery2Ready != 0U)
  {
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  }
  else
  {
    HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
    HAL_GPIO_WritePin(BAT2_RECHARGE_MOS_GPIO_Port, BAT2_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  }
}

/* USER CODE END 2 */
