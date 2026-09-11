/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    fdcan.c
  * @brief   This file provides code for the configuration
  *          of the FDCAN instances.
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
#include "fdcan.h"

/* USER CODE BEGIN 0 */
#include "adc.h"
#include "gpio.h"
#include <string.h>

#define FDCAN_BATTERY_STATUS_ID_BASE 0x04028000U
#define FDCAN_BATTERY_STATUS_ID_MASK 0x1FFFFF00U
#define FDCAN_BATTERY_CHARGE_REPLY_STATUS_ID_BASE 0x04028000U
#define FDCAN_BATTERY_CHARGE_REPLY_INFO_ID_BASE   0x04008000U
#define FDCAN_BATTERY_CHARGE_REPLY_TEMP_ID_BASE   0x04088000U
#define FDCAN_BATTERY_MOS_ID_BASE    0x04068000U
#define FDCAN_BATTERY_MOS_ID_MASK    0x1FFFFF00U
#define FDCAN_BATTERY_INFO_ID_BASE   0x04038000U
#define FDCAN_BATTERY_FAULT_ID_BASE  0x040E8000U
#define FDCAN_BATTERY_TEMP_ID_BASE   0x04078000U
#define FDCAN_BATTERY_ID_MASK        0x1FFFFF00U
#define FDCAN_LEG_CURRENT_REPORT_ID  0x04100000U
#define FDCAN_PERIPHERAL_CURRENT_REPORT_ID 0x04200000U
#define FDCAN_ESTOP_REPORT_ID        0x04300000U
#define FDCAN_BATTERY_ALARM_REPORT_ID 0x04400000U
#define FDCAN_CURRENT_REPORT_PERIOD_MS 200U
#define FDCAN_ESTOP_REPORT_PERIOD_MS 50U
#define FDCAN_BATTERY_ALARM_REPORT_PERIOD_MS 200U
#define FDCAN_CURRENT_REPORT_SCALE    100.0f
#define FDCAN_BATTERY_WAKE_PERIOD_MS  2000U

static uint8_t batteryCanStarted = 0U;
static uint32_t batteryCanLastTxTick = 0U;
static uint32_t currentReportLastTxTick = 0U;
static uint32_t estopReportLastTxTick = 0U;
static uint32_t batteryAlarmReportLastTxTick = 0U;

typedef struct
{
  uint8_t valid;
  uint32_t id;
  uint32_t dataLength;
  uint8_t data[8];
} FDCAN_ChargeReplyFrame_t;

static FDCAN_ChargeReplyFrame_t batteryChargeReplyFrames[2][3];
volatile uint32_t battery_can_forward_count = 0U;
volatile uint32_t battery_can_forward_drop_count = 0U;
volatile float battery1_can_sum_voltage = 0.0f;
volatile float battery1_can_current = 0.0f;
volatile uint32_t battery1_can_rx_id = 0U;
volatile uint32_t battery1_can_rx_count = 0U;
volatile uint32_t battery1_can_status_last_rx_tick = 0U;
volatile uint8_t battery1_can_charge_mos_state = 0U;
volatile uint8_t battery1_can_discharge_mos_state = 0U;
volatile BmsDischargeMosState_t battery1_bms_discharge_state = BMS_DISCHARGE_MOS_UNKNOWN;
volatile uint32_t battery1_can_mos_rx_id = 0U;
volatile uint32_t battery1_can_mos_rx_count = 0U;
volatile uint32_t battery1_can_mos_last_rx_tick = 0U;
volatile float battery2_can_sum_voltage = 0.0f;
volatile float battery2_can_current = 0.0f;
volatile uint32_t battery2_can_rx_id = 0U;
volatile uint32_t battery2_can_rx_count = 0U;
volatile uint32_t battery2_can_status_last_rx_tick = 0U;
volatile uint8_t battery2_can_charge_mos_state = 0U;
volatile uint8_t battery2_can_discharge_mos_state = 0U;
volatile BmsDischargeMosState_t battery2_bms_discharge_state = BMS_DISCHARGE_MOS_UNKNOWN;
volatile uint32_t battery2_can_mos_rx_id = 0U;
volatile uint32_t battery2_can_mos_rx_count = 0U;
volatile uint32_t battery2_can_mos_last_rx_tick = 0U;
volatile uint32_t charger_can_rx_count = 0U;
volatile uint32_t charger_can_rx_id = 0U;
volatile uint32_t charger_can_last_rx_tick = 0U;
volatile uint32_t charger_can_rx_interval_ms = 0U;
volatile uint8_t rk_charge_mode_request = 0U;
volatile uint8_t charge_mode_active = 0U;

static void FDCAN_IncrementDebugCounter(volatile uint32_t *counter)
{
  if (*counter < 0xFFFFFFFFU)
  {
    (*counter)++;
  }
}

static int16_t FDCAN_CurrentToCanRaw(float current)
{
  float scaled = current * FDCAN_CURRENT_REPORT_SCALE;

  if (scaled > 32767.0f)
  {
    return 32767;
  }

  if (scaled < -32768.0f)
  {
    return -32768;
  }

  return (int16_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

static void FDCAN_PackInt16LittleEndian(uint8_t data[], uint8_t offset, int16_t value)
{
  uint16_t raw = (uint16_t)value;

  data[offset] = (uint8_t)(raw & 0xFFU);
  data[offset + 1U] = (uint8_t)((raw >> 8) & 0xFFU);
}

static HAL_StatusTypeDef FDCAN_SendCurrentReport(uint32_t identifier, const uint8_t txData[])
{
  FDCAN_TxHeaderTypeDef txHeader;

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    return HAL_BUSY;
  }

  txHeader.Identifier = identifier;
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0U;

  return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan3, &txHeader, (uint8_t *)txData);
}

static void FDCAN_SendCurrentReportsToRk(void)
{
  uint8_t legCurrentData[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  uint8_t peripheralCurrentData[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

  FDCAN_PackInt16LittleEndian(legCurrentData, 0U, FDCAN_CurrentToCanRaw(ADC_GetLeg1Current()));
  FDCAN_PackInt16LittleEndian(legCurrentData, 2U, FDCAN_CurrentToCanRaw(ADC_GetLeg2Current()));
  FDCAN_PackInt16LittleEndian(legCurrentData, 4U, FDCAN_CurrentToCanRaw(ADC_GetLeg3Current()));
  FDCAN_PackInt16LittleEndian(legCurrentData, 6U, FDCAN_CurrentToCanRaw(ADC_GetLeg4Current()));

  FDCAN_PackInt16LittleEndian(peripheralCurrentData,
                              0U,
                              FDCAN_CurrentToCanRaw(ADC_GetPeripheralDischargeCurrent()));

  (void)FDCAN_SendCurrentReport(FDCAN_LEG_CURRENT_REPORT_ID, legCurrentData);
  (void)FDCAN_SendCurrentReport(FDCAN_PERIPHERAL_CURRENT_REPORT_ID, peripheralCurrentData);
}

static void FDCAN_SendEmergencyStopReportToRk(void)
{
  uint8_t estopData[8] = {1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

  (void)FDCAN_SendCurrentReport(FDCAN_ESTOP_REPORT_ID, estopData);
}

HAL_StatusTypeDef FDCAN_SendBatteryAlarmReportToRk(void)
{
  uint8_t alarmData[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

  alarmData[0] = Power_GetBattery1AlarmStatus();
  alarmData[1] = Power_GetBattery2AlarmStatus();
  alarmData[2] = bat1_discharge_mos_state;
  alarmData[3] = bat1_recharge_mos_state;
  alarmData[4] = bat2_discharge_mos_state;
  alarmData[5] = bat2_recharge_mos_state;
  alarmData[6] = bat1_charge_mos_state;
  alarmData[7] = bat2_charge_mos_state;

  return FDCAN_SendCurrentReport(FDCAN_BATTERY_ALARM_REPORT_ID, alarmData);
}

static void FDCAN_SendChargeModeStatusToRk(uint8_t status)
{
  FDCAN_TxHeaderTypeDef txHeader;
  uint8_t statusData[1];

  statusData[0] = status;

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    return;
  }

  txHeader.Identifier = FDCAN_RK_CHARGE_MODE_STATUS_ID;
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_1;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0U;

  (void)HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan3, &txHeader, statusData);
}

static void FDCAN_ConfigBatteryRxFilters(FDCAN_HandleTypeDef *hfdcan)
{
  static const uint32_t filterIdBases[] =
  {
    FDCAN_BATTERY_STATUS_ID_BASE,
    FDCAN_BATTERY_MOS_ID_BASE,
    FDCAN_BATTERY_INFO_ID_BASE,
    FDCAN_BATTERY_FAULT_ID_BASE,
    FDCAN_BATTERY_TEMP_ID_BASE,
    FDCAN_BATTERY_CHARGE_REPLY_INFO_ID_BASE,
    FDCAN_BATTERY_CHARGE_REPLY_TEMP_ID_BASE
  };
  FDCAN_FilterTypeDef filterConfig;
  uint32_t filterIndex;

  filterConfig.IdType = FDCAN_EXTENDED_ID;
  filterConfig.FilterType = FDCAN_FILTER_MASK;
  filterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;

  for (filterIndex = 0U; filterIndex < (sizeof(filterIdBases) / sizeof(filterIdBases[0])); filterIndex++)
  {
    filterConfig.FilterIndex = filterIndex;
    filterConfig.FilterID1 = filterIdBases[filterIndex];
    filterConfig.FilterID2 = FDCAN_BATTERY_ID_MASK;

    if (HAL_FDCAN_ConfigFilter(hfdcan, &filterConfig) != HAL_OK)
    {
      Error_Handler();
    }
  }

  if (hfdcan->Instance == FDCAN2)
  {
    filterConfig.FilterIndex = filterIndex;
    filterConfig.FilterID1 = FDCAN_CHARGE_MODE_CMD_ID;
    filterConfig.FilterID2 = 0x1FFFFFFFU;

    if (HAL_FDCAN_ConfigFilter(hfdcan, &filterConfig) != HAL_OK)
    {
      Error_Handler();
    }
  }
}

static void FDCAN_ConfigRkRxFilters(void)
{
  FDCAN_FilterTypeDef filterConfig;

  filterConfig.IdType = FDCAN_EXTENDED_ID;
  filterConfig.FilterIndex = 0U;
  filterConfig.FilterType = FDCAN_FILTER_MASK;
  filterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filterConfig.FilterID1 = FDCAN_RK_CHARGE_MODE_CMD_ID;
  filterConfig.FilterID2 = 0x1FFFFFFFU;

  if (HAL_FDCAN_ConfigFilter(&hfdcan3, &filterConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

static uint8_t FDCAN_GetChargeReplyFrameSlot(uint32_t rxId, uint8_t *slot)
{
  uint32_t idBase = rxId & FDCAN_BATTERY_ID_MASK;

  if (idBase == FDCAN_BATTERY_CHARGE_REPLY_STATUS_ID_BASE)
  {
    *slot = 0U;
    return 1U;
  }

  if (idBase == FDCAN_BATTERY_CHARGE_REPLY_INFO_ID_BASE)
  {
    *slot = 1U;
    return 1U;
  }

  if (idBase == FDCAN_BATTERY_CHARGE_REPLY_TEMP_ID_BASE)
  {
    *slot = 2U;
    return 1U;
  }

  return 0U;
}

static void FDCAN_CacheChargeReplyFrame(uint8_t batteryIndex,
                                        const FDCAN_RxHeaderTypeDef *rxHeader,
                                        const uint8_t rxData[])
{
  FDCAN_ChargeReplyFrame_t *frame;
  uint8_t slot;

  if ((batteryIndex < 1U) ||
      (batteryIndex > 2U) ||
      (rxHeader->IdType != FDCAN_EXTENDED_ID) ||
      (rxHeader->RxFrameType != FDCAN_DATA_FRAME) ||
      (FDCAN_GetChargeReplyFrameSlot(rxHeader->Identifier, &slot) == 0U))
  {
    return;
  }

  frame = &batteryChargeReplyFrames[batteryIndex - 1U][slot];
  frame->valid = 1U;
  frame->id = rxHeader->Identifier;
  frame->dataLength = rxHeader->DataLength;
  (void)memcpy(frame->data, rxData, sizeof(frame->data));
}

static uint8_t FDCAN_IsBatteryForwardFrame(uint32_t rxId)
{
  uint32_t idBase = rxId & FDCAN_BATTERY_ID_MASK;

  return ((idBase == FDCAN_BATTERY_STATUS_ID_BASE) ||
          (idBase == FDCAN_BATTERY_INFO_ID_BASE) ||
          (idBase == FDCAN_BATTERY_FAULT_ID_BASE) ||
          (idBase == FDCAN_BATTERY_TEMP_ID_BASE)) ? 1U : 0U;
}

static void FDCAN_ForwardBatteryFrameToRk(uint8_t batteryIndex,
                                          const FDCAN_RxHeaderTypeDef *rxHeader,
                                          const uint8_t rxData[])
{
  FDCAN_TxHeaderTypeDef txHeader;

  if ((rxHeader->IdType != FDCAN_EXTENDED_ID) ||
      (FDCAN_IsBatteryForwardFrame(rxHeader->Identifier) == 0U))
  {
    return;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    FDCAN_IncrementDebugCounter(&battery_can_forward_drop_count);
    return;
  }

  txHeader.Identifier = (rxHeader->Identifier & FDCAN_BATTERY_ID_MASK) | ((uint32_t)batteryIndex & 0xFFU);
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = rxHeader->RxFrameType;
  txHeader.DataLength = rxHeader->DataLength;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = batteryIndex;

  if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan3, &txHeader, rxData) == HAL_OK)
  {
    FDCAN_IncrementDebugCounter(&battery_can_forward_count);
  }
  else
  {
    FDCAN_IncrementDebugCounter(&battery_can_forward_drop_count);
  }
}

static void FDCAN_ParseBatteryStatus(uint8_t batteryIndex, uint32_t rxId, const uint8_t rxData[])
{
  uint16_t rawVoltage = ((uint16_t)rxData[0] << 8) | rxData[1];
  uint16_t rawCurrent = ((uint16_t)rxData[2] << 8) | rxData[3];
  float sumVoltage = (float)rawVoltage * 0.1f;
  float current = ((float)rawCurrent - 30000.0f) * 0.1f;

  if (batteryIndex == 1U)
  {
    battery1_can_sum_voltage = sumVoltage;
    battery1_can_current = current;
    battery1_can_rx_id = rxId;
    FDCAN_IncrementDebugCounter(&battery1_can_rx_count);
    battery1_can_status_last_rx_tick = HAL_GetTick();
  }
  else
  {
    battery2_can_sum_voltage = sumVoltage;
    battery2_can_current = current;
    battery2_can_rx_id = rxId;
    FDCAN_IncrementDebugCounter(&battery2_can_rx_count);
    battery2_can_status_last_rx_tick = HAL_GetTick();
  }
}

static void FDCAN_ParseBatteryMosStatus(uint8_t batteryIndex, uint32_t rxId, const uint8_t rxData[])
{
  uint8_t chargeMosState = (rxData[0] != 0U) ? 1U : 0U;
  uint8_t dischargeMosByte = rxData[1];
  uint8_t dischargeMosState = (dischargeMosByte == 0x01U) ? 1U : 0U;
  BmsDischargeMosState_t bmsDischargeState;

  if (dischargeMosByte == 0x01U)
  {
    bmsDischargeState = BMS_DISCHARGE_MOS_ALLOWED;
  }
  else if (dischargeMosByte == 0x00U)
  {
    bmsDischargeState = BMS_DISCHARGE_MOS_PROHIBITED;
  }
  else
  {
    bmsDischargeState = BMS_DISCHARGE_MOS_UNKNOWN;
  }

  if (batteryIndex == 1U)
  {
    battery1_can_charge_mos_state = chargeMosState;
    battery1_can_discharge_mos_state = dischargeMosState;
    battery1_bms_discharge_state = bmsDischargeState;
    battery1_can_mos_rx_id = rxId;
    FDCAN_IncrementDebugCounter(&battery1_can_mos_rx_count);
    battery1_can_mos_last_rx_tick = HAL_GetTick();
  }
  else
  {
    battery2_can_charge_mos_state = chargeMosState;
    battery2_can_discharge_mos_state = dischargeMosState;
    battery2_bms_discharge_state = bmsDischargeState;
    battery2_can_mos_rx_id = rxId;
    FDCAN_IncrementDebugCounter(&battery2_can_mos_rx_count);
    battery2_can_mos_last_rx_tick = HAL_GetTick();
  }
}

static void FDCAN_PollBatteryRx(FDCAN_HandleTypeDef *hfdcan, uint8_t batteryIndex)
{
  FDCAN_RxHeaderTypeDef rxHeader;
  uint8_t rxData[8];

  while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0U)
  {
    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK)
    {
      return;
    }

    if ((hfdcan->Instance == FDCAN2) &&
        (rxHeader.IdType == FDCAN_EXTENDED_ID) &&
        (rxHeader.Identifier == FDCAN_CHARGE_MODE_CMD_ID))
    {
      uint32_t now = HAL_GetTick();
      if (charger_can_last_rx_tick != 0U)
      {
        charger_can_rx_interval_ms = now - charger_can_last_rx_tick;
      }
      charger_can_rx_id = rxHeader.Identifier;
      FDCAN_IncrementDebugCounter(&charger_can_rx_count);
      charger_can_last_rx_tick = now;

      if ((rk_charge_mode_request != 0U) && (charge_mode_active == 0U))
      {
        if (Power_EnterChargeMode() == HAL_OK)
        {
          charge_mode_active = 1U;
          FDCAN_SendChargeModeStatusToRk(FDCAN_CHARGE_MODE_ENTER);
        }
      }
      continue;
    }

    FDCAN_CacheChargeReplyFrame(batteryIndex, &rxHeader, rxData);
    FDCAN_ForwardBatteryFrameToRk(batteryIndex, &rxHeader, rxData);

    if ((rxHeader.IdType == FDCAN_EXTENDED_ID) &&
        ((rxHeader.Identifier & FDCAN_BATTERY_STATUS_ID_MASK) == FDCAN_BATTERY_STATUS_ID_BASE) &&
        (rxHeader.DataLength >= FDCAN_DLC_BYTES_4))
    {
      FDCAN_ParseBatteryStatus(batteryIndex, rxHeader.Identifier, rxData);
    }
    else if ((rxHeader.IdType == FDCAN_EXTENDED_ID) &&
             ((rxHeader.Identifier & FDCAN_BATTERY_MOS_ID_MASK) == FDCAN_BATTERY_MOS_ID_BASE) &&
             (rxHeader.DataLength >= FDCAN_DLC_BYTES_2))
    {
      FDCAN_ParseBatteryMosStatus(batteryIndex, rxHeader.Identifier, rxData);
    }
  }
}

static void FDCAN_HandleRkChargeModeCommand(const FDCAN_RxHeaderTypeDef *rxHeader, const uint8_t rxData[])
{
  if ((rxHeader->IdType != FDCAN_EXTENDED_ID) ||
      (rxHeader->RxFrameType != FDCAN_DATA_FRAME) ||
      (rxHeader->Identifier != FDCAN_RK_CHARGE_MODE_CMD_ID) ||
      (rxHeader->DataLength < FDCAN_DLC_BYTES_1))
  {
    return;
  }

  if (rxData[0] == FDCAN_CHARGE_MODE_ENTER)
  {
    rk_charge_mode_request = 1U;
    return;
  }

  if (rxData[0] == FDCAN_CHARGE_MODE_EXIT)
  {
    rk_charge_mode_request = 0U;
    charge_mode_active = 0U;
    Power_ExitChargeMode();
    FDCAN_SendChargeModeStatusToRk(FDCAN_CHARGE_MODE_EXIT);
  }
}

static void FDCAN_PollRkRx(void)
{
  FDCAN_RxHeaderTypeDef rxHeader;
  uint8_t rxData[8];

  while (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan3, FDCAN_RX_FIFO0) > 0U)
  {
    if (HAL_FDCAN_GetRxMessage(&hfdcan3, FDCAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK)
    {
      return;
    }

    FDCAN_HandleRkChargeModeCommand(&rxHeader, rxData);
  }
}

HAL_StatusTypeDef FDCAN_SendChargeReplyToCan2(uint8_t batteryIndex)
{
  FDCAN_TxHeaderTypeDef txHeader;
  HAL_StatusTypeDef result = HAL_OK;
  uint8_t sentFrameCount = 0U;
  uint8_t slot;

  if ((batteryIndex < 1U) || (batteryIndex > 2U))
  {
    return HAL_ERROR;
  }

  for (slot = 0U; slot < 3U; slot++)
  {
    FDCAN_ChargeReplyFrame_t *frame = &batteryChargeReplyFrames[batteryIndex - 1U][slot];

    if (frame->valid == 0U)
    {
      continue;
    }

    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2) == 0U)
    {
      result = HAL_BUSY;
      break;
    }

    txHeader.Identifier = (frame->id & 0x00FFFFFFU) | 0x05000000U;
    txHeader.IdType = FDCAN_EXTENDED_ID;
    txHeader.TxFrameType = FDCAN_DATA_FRAME;
    txHeader.DataLength = frame->dataLength;
    txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    txHeader.BitRateSwitch = FDCAN_BRS_OFF;
    txHeader.FDFormat = FDCAN_CLASSIC_CAN;
    txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    txHeader.MessageMarker = batteryIndex;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &txHeader, frame->data) == HAL_OK)
    {
      sentFrameCount++;
    }
    else
    {
      result = HAL_ERROR;
    }
  }

  if (sentFrameCount != 0U)
  {
    result = HAL_OK;
  }
  else
  {
    result = HAL_ERROR;
  }

  return result;
}

static HAL_StatusTypeDef FDCAN_SendBatteryWakeFrame(FDCAN_HandleTypeDef *hfdcan, uint8_t marker)
{
  FDCAN_TxHeaderTypeDef txHeader;
  uint8_t txData[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

  if (HAL_FDCAN_GetTxFifoFreeLevel(hfdcan) == 0U)
  {
    return HAL_BUSY;
  }

  txHeader.Identifier = 0x0400FF80U;
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = marker;

  return HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, &txHeader, txData);
}

void FDCAN_BatteryCanStart(void)
{
  if (batteryCanStarted != 0U)
  {
    return;
  }

  FDCAN_ConfigBatteryRxFilters(&hfdcan1);
  FDCAN_ConfigBatteryRxFilters(&hfdcan2);
  FDCAN_ConfigRkRxFilters();

  if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_FDCAN_Start(&hfdcan2) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_FDCAN_Start(&hfdcan3) != HAL_OK)
  {
    Error_Handler();
  }

  batteryCanStarted = 1U;
  batteryCanLastTxTick = HAL_GetTick() - FDCAN_BATTERY_WAKE_PERIOD_MS;
  currentReportLastTxTick = HAL_GetTick();
  estopReportLastTxTick = HAL_GetTick() - FDCAN_ESTOP_REPORT_PERIOD_MS;
  batteryAlarmReportLastTxTick = HAL_GetTick() - FDCAN_BATTERY_ALARM_REPORT_PERIOD_MS;
}//负责初始化接收过滤器并启动 CAN。

void FDCAN_BatteryCanTask(void)
{
  uint32_t now;

  if (batteryCanStarted == 0U)
  {
    return;
  }

  now = HAL_GetTick();

  FDCAN_PollRkRx();
  FDCAN_PollBatteryRx(&hfdcan1, 1U);
  FDCAN_PollBatteryRx(&hfdcan2, 2U);

  /* 充电桩通信超时监测：在充电激活状态下，若超过 FDCAN_CHARGER_CAN_TIMEOUT_MS（当前3000ms）未收到充电桩握手帧（拔枪或充电机断电），自动退出充电模式 */
  now = HAL_GetTick();  /* 在超时判断前刷新 now，消除与 PollBatteryRx 内部更新 charger_can_last_rx_tick 的时序下溢 */
  if (charge_mode_active != 0U)
  {
    uint32_t elapsed = now - charger_can_last_rx_tick;
    if ((elapsed < 0x80000000U) && (elapsed >= FDCAN_CHARGER_CAN_TIMEOUT_MS))
    {
      rk_charge_mode_request = 0U;
      charge_mode_active = 0U;
      Power_ExitChargeMode();
      FDCAN_SendChargeModeStatusToRk(FDCAN_CHARGE_MODE_EXIT);
    }
  }

  if ((Power_IsEmergencyStopActive() != 0U) &&
      ((now - estopReportLastTxTick) >= FDCAN_ESTOP_REPORT_PERIOD_MS))
  {
    estopReportLastTxTick = now;
    FDCAN_SendEmergencyStopReportToRk();
  }

  static uint8_t prevBat1Alarm = BATTERY_ALARM_STATUS_NORMAL;
  static uint8_t prevBat2Alarm = BATTERY_ALARM_STATUS_NORMAL;
  static uint8_t prevBat1DischargeMos = 0U;
  static uint8_t prevBat1RechargeMos = 0U;
  static uint8_t prevBat1ChargeMos = 0U;
  static uint8_t prevBat2DischargeMos = 0U;
  static uint8_t prevBat2RechargeMos = 0U;
  static uint8_t prevBat2ChargeMos = 0U;
  static uint8_t alarmClearBurstRemaining = 0U;

  uint8_t curBat1Alarm = Power_GetBattery1AlarmStatus();
  uint8_t curBat2Alarm = Power_GetBattery2AlarmStatus();
  uint8_t hasAlarm = ((curBat1Alarm != BATTERY_ALARM_STATUS_NORMAL) ||
                      (curBat2Alarm != BATTERY_ALARM_STATUS_NORMAL)) ? 1U : 0U;

  /* 监测本地 MOS 状态是否发生跳变（放电、回充、充电 MOS） */
  uint8_t mosChanged = ((bat1_discharge_mos_state != prevBat1DischargeMos) ||
                        (bat1_recharge_mos_state != prevBat1RechargeMos) ||
                        (bat1_charge_mos_state != prevBat1ChargeMos) ||
                        (bat2_discharge_mos_state != prevBat2DischargeMos) ||
                        (bat2_recharge_mos_state != prevBat2RechargeMos) ||
                        (bat2_charge_mos_state != prevBat2ChargeMos)) ? 1U : 0U;

  /* 检测是否从有报警恢复到全正常 */
  if ((hasAlarm == 0U) &&
      ((prevBat1Alarm != BATTERY_ALARM_STATUS_NORMAL) ||
       (prevBat2Alarm != BATTERY_ALARM_STATUS_NORMAL)))
  {
    alarmClearBurstRemaining = FDCAN_BATTERY_ALARM_CLEAR_BURST_COUNT;
  }

  prevBat1Alarm = curBat1Alarm;
  prevBat2Alarm = curBat2Alarm;
  prevBat1DischargeMos = bat1_discharge_mos_state;
  prevBat1RechargeMos = bat1_recharge_mos_state;
  prevBat1ChargeMos = bat1_charge_mos_state;
  prevBat2DischargeMos = bat2_discharge_mos_state;
  prevBat2RechargeMos = bat2_recharge_mos_state;
  prevBat2ChargeMos = bat2_charge_mos_state;

  if (hasAlarm != 0U)
  {
    if ((now - batteryAlarmReportLastTxTick) >= FDCAN_BATTERY_ALARM_REPORT_PERIOD_MS)
    {
      if (FDCAN_SendBatteryAlarmReportToRk() == HAL_OK)
      {
        batteryAlarmReportLastTxTick = now;
      }
    }
  }
  else if (alarmClearBurstRemaining > 0U)
  {
    if ((now - batteryAlarmReportLastTxTick) >= FDCAN_BATTERY_ALARM_REPORT_PERIOD_MS)
    {
      if (FDCAN_SendBatteryAlarmReportToRk() == HAL_OK)
      {
        batteryAlarmReportLastTxTick = now;
        alarmClearBurstRemaining--;
      }
    }
  }
  else if (mosChanged != 0U)
  {
    /* MOS 状态发生开/关动作时，立即触发发送，无需等待 100ms 计时 */
    if (FDCAN_SendBatteryAlarmReportToRk() == HAL_OK)
    {
      batteryAlarmReportLastTxTick = now;
    }
  }
  else
  {
    if ((now - batteryAlarmReportLastTxTick) >= FDCAN_BATTERY_ALARM_HEARTBEAT_PERIOD_MS)
    {
      if (FDCAN_SendBatteryAlarmReportToRk() == HAL_OK)
      {
        batteryAlarmReportLastTxTick = now;
      }
    }
  }

  if ((now - currentReportLastTxTick) >= FDCAN_CURRENT_REPORT_PERIOD_MS)
  {
    currentReportLastTxTick = now;
    FDCAN_SendCurrentReportsToRk();
  }

  if ((now - batteryCanLastTxTick) < FDCAN_BATTERY_WAKE_PERIOD_MS)
  {
    return;
  }

  batteryCanLastTxTick = now;
  (void)FDCAN_SendBatteryWakeFrame(&hfdcan1, 1U);
  (void)FDCAN_SendBatteryWakeFrame(&hfdcan2, 2U);
}//负责初始化接收过滤器并启动 CAN。

/* USER CODE END 0 */

FDCAN_HandleTypeDef hfdcan1;
FDCAN_HandleTypeDef hfdcan2;
FDCAN_HandleTypeDef hfdcan3;

/* FDCAN1 init function */
void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */

  /* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = ENABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 40;
  hfdcan1.Init.NominalSyncJumpWidth = 1;
  hfdcan1.Init.NominalTimeSeg1 = 13;
  hfdcan1.Init.NominalTimeSeg2 = 3;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.StdFiltersNbr = 1;
  hfdcan1.Init.ExtFiltersNbr = 7;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */

  /* USER CODE END FDCAN1_Init 2 */

}
/* FDCAN2 init function */
void MX_FDCAN2_Init(void)
{

  /* USER CODE BEGIN FDCAN2_Init 0 */

  /* USER CODE END FDCAN2_Init 0 */

  /* USER CODE BEGIN FDCAN2_Init 1 */

  /* USER CODE END FDCAN2_Init 1 */
  hfdcan2.Instance = FDCAN2;
  hfdcan2.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan2.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan2.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan2.Init.AutoRetransmission = ENABLE;
  hfdcan2.Init.TransmitPause = DISABLE;
  hfdcan2.Init.ProtocolException = DISABLE;
  hfdcan2.Init.NominalPrescaler = 40;
  hfdcan2.Init.NominalSyncJumpWidth = 1;
  hfdcan2.Init.NominalTimeSeg1 = 13;
  hfdcan2.Init.NominalTimeSeg2 = 3;
  hfdcan2.Init.DataPrescaler = 1;
  hfdcan2.Init.DataSyncJumpWidth = 1;
  hfdcan2.Init.DataTimeSeg1 = 1;
  hfdcan2.Init.DataTimeSeg2 = 1;
  hfdcan2.Init.StdFiltersNbr = 1;
  hfdcan2.Init.ExtFiltersNbr = 8;
  hfdcan2.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN2_Init 2 */

  /* USER CODE END FDCAN2_Init 2 */

}
/* FDCAN3 init function */
void MX_FDCAN3_Init(void)
{

  /* USER CODE BEGIN FDCAN3_Init 0 */

  /* USER CODE END FDCAN3_Init 0 */

  /* USER CODE BEGIN FDCAN3_Init 1 */

  /* USER CODE END FDCAN3_Init 1 */
  hfdcan3.Instance = FDCAN3;
  hfdcan3.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan3.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan3.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan3.Init.AutoRetransmission = ENABLE;
  hfdcan3.Init.TransmitPause = DISABLE;
  hfdcan3.Init.ProtocolException = DISABLE;
  hfdcan3.Init.NominalPrescaler = 40;
  hfdcan3.Init.NominalSyncJumpWidth = 1;
  hfdcan3.Init.NominalTimeSeg1 = 13;
  hfdcan3.Init.NominalTimeSeg2 = 3;
  hfdcan3.Init.DataPrescaler = 1;
  hfdcan3.Init.DataSyncJumpWidth = 1;
  hfdcan3.Init.DataTimeSeg1 = 1;
  hfdcan3.Init.DataTimeSeg2 = 1;
  hfdcan3.Init.StdFiltersNbr = 1;
  hfdcan3.Init.ExtFiltersNbr = 1;
  hfdcan3.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN3_Init 2 */

  /* USER CODE END FDCAN3_Init 2 */

}

static uint32_t HAL_RCC_FDCAN_CLK_ENABLED=0;

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef* fdcanHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(fdcanHandle->Instance==FDCAN1)
  {
  /* USER CODE BEGIN FDCAN1_MspInit 0 */

  /* USER CODE END FDCAN1_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* FDCAN1 clock enable */
    HAL_RCC_FDCAN_CLK_ENABLED++;
    if(HAL_RCC_FDCAN_CLK_ENABLED==1){
      __HAL_RCC_FDCAN_CLK_ENABLE();
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**FDCAN1 GPIO Configuration
    PA11     ------> FDCAN1_RX
    PA12     ------> FDCAN1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* FDCAN1 interrupt Init */
    HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
    HAL_NVIC_SetPriority(FDCAN1_IT1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(FDCAN1_IT1_IRQn);
  /* USER CODE BEGIN FDCAN1_MspInit 1 */

  /* USER CODE END FDCAN1_MspInit 1 */
  }
  else if(fdcanHandle->Instance==FDCAN2)
  {
  /* USER CODE BEGIN FDCAN2_MspInit 0 */

  /* USER CODE END FDCAN2_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* FDCAN2 clock enable */
    HAL_RCC_FDCAN_CLK_ENABLED++;
    if(HAL_RCC_FDCAN_CLK_ENABLED==1){
      __HAL_RCC_FDCAN_CLK_ENABLE();
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**FDCAN2 GPIO Configuration
    PB13     ------> FDCAN2_TX
    PB5     ------> FDCAN2_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF9_FDCAN2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* FDCAN2 interrupt Init */
    HAL_NVIC_SetPriority(FDCAN2_IT0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(FDCAN2_IT0_IRQn);
    HAL_NVIC_SetPriority(FDCAN2_IT1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(FDCAN2_IT1_IRQn);
  /* USER CODE BEGIN FDCAN2_MspInit 1 */

  /* USER CODE END FDCAN2_MspInit 1 */
  }
  else if(fdcanHandle->Instance==FDCAN3)
  {
  /* USER CODE BEGIN FDCAN3_MspInit 0 */

  /* USER CODE END FDCAN3_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* FDCAN3 clock enable */
    HAL_RCC_FDCAN_CLK_ENABLED++;
    if(HAL_RCC_FDCAN_CLK_ENABLED==1){
      __HAL_RCC_FDCAN_CLK_ENABLE();
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**FDCAN3 GPIO Configuration
    PA8     ------> FDCAN3_RX
    PB4     ------> FDCAN3_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF11_FDCAN3;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF11_FDCAN3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* FDCAN3 interrupt Init */
    HAL_NVIC_SetPriority(FDCAN3_IT0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(FDCAN3_IT0_IRQn);
    HAL_NVIC_SetPriority(FDCAN3_IT1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(FDCAN3_IT1_IRQn);
  /* USER CODE BEGIN FDCAN3_MspInit 1 */

  /* USER CODE END FDCAN3_MspInit 1 */
  }
}

void HAL_FDCAN_MspDeInit(FDCAN_HandleTypeDef* fdcanHandle)
{

  if(fdcanHandle->Instance==FDCAN1)
  {
  /* USER CODE BEGIN FDCAN1_MspDeInit 0 */

  /* USER CODE END FDCAN1_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_RCC_FDCAN_CLK_ENABLED--;
    if(HAL_RCC_FDCAN_CLK_ENABLED==0){
      __HAL_RCC_FDCAN_CLK_DISABLE();
    }

    /**FDCAN1 GPIO Configuration
    PA11     ------> FDCAN1_RX
    PA12     ------> FDCAN1_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11|GPIO_PIN_12);

    /* FDCAN1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
    HAL_NVIC_DisableIRQ(FDCAN1_IT1_IRQn);
  /* USER CODE BEGIN FDCAN1_MspDeInit 1 */

  /* USER CODE END FDCAN1_MspDeInit 1 */
  }
  else if(fdcanHandle->Instance==FDCAN2)
  {
  /* USER CODE BEGIN FDCAN2_MspDeInit 0 */

  /* USER CODE END FDCAN2_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_RCC_FDCAN_CLK_ENABLED--;
    if(HAL_RCC_FDCAN_CLK_ENABLED==0){
      __HAL_RCC_FDCAN_CLK_DISABLE();
    }

    /**FDCAN2 GPIO Configuration
    PB13     ------> FDCAN2_TX
    PB5     ------> FDCAN2_RX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_13|GPIO_PIN_5);

    /* FDCAN2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(FDCAN2_IT0_IRQn);
    HAL_NVIC_DisableIRQ(FDCAN2_IT1_IRQn);
  /* USER CODE BEGIN FDCAN2_MspDeInit 1 */

  /* USER CODE END FDCAN2_MspDeInit 1 */
  }
  else if(fdcanHandle->Instance==FDCAN3)
  {
  /* USER CODE BEGIN FDCAN3_MspDeInit 0 */

  /* USER CODE END FDCAN3_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_RCC_FDCAN_CLK_ENABLED--;
    if(HAL_RCC_FDCAN_CLK_ENABLED==0){
      __HAL_RCC_FDCAN_CLK_DISABLE();
    }

    /**FDCAN3 GPIO Configuration
    PA8     ------> FDCAN3_RX
    PB4     ------> FDCAN3_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_8);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_4);

    /* FDCAN3 interrupt Deinit */
    HAL_NVIC_DisableIRQ(FDCAN3_IT0_IRQn);
    HAL_NVIC_DisableIRQ(FDCAN3_IT1_IRQn);
  /* USER CODE BEGIN FDCAN3_MspDeInit 1 */

  /* USER CODE END FDCAN3_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

