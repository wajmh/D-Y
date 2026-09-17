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

#define FDCAN_BATTERY_STATUS_ID_BASE 0x04028000U
#define FDCAN_BATTERY_STATUS_ID_MASK 0x1FFFFF00U
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

/* Bus-Off 中断驱动恢复配置 */
#define FDCAN_BUSOFF_RECOVERY_DELAY_MS 100U   /* 恢复延迟，等待总线稳定 */
#define FDCAN_BUSOFF_RECOVERY_TIMEOUT_MS 10U  /* 硬件操作超时（CCCR.INIT 进入/退出响应） */

typedef enum
{
  FDCAN_BUSOFF_STATE_IDLE = 0U,           /* 正常工作状态，无 Bus-Off */
  FDCAN_BUSOFF_STATE_PENDING = 1U,        /* 检测到 Bus-Off，等待总线稳定（100ms），优先检查硬件是否已恢复 */
  FDCAN_BUSOFF_STATE_REQUEST_INIT = 2U,   /* 请求进入配置模式（CCCR.INIT=1），非阻塞等待确认 */
  FDCAN_BUSOFF_STATE_START_SYNC = 3U,     /* 已取消待发请求，请求退出配置模式（CCCR.INIT=0），非阻塞等待确认 */
  FDCAN_BUSOFF_STATE_RECOVERING = 4U      /* 已退出配置模式，持续非阻塞轮询 PSR.BO == 0（等待 129 × 11 bit 隐性位同步完成） */
} FDCAN_BusOffState_t;

/* FDCAN1 恢复标志、代际序号、状态与统计 */
static volatile uint8_t fdcan1_busoff_flag = 0U;
static volatile uint32_t fdcan1_busoff_generation = 0U;
static volatile FDCAN_BusOffState_t fdcan1_busoff_state = FDCAN_BUSOFF_STATE_IDLE;
static volatile uint32_t fdcan1BusoffDetectTick = 0U;
static volatile uint32_t fdcan1StateTick = 0U;
static volatile uint32_t fdcan1SyncStartTick = 0U;
volatile uint32_t fdcan1_busoff_recovery_count = 0U;
volatile uint32_t fdcan1_busoff_recovery_fail_count = 0U;
volatile uint32_t fdcan1_last_psr = 0U;
volatile uint32_t fdcan1_last_ecr = 0U;

/* FDCAN2 恢复标志、代际序号、状态与统计 */
static volatile uint8_t fdcan2_busoff_flag = 0U;
static volatile uint32_t fdcan2_busoff_generation = 0U;
static volatile FDCAN_BusOffState_t fdcan2_busoff_state = FDCAN_BUSOFF_STATE_IDLE;
static volatile uint32_t fdcan2BusoffDetectTick = 0U;
static volatile uint32_t fdcan2StateTick = 0U;
static volatile uint32_t fdcan2SyncStartTick = 0U;
volatile uint32_t fdcan2_busoff_recovery_count = 0U;
volatile uint32_t fdcan2_busoff_recovery_fail_count = 0U;
volatile uint32_t fdcan2_last_psr = 0U;
volatile uint32_t fdcan2_last_ecr = 0U;

/* FDCAN3 恢复标志、代际序号、状态与统计 */
static volatile uint8_t fdcan3_busoff_flag = 0U;
static volatile uint32_t fdcan3_busoff_generation = 0U;
static volatile FDCAN_BusOffState_t fdcan3_busoff_state = FDCAN_BUSOFF_STATE_IDLE;
static volatile uint32_t fdcan3BusoffDetectTick = 0U;
static volatile uint32_t fdcan3StateTick = 0U;
static volatile uint32_t fdcan3SyncStartTick = 0U;
volatile uint32_t fdcan3_busoff_recovery_count = 0U;
volatile uint32_t fdcan3_busoff_recovery_fail_count = 0U;
volatile uint32_t fdcan3_last_psr = 0U;
volatile uint32_t fdcan3_last_ecr = 0U;

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
  HAL_StatusTypeDef status;
  uint32_t primask;

  /* 快速无锁预检 */
  if (fdcan3_busoff_flag != 0U)
  {
    return HAL_BUSY;
  }

  /* 临界区保护：原子检查标志与入队，安全恢复原始中断屏蔽状态 */
  primask = __get_PRIMASK();
  __disable_irq();
  if (fdcan3_busoff_flag != 0U)
  {
    __set_PRIMASK(primask);
    return HAL_BUSY;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    __set_PRIMASK(primask);
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

  status = HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan3, &txHeader, (uint8_t *)txData);
  __set_PRIMASK(primask);

  return status;
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

  /* P1: Bus-Off pending / recovering 期间禁止入队 */
  if (fdcan3_busoff_flag != 0U)
  {
    return HAL_BUSY;
  }

  alarmData[0] = Power_GetBattery1AlarmStatus();
  alarmData[1] = Power_GetBattery2AlarmStatus();
  alarmData[2] = bat1_discharge_mos_state;
  alarmData[3] = bat1_recharge_mos_state;
  alarmData[4] = bat2_discharge_mos_state;
  alarmData[5] = bat2_recharge_mos_state;

  return FDCAN_SendCurrentReport(FDCAN_BATTERY_ALARM_REPORT_ID, alarmData);
}

static HAL_StatusTypeDef FDCAN_ConfigBatteryRxFilters(FDCAN_HandleTypeDef *hfdcan)
{
  static const uint32_t filterIdBases[] =
  {
    FDCAN_BATTERY_STATUS_ID_BASE,
    FDCAN_BATTERY_MOS_ID_BASE,
    FDCAN_BATTERY_INFO_ID_BASE,
    FDCAN_BATTERY_FAULT_ID_BASE,
    FDCAN_BATTERY_TEMP_ID_BASE
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
      return HAL_ERROR;
    }
  }

  return HAL_OK;
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
  uint32_t primask;

  if ((rxHeader == NULL) || (rxData == NULL) || (batteryIndex == 0U) ||
      (FDCAN_IsBatteryForwardFrame(rxHeader->Identifier) == 0U))
  {
    return;
  }

  /* 快速无锁预检 */
  if (fdcan3_busoff_flag != 0U)
  {
    FDCAN_IncrementDebugCounter(&battery_can_forward_drop_count);
    return;
  }

  /* 临界区保护：原子检查标志与入队，安全恢复原始中断屏蔽状态 */
  primask = __get_PRIMASK();
  __disable_irq();
  if (fdcan3_busoff_flag != 0U)
  {
    __set_PRIMASK(primask);
    FDCAN_IncrementDebugCounter(&battery_can_forward_drop_count);
    return;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    __set_PRIMASK(primask);
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
    __set_PRIMASK(primask);
    FDCAN_IncrementDebugCounter(&battery_can_forward_count);
  }
  else
  {
    __set_PRIMASK(primask);
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

static HAL_StatusTypeDef FDCAN_SendBatteryWakeFrame(FDCAN_HandleTypeDef *hfdcan, uint8_t marker)
{
  FDCAN_TxHeaderTypeDef txHeader;
  uint8_t txData[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  HAL_StatusTypeDef status;
  uint32_t primask;

  if ((hfdcan != &hfdcan1) && (hfdcan != &hfdcan2))
  {
    return HAL_ERROR;
  }

  /* 快速无锁预检 */
  if ((hfdcan == &hfdcan1 && fdcan1_busoff_flag != 0U) ||
      (hfdcan == &hfdcan2 && fdcan2_busoff_flag != 0U))
  {
    return HAL_BUSY;
  }

  /* 临界区保护：原子检查与入队，安全恢复原始中断屏蔽状态 */
  primask = __get_PRIMASK();
  __disable_irq();
  if ((hfdcan == &hfdcan1 && fdcan1_busoff_flag != 0U) ||
      (hfdcan == &hfdcan2 && fdcan2_busoff_flag != 0U))
  {
    __set_PRIMASK(primask);
    return HAL_BUSY;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(hfdcan) == 0U)
  {
    __set_PRIMASK(primask);
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

  status = HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, &txHeader, txData);
  __set_PRIMASK(primask);

  return status;
}

void FDCAN_BatteryCanStart(void)
{
  if (batteryCanStarted != 0U)
  {
    return;
  }

  if (FDCAN_ConfigBatteryRxFilters(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  if (FDCAN_ConfigBatteryRxFilters(&hfdcan2) != HAL_OK)
  {
    Error_Handler();
  }

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

  /* 使能 Bus-Off 中断（三个 FDCAN 都配置） */
  /* 注意：Bus-Off 属于 Error Status 中断组，默认已路由到 Line 0 */
  if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_BUS_OFF, 0U) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_BUS_OFF, 0U) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_FDCAN_ActivateNotification(&hfdcan3, FDCAN_IT_BUS_OFF, 0U) != HAL_OK)
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

  FDCAN_PollBatteryRx(&hfdcan1, 1U);
  FDCAN_PollBatteryRx(&hfdcan2, 2U);

  /* P1: 仅在 FDCAN3 处于正常状态（非 Bus-Off pending/recovering）时才处理并向 RK 上报数据 */
  if (fdcan3_busoff_flag == 0U)
  {
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
    static uint8_t prevBat2DischargeMos = 0U;
    static uint8_t prevBat2RechargeMos = 0U;
    static uint8_t alarmClearBurstRemaining = 0U;

    uint8_t curBat1Alarm = Power_GetBattery1AlarmStatus();
    uint8_t curBat2Alarm = Power_GetBattery2AlarmStatus();
    uint8_t hasAlarm = ((curBat1Alarm != BATTERY_ALARM_STATUS_NORMAL) ||
                        (curBat2Alarm != BATTERY_ALARM_STATUS_NORMAL)) ? 1U : 0U;

    /* 监测本地 MOS 状态是否发生跳变 */
    uint8_t mosChanged = ((bat1_discharge_mos_state != prevBat1DischargeMos) ||
                          (bat1_recharge_mos_state != prevBat1RechargeMos) ||
                          (bat2_discharge_mos_state != prevBat2DischargeMos) ||
                          (bat2_recharge_mos_state != prevBat2RechargeMos)) ? 1U : 0U;

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
    prevBat2DischargeMos = bat2_discharge_mos_state;
    prevBat2RechargeMos = bat2_recharge_mos_state;

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
  }

  if ((now - batteryCanLastTxTick) < FDCAN_BATTERY_WAKE_PERIOD_MS)
  {
    return;
  }

  batteryCanLastTxTick = now;
  if (fdcan1_busoff_flag == 0U)
  {
    (void)FDCAN_SendBatteryWakeFrame(&hfdcan1, 1U);
  }
  if (fdcan2_busoff_flag == 0U)
  {
    (void)FDCAN_SendBatteryWakeFrame(&hfdcan2, 2U);
  }
}
/**
 * @brief 确认 FDCAN 外设是否真正退出 Bus-Off
 * @param instance FDCAN 外设寄存器基地址
 * @param hfdcan FDCAN 句柄
 * @return 1 = 恢复成功（已退出 Bus-Off 且处于正常通信模式），0 = 仍在 Bus-Off 或配置模式
 * @note 遵循 ISO 11898-1 规范，退出 Bus-Off 的核心判据严格且唯一为 CCCR.INIT == 0 且 PSR.BO == 0。
 *       Error Warning (EW) 和 Error Passive (EP) 仅代表总线质量与错误告警级别，节点在此状态下
 *       依然具备完全的收发能力，绝不能视作 Bus-Off 恢复失败，否则会导致正常通信节点被死锁拦截。
 */
static uint8_t FDCAN_VerifyRecovery(const FDCAN_GlobalTypeDef *instance, const FDCAN_HandleTypeDef *hfdcan)
{
  if ((instance == NULL) || (hfdcan == NULL))
  {
    return 0U;
  }

  /* 核心判据 1: CCCR.INIT 已清零（已退出配置模式，处于正常通信运行状态） */
  if ((instance->CCCR & FDCAN_CCCR_INIT) != 0U)
  {
    return 0U;
  }

  /* 核心判据 2: PSR.BO 已清零（硬件已完成 129 × 11 bit 隐性位同步，真正退出 Bus-Off） */
  if ((instance->PSR & FDCAN_PSR_BO) != 0U)
  {
    return 0U;
  }

  return 1U;
}

/**
 * @brief 统一处理单个 FDCAN 实例的 Bus-Off 恢复状态机（纯异步非阻塞，0ms 忙等）
 * @param hfdcan FDCAN 句柄
 * @param instance FDCAN 外设实例
 * @param busoffFlag 对应 Bus-Off 标志指针
 * @param generation 对应代际序号指针
 * @param detectTick 对应检测时间戳指针
 * @param stateTick 对应状态切换时间戳指针
 * @param syncStartTick 对应总线同步开始时间戳指针
 * @param state 对应恢复状态指针
 * @param recoveryCount 恢复成功计数器指针
 * @param recoveryFailCount 恢复失败计数器指针
 * @param diagPsr 诊断指标：最后一次采样到的 PSR 指针（可选，可为 NULL）
 * @param diagEcr 诊断指标：最后一次采样到的 ECR 指针（可选，可为 NULL）
 * @param now 当前系统时间戳
 */
static void FDCAN_HandleInstanceBusOff(
  FDCAN_HandleTypeDef *hfdcan,
  FDCAN_GlobalTypeDef *instance,
  volatile uint8_t *busoffFlag,
  volatile uint32_t *generation,
  volatile uint32_t *detectTick,
  volatile uint32_t *stateTick,
  volatile uint32_t *syncStartTick,
  volatile FDCAN_BusOffState_t *state,
  volatile uint32_t *recoveryCount,
  volatile uint32_t *recoveryFailCount,
  volatile uint32_t *diagPsr,
  volatile uint32_t *diagEcr,
  uint32_t now)
{
  uint8_t localFlag;
  uint32_t localDetectTick;
  uint32_t localStateTick;
  uint32_t localGen;
  FDCAN_BusOffState_t localState;
  uint32_t primask;

  /* 原子读取状态快照，安全恢复原始中断屏蔽状态 */
  primask = __get_PRIMASK();
  __disable_irq();
  localFlag = *busoffFlag;
  localDetectTick = *detectTick;
  localStateTick = *stateTick;
  localGen = *generation;
  localState = *state;
  __set_PRIMASK(primask);

  if ((localFlag == 0U) && (localState == FDCAN_BUSOFF_STATE_IDLE))
  {
    /* 兜底检测：如果中断因异常丢失，主循环读取 PSR.BO 自动捕获并冻结现场诊断值 */
    uint32_t psr_snapshot = instance->PSR;

    if ((psr_snapshot & FDCAN_PSR_BO) != 0U)
    {
      primask = __get_PRIMASK();
      __disable_irq();

      /* 双重检查：防止中断回调已经处理了这个 Bus-Off 事件 */
      if (*busoffFlag == 0U)
      {
        (*generation)++;
        *busoffFlag = 1U;
        *detectTick = now;
        *state = FDCAN_BUSOFF_STATE_PENDING;
        if (diagPsr != NULL)
        {
          *diagPsr = psr_snapshot;  /* 使用首次读取的值，保留真实的 LEC/DLEC */
        }
        if (diagEcr != NULL)
        {
          *diagEcr = instance->ECR;
        }
      }

      __set_PRIMASK(primask);
    }
    return;
  }

  switch (localState)
  {
    case FDCAN_BUSOFF_STATE_PENDING:
      /* 优先检查：硬件是否已经脱离 Bus-Off（例如上一次恢复在冷却期内达成 129x11 同步，或硬件已自愈） */
      if (((instance->PSR & FDCAN_PSR_BO) == 0U) && ((instance->CCCR & FDCAN_CCCR_INIT) == 0U))
      {
        primask = __get_PRIMASK();
        __disable_irq();
        if (*generation == localGen)
        {
          hfdcan->State = HAL_FDCAN_STATE_BUSY;
          hfdcan->ErrorCode = HAL_FDCAN_ERROR_NONE;
          hfdcan->LatestTxFifoQRequest = 0U;
          *busoffFlag = 0U;
          *state = FDCAN_BUSOFF_STATE_IDLE;
          FDCAN_IncrementDebugCounter(recoveryCount);
        }
        __set_PRIMASK(primask);
        break;
      }

      /* 阶段 1: 检查 100ms 总线稳定等待期 */
      if ((now - localDetectTick) >= FDCAN_BUSOFF_RECOVERY_DELAY_MS)
      {
        /* 请求进入配置模式（CCCR.INIT = 1），纯异步非阻塞，无任何 while 忙等 */
        SET_BIT(instance->CCCR, FDCAN_CCCR_INIT);
        primask = __get_PRIMASK();
        __disable_irq();
        if (*generation == localGen)
        {
          *stateTick = now;
          *state = FDCAN_BUSOFF_STATE_REQUEST_INIT;
        }
        __set_PRIMASK(primask);
      }
      break;

    case FDCAN_BUSOFF_STATE_REQUEST_INIT:
      /* 阶段 2: 非阻塞检查硬件是否已确认进入 INIT 模式 */
      if ((instance->CCCR & FDCAN_CCCR_INIT) != 0U)
      {
        /* 已进入配置模式：使能 CCE，取消所有待发请求（严格使用标准有效掩码 0x7，绝不写入保留位） */
        SET_BIT(instance->CCCR, FDCAN_CCCR_CCE);
        instance->TXBCR = FDCAN_TXBCR_CR_Msk;
        hfdcan->LatestTxFifoQRequest = 0U;

        /* 请求退出配置模式（CCE = 0, INIT = 0），硬件将自主开启 129*11 bit 总线同步倒计时 */
        CLEAR_BIT(instance->CCCR, FDCAN_CCCR_CCE);
        CLEAR_BIT(instance->CCCR, FDCAN_CCCR_INIT);

        primask = __get_PRIMASK();
        __disable_irq();
        if (*generation == localGen)
        {
          *stateTick = now;
          *state = FDCAN_BUSOFF_STATE_START_SYNC;
        }
        __set_PRIMASK(primask);
      }
      else if ((now - localStateTick) >= FDCAN_BUSOFF_RECOVERY_TIMEOUT_MS)
      {
        /* 最坏情况防御：硬件未在 10ms 内响应 INIT，重置回 PENDING 阶段延后重试 */
        primask = __get_PRIMASK();
        __disable_irq();
        *detectTick = now;
        *state = FDCAN_BUSOFF_STATE_PENDING;
        __set_PRIMASK(primask);

        FDCAN_IncrementDebugCounter(recoveryFailCount);
      }
      break;

    case FDCAN_BUSOFF_STATE_START_SYNC:
      /* 阶段 3: 非阻塞检查硬件是否已成功退出 INIT 模式 */
      if ((instance->CCCR & FDCAN_CCCR_INIT) == 0U)
      {
        /* 成功退出 INIT 模式，硬件协议引擎在此刻开始 129 × 11 bit 隐性位同步计数 */
        primask = __get_PRIMASK();
        __disable_irq();
        if (*generation == localGen)
        {
          *syncStartTick = now;
          *state = FDCAN_BUSOFF_STATE_RECOVERING;
        }
        __set_PRIMASK(primask);
      }
      else if ((now - localStateTick) >= FDCAN_BUSOFF_RECOVERY_TIMEOUT_MS)
      {
        /* 退出 INIT 超时，重置检测时间下次重试 */
        primask = __get_PRIMASK();
        __disable_irq();
        *detectTick = now;
        *state = FDCAN_BUSOFF_STATE_PENDING;
        __set_PRIMASK(primask);

        FDCAN_IncrementDebugCounter(recoveryFailCount);
      }
      break;

    case FDCAN_BUSOFF_STATE_RECOVERING:
      /* 阶段 4: 持续非阻塞轮询硬件是否已退出 Bus-Off（持续等待 129 × 11 bit 隐性位同步完成）
       * 遵循 ISO 11898-1 规范：硬件在 CCCR.INIT=0 后自主累积 129 次 11 个隐性位序列。
       * 软件绝不设置超时强行打断重新写入 CCCR.INIT，否则在重载总线或周期性干扰下，每隔固定周期
       * 强行打断硬件计数器，会导致硬件永远无法累加到 129 次，造成永久无法恢复！
       * 纯异步持续轮询 PSR.BO == 0，一旦总线恢复，硬件完成同步后秒级自动捕获。 */
      if ((instance->PSR & FDCAN_PSR_BO) == 0U)
      {
        /* 硬件已成功脱离 Bus-Off，进行核心恢复确认 */
        if (FDCAN_VerifyRecovery(instance, hfdcan) != 0U)
        {
          primask = __get_PRIMASK();
          __disable_irq();
          if (*generation == localGen)
          {
            hfdcan->State = HAL_FDCAN_STATE_BUSY;
            hfdcan->ErrorCode = HAL_FDCAN_ERROR_NONE;
            hfdcan->LatestTxFifoQRequest = 0U;
            *busoffFlag = 0U;
            *state = FDCAN_BUSOFF_STATE_IDLE;
            FDCAN_IncrementDebugCounter(recoveryCount);
          }
          __set_PRIMASK(primask);
        }
      }
      break;

    default:
      *state = FDCAN_BUSOFF_STATE_IDLE;
      break;
  }
}

/**
 * @brief 公共接口：处理所有 FDCAN 的 Bus-Off 恢复（纯异步非阻塞高频处理）
 * @note 应在主循环中每次都调用，完全非阻塞，不阻塞双电池放电保护等实时任务
 */
void FDCAN_CheckAndRecoverAllBusOff(void)
{
  uint32_t now = HAL_GetTick();

  /* 只有在 FDCAN 已启动后才进行恢复 */
  if (batteryCanStarted != 0U)
  {
    FDCAN_HandleInstanceBusOff(
      &hfdcan1, FDCAN1,
      &fdcan1_busoff_flag, &fdcan1_busoff_generation,
      &fdcan1BusoffDetectTick, &fdcan1StateTick, &fdcan1SyncStartTick,
      &fdcan1_busoff_state,
      &fdcan1_busoff_recovery_count, &fdcan1_busoff_recovery_fail_count,
      &fdcan1_last_psr, &fdcan1_last_ecr,
      now);

    FDCAN_HandleInstanceBusOff(
      &hfdcan2, FDCAN2,
      &fdcan2_busoff_flag, &fdcan2_busoff_generation,
      &fdcan2BusoffDetectTick, &fdcan2StateTick, &fdcan2SyncStartTick,
      &fdcan2_busoff_state,
      &fdcan2_busoff_recovery_count, &fdcan2_busoff_recovery_fail_count,
      &fdcan2_last_psr, &fdcan2_last_ecr,
      now);

    FDCAN_HandleInstanceBusOff(
      &hfdcan3, FDCAN3,
      &fdcan3_busoff_flag, &fdcan3_busoff_generation,
      &fdcan3BusoffDetectTick, &fdcan3StateTick, &fdcan3SyncStartTick,
      &fdcan3_busoff_state,
      &fdcan3_busoff_recovery_count, &fdcan3_busoff_recovery_fail_count,
      &fdcan3_last_psr, &fdcan3_last_ecr,
      now);
  }
}

/**
 * @brief HAL FDCAN 错误回调（中断中调用，Bus-Off 中断触发）
 * @param hfdcan FDCAN 句柄
 * @param ErrorStatusITs 错误状态中断源位掩码
 * @note 必须同时检查当前 PSR.BO 是否为 1，排除退出 Bus-Off 时的状态跳变中断，防止假性二次恢复循环
 */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
  uint32_t now = HAL_GetTick();
  uint32_t psr_snapshot;

  /* 检查是否为 Bus-Off 中断 */
  if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0U)
  {
    /* 一次性读取 PSR 到局部变量，避免二次读取清除 LEC/DLEC */
    psr_snapshot = hfdcan->Instance->PSR;

    /* 确认当前处于 Bus-Off 状态（排除退出 Bus-Off 时的中断） */
    if ((psr_snapshot & FDCAN_PSR_BO) != 0U)
    {
      if (hfdcan->Instance == FDCAN1)
      {
        fdcan1_busoff_generation++;
        fdcan1_busoff_flag = 1U;
        fdcan1_busoff_state = FDCAN_BUSOFF_STATE_PENDING;
        fdcan1BusoffDetectTick = now;
        /* 冻结故障现场快照：使用首次读取的 PSR 保留真实的 LEC/DLEC */
        fdcan1_last_psr = psr_snapshot;
        fdcan1_last_ecr = hfdcan->Instance->ECR;
      }
      else if (hfdcan->Instance == FDCAN2)
      {
        fdcan2_busoff_generation++;
        fdcan2_busoff_flag = 1U;
        fdcan2_busoff_state = FDCAN_BUSOFF_STATE_PENDING;
        fdcan2BusoffDetectTick = now;
        fdcan2_last_psr = psr_snapshot;
        fdcan2_last_ecr = hfdcan->Instance->ECR;
      }
      else if (hfdcan->Instance == FDCAN3)
      {
        fdcan3_busoff_generation++;
        fdcan3_busoff_flag = 1U;
        fdcan3_busoff_state = FDCAN_BUSOFF_STATE_PENDING;
        fdcan3BusoffDetectTick = now;
        fdcan3_last_psr = psr_snapshot;
        fdcan3_last_ecr = hfdcan->Instance->ECR;
      }
    }
  }
}

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
  hfdcan1.Init.ExtFiltersNbr = 5;
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
  hfdcan2.Init.ExtFiltersNbr = 5;
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