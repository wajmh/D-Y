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
volatile float battery1_can_soc = 0.0f;
volatile uint8_t battery1_can_soc_valid = 0U;
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
volatile float battery2_can_soc = 0.0f;
volatile uint8_t battery2_can_soc_valid = 0U;
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

/* ============================================================================
 * Bus-Off 中断驱动恢复配置与状态机
 * ========================================================================== */
#define FDCAN_BUSOFF_RECOVERY_DELAY_MS 100U   /* 恢复延迟，等待总线稳定 */
#define FDCAN_BUSOFF_RECOVERY_TIMEOUT_MS 10U  /* 硬件操作超时（CCCR.INIT 响应） */

typedef enum
{
  FDCAN_BUSOFF_STATE_IDLE = 0U,           /* 正常工作状态，无 Bus-Off */
  FDCAN_BUSOFF_STATE_PENDING = 1U,        /* 等待总线稳定（100ms），优先检查是否自愈 */
  FDCAN_BUSOFF_STATE_REQUEST_INIT = 2U,   /* 请求进入配置模式（CCCR.INIT=1），非阻塞等待确认 */
  FDCAN_BUSOFF_STATE_START_SYNC = 3U,     /* 取消待发请求，请求退出配置模式（CCCR.INIT=0） */
  FDCAN_BUSOFF_STATE_RECOVERING = 4U      /* 持续非阻塞等待 129 × 11 bit 隐性位同步完成 */
} FDCAN_BusOffState_t;

/* 分别为 FDCAN1, FDCAN2, FDCAN3 定义恢复状态变量 */
#define DEFINE_FDCAN_BUSOFF_VARS(idx) \
  static volatile uint8_t fdcan##idx##_busoff_flag = 0U; \
  static volatile uint32_t fdcan##idx##_busoff_generation = 0U; \
  static volatile FDCAN_BusOffState_t fdcan##idx##_busoff_state = FDCAN_BUSOFF_STATE_IDLE; \
  static volatile uint32_t fdcan##idx##BusoffDetectTick = 0U; \
  static volatile uint32_t fdcan##idx##StateTick = 0U; \
  static volatile uint32_t fdcan##idx##SyncStartTick = 0U; \
  volatile uint32_t fdcan##idx##_busoff_recovery_count = 0U; \
  volatile uint32_t fdcan##idx##_busoff_recovery_fail_count = 0U; \
  volatile uint32_t fdcan##idx##_last_psr = 0U; \
  volatile uint32_t fdcan##idx##_last_ecr = 0U;

DEFINE_FDCAN_BUSOFF_VARS(1)
DEFINE_FDCAN_BUSOFF_VARS(2)
DEFINE_FDCAN_BUSOFF_VARS(3)

static uint8_t FDCAN_VerifyRecovery(const FDCAN_GlobalTypeDef *instance, const FDCAN_HandleTypeDef *hfdcan)
{
  if ((instance == NULL) || (hfdcan == NULL)) return 0U;
  if ((instance->CCCR & FDCAN_CCCR_INIT) != 0U) return 0U;
  if ((instance->PSR & FDCAN_PSR_BO) != 0U) return 0U;
  return 1U;
}

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
  uint32_t localDetectTick, localStateTick, localGen;
  FDCAN_BusOffState_t localState;
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  localFlag = *busoffFlag;
  localDetectTick = *detectTick;
  localStateTick = *stateTick;
  localGen = *generation;
  localState = *state;
  __set_PRIMASK(primask);

  /* 兜底检测（防御中断丢失） */
  if ((localFlag == 0U) && (localState == FDCAN_BUSOFF_STATE_IDLE))
  {
    uint32_t psr_snapshot = instance->PSR;
    if ((psr_snapshot & FDCAN_PSR_BO) != 0U)
    {
      primask = __get_PRIMASK();
      __disable_irq();
      if (*busoffFlag == 0U)
      {
        (*generation)++;
        *busoffFlag = 1U;
        *detectTick = now;
        *state = FDCAN_BUSOFF_STATE_PENDING;
        if (diagPsr != NULL) *diagPsr = psr_snapshot;
        if (diagEcr != NULL) *diagEcr = instance->ECR;
      }
      __set_PRIMASK(primask);
    }
    return;
  }

  switch (localState)
  {
    case FDCAN_BUSOFF_STATE_PENDING:
      /* 优先检查自愈 */
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

      /* 100ms 稳定期等待 */
      if ((now - localDetectTick) >= FDCAN_BUSOFF_RECOVERY_DELAY_MS)
      {
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
      if ((instance->CCCR & FDCAN_CCCR_INIT) != 0U)
      {
        /* 清除残留 TX 队列，准备退出 INIT */
        SET_BIT(instance->CCCR, FDCAN_CCCR_CCE);
        instance->TXBCR = FDCAN_TXBCR_CR_Msk;  /* 0x7 */
        hfdcan->LatestTxFifoQRequest = 0U;

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
        primask = __get_PRIMASK();
        __disable_irq();
        *detectTick = now;
        *state = FDCAN_BUSOFF_STATE_PENDING;
        __set_PRIMASK(primask);
        FDCAN_IncrementDebugCounter(recoveryFailCount);
      }
      break;

    case FDCAN_BUSOFF_STATE_START_SYNC:
      if ((instance->CCCR & FDCAN_CCCR_INIT) == 0U)
      {
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
        primask = __get_PRIMASK();
        __disable_irq();
        *detectTick = now;
        *state = FDCAN_BUSOFF_STATE_PENDING;
        __set_PRIMASK(primask);
        FDCAN_IncrementDebugCounter(recoveryFailCount);
      }
      break;

    case FDCAN_BUSOFF_STATE_RECOVERING:
      /* 核心：不设超时，持续轮询等待硬件完成 129×11 bit 同步 */
      if ((instance->PSR & FDCAN_PSR_BO) == 0U)
      {
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

void FDCAN_CheckAndRecoverAllBusOff(void)
{
  uint32_t now = HAL_GetTick();
  if (batteryCanStarted != 0U)
  {
    FDCAN_HandleInstanceBusOff(&hfdcan1, FDCAN1, &fdcan1_busoff_flag, &fdcan1_busoff_generation,
                              &fdcan1BusoffDetectTick, &fdcan1StateTick, &fdcan1SyncStartTick,
                              &fdcan1_busoff_state, &fdcan1_busoff_recovery_count,
                              &fdcan1_busoff_recovery_fail_count,
                              &fdcan1_last_psr, &fdcan1_last_ecr, now);
    FDCAN_HandleInstanceBusOff(&hfdcan2, FDCAN2, &fdcan2_busoff_flag, &fdcan2_busoff_generation,
                              &fdcan2BusoffDetectTick, &fdcan2StateTick, &fdcan2SyncStartTick,
                              &fdcan2_busoff_state, &fdcan2_busoff_recovery_count,
                              &fdcan2_busoff_recovery_fail_count,
                              &fdcan2_last_psr, &fdcan2_last_ecr, now);
    FDCAN_HandleInstanceBusOff(&hfdcan3, FDCAN3, &fdcan3_busoff_flag, &fdcan3_busoff_generation,
                              &fdcan3BusoffDetectTick, &fdcan3StateTick, &fdcan3SyncStartTick,
                              &fdcan3_busoff_state, &fdcan3_busoff_recovery_count,
                              &fdcan3_busoff_recovery_fail_count,
                              &fdcan3_last_psr, &fdcan3_last_ecr, now);
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

  /* 快速预检（无锁） */
  if (fdcan3_busoff_flag != 0U)
  {
    return HAL_BUSY;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    return HAL_BUSY;
  }

  /* 配置 txHeader（不需要在临界区内） */
  txHeader.Identifier = identifier;
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0U;

  /* 临界区保护：原子检查标志与入队 */
  primask = __get_PRIMASK();
  __disable_irq();
  if (fdcan3_busoff_flag != 0U)
  {
    __set_PRIMASK(primask);
    return HAL_BUSY;
  }
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
  uint32_t primask;

  statusData[0] = status;

  /* 快速预检（无锁） */
  if (fdcan3_busoff_flag != 0U)
  {
    return;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    return;
  }

  /* 配置 txHeader（不需要在临界区内） */
  txHeader.Identifier = FDCAN_RK_CHARGE_MODE_STATUS_ID;
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_1;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0U;

  /* 临界区保护：原子检查标志与入队 */
  primask = __get_PRIMASK();
  __disable_irq();
  if (fdcan3_busoff_flag != 0U)
  {
    __set_PRIMASK(primask);
    return;
  }
  (void)HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan3, &txHeader, statusData);
  __set_PRIMASK(primask);
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
  uint32_t primask;

  if ((rxHeader->IdType != FDCAN_EXTENDED_ID) ||
      (FDCAN_IsBatteryForwardFrame(rxHeader->Identifier) == 0U))
  {
    return;
  }

  /* 快速预检（无锁） */
  if (fdcan3_busoff_flag != 0U)
  {
    FDCAN_IncrementDebugCounter(&battery_can_forward_drop_count);
    return;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    FDCAN_IncrementDebugCounter(&battery_can_forward_drop_count);
    return;
  }

  /* 配置 txHeader（不需要在临界区内） */
  txHeader.Identifier = (rxHeader->Identifier & FDCAN_BATTERY_ID_MASK) | ((uint32_t)batteryIndex & 0xFFU);
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = rxHeader->RxFrameType;
  txHeader.DataLength = rxHeader->DataLength;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = batteryIndex;

  /* 临界区保护：原子检查标志与入队 */
  primask = __get_PRIMASK();
  __disable_irq();
  if (fdcan3_busoff_flag != 0U)
  {
    __set_PRIMASK(primask);
    FDCAN_IncrementDebugCounter(&battery_can_forward_drop_count);
    return;
  }

  if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan3, &txHeader, rxData) == HAL_OK)
  {
    FDCAN_IncrementDebugCounter(&battery_can_forward_count);
  }
  else
  {
    FDCAN_IncrementDebugCounter(&battery_can_forward_drop_count);
  }
  __set_PRIMASK(primask);
}

static void FDCAN_ParseBatteryStatus(uint8_t batteryIndex, uint32_t rxId, const uint8_t rxData[], uint32_t dataLength)
{
  uint16_t rawVoltage = ((uint16_t)rxData[0] << 8) | rxData[1];
  uint16_t rawCurrent = ((uint16_t)rxData[2] << 8) | rxData[3];
  float sumVoltage = (float)rawVoltage * 0.1f;
  float current = ((float)rawCurrent - 30000.0f) * 0.1f;
  float soc = 0.0f;
  uint8_t hasSoc = 0U;

  if (dataLength >= FDCAN_DLC_BYTES_6)
  {
    uint16_t rawSoc = ((uint16_t)rxData[4] << 8) | rxData[5];
    soc = (float)rawSoc * 0.1f;
    if (soc > 100.0f)
    {
      soc = 100.0f;
    }
    hasSoc = 1U;
  }

  if (batteryIndex == 1U)
  {
    battery1_can_sum_voltage = sumVoltage;
    battery1_can_current = current;
    if (hasSoc != 0U)
    {
      battery1_can_soc = soc;
      battery1_can_soc_valid = 1U;
    }
    battery1_can_rx_id = rxId;
    FDCAN_IncrementDebugCounter(&battery1_can_rx_count);
    battery1_can_status_last_rx_tick = HAL_GetTick();
  }
  else
  {
    battery2_can_sum_voltage = sumVoltage;
    battery2_can_current = current;
    if (hasSoc != 0U)
    {
      battery2_can_soc = soc;
      battery2_can_soc_valid = 1U;
    }
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
      FDCAN_ParseBatteryStatus(batteryIndex, rxHeader.Identifier, rxData, rxHeader.DataLength);
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
  uint32_t primask;

  if ((batteryIndex < 1U) || (batteryIndex > 2U))
  {
    return HAL_ERROR;
  }

  /* 快速预检（无锁） */
  if (fdcan2_busoff_flag != 0U)
  {
    return HAL_BUSY;
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

    /* 配置 txHeader（不需要在临界区内） */
    txHeader.Identifier = (frame->id & 0x00FFFFFFU) | 0x05000000U;
    txHeader.IdType = FDCAN_EXTENDED_ID;
    txHeader.TxFrameType = FDCAN_DATA_FRAME;
    txHeader.DataLength = frame->dataLength;
    txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    txHeader.BitRateSwitch = FDCAN_BRS_OFF;
    txHeader.FDFormat = FDCAN_CLASSIC_CAN;
    txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    txHeader.MessageMarker = batteryIndex;

    /* 临界区保护：原子检查标志与入队 */
    primask = __get_PRIMASK();
    __disable_irq();
    if (fdcan2_busoff_flag != 0U)
    {
      __set_PRIMASK(primask);
      return HAL_BUSY;
    }

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &txHeader, frame->data) == HAL_OK)
    {
      sentFrameCount++;
    }
    else
    {
      result = HAL_ERROR;
    }
    __set_PRIMASK(primask);
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
  HAL_StatusTypeDef status;
  uint32_t primask;

  /* 参数有效性检查 */
  if (hfdcan == NULL)
  {
    return HAL_ERROR;
  }

  /* 只允许 FDCAN1/2 调用此函数（FDCAN3 用于上报，不发送唤醒帧） */
  if ((hfdcan != &hfdcan1) && (hfdcan != &hfdcan2))
  {
    return HAL_ERROR;
  }

  /* 快速预检（无锁） */
  if ((hfdcan == &hfdcan1 && fdcan1_busoff_flag != 0U) ||
      (hfdcan == &hfdcan2 && fdcan2_busoff_flag != 0U))
  {
    return HAL_BUSY;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(hfdcan) == 0U)
  {
    return HAL_BUSY;
  }

  /* 配置 txHeader（不需要在临界区内） */
  txHeader.Identifier = 0x0400FF80U;
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = marker;

  /* 临界区保护：原子检查标志与入队 */
  primask = __get_PRIMASK();
  __disable_irq();
  if ((hfdcan == &hfdcan1 && fdcan1_busoff_flag != 0U) ||
      (hfdcan == &hfdcan2 && fdcan2_busoff_flag != 0U))
  {
    __set_PRIMASK(primask);
    return HAL_BUSY;
  }
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

  /* 使能 Bus-Off 中断 */
  (void)HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_BUS_OFF, 0U);
  (void)HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_BUS_OFF, 0U);
  (void)HAL_FDCAN_ActivateNotification(&hfdcan3, FDCAN_IT_BUS_OFF, 0U);

  batteryCanStarted = 1U;
  batteryCanLastTxTick = HAL_GetTick() - FDCAN_BATTERY_WAKE_PERIOD_MS;
  currentReportLastTxTick = HAL_GetTick();
  estopReportLastTxTick = HAL_GetTick() - FDCAN_ESTOP_REPORT_PERIOD_MS;
  batteryAlarmReportLastTxTick = HAL_GetTick() - FDCAN_BATTERY_ALARM_REPORT_PERIOD_MS;
}//负责初始化接收过滤器并启动 CAN。

void FDCAN_BatteryCanTask(void)
{
  static uint8_t prevBat1Alarm = BATTERY_ALARM_STATUS_NORMAL;
  static uint8_t prevBat2Alarm = BATTERY_ALARM_STATUS_NORMAL;
  static uint8_t prevBat1DischargeMos = 0U;
  static uint8_t prevBat1RechargeMos = 0U;
  static uint8_t prevBat1ChargeMos = 0U;
  static uint8_t prevBat2DischargeMos = 0U;
  static uint8_t prevBat2RechargeMos = 0U;
  static uint8_t prevBat2ChargeMos = 0U;
  static uint8_t alarmClearBurstRemaining = 0U;

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
    if ((charger_can_last_rx_tick != 0U) && (elapsed < 0x80000000U) && (elapsed >= FDCAN_CHARGER_CAN_TIMEOUT_MS))
    {
      rk_charge_mode_request = 0U;
      charge_mode_active = 0U;
      Power_ExitChargeMode();
      FDCAN_SendChargeModeStatusToRk(FDCAN_CHARGE_MODE_EXIT);
    }
  }

  /* FDCAN3 上报统一门禁：Bus-Off 期间跳过所有向 RK 的上报，避免无效计算 */
  if (fdcan3_busoff_flag == 0U)
  {
    if ((Power_IsEmergencyStopActive() != 0U) &&
        ((now - estopReportLastTxTick) >= FDCAN_ESTOP_REPORT_PERIOD_MS))
    {
      estopReportLastTxTick = now;
      FDCAN_SendEmergencyStopReportToRk();
    }

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
  } /* 结束 FDCAN3 门禁保护块 */

  if ((now - batteryCanLastTxTick) < FDCAN_BATTERY_WAKE_PERIOD_MS)
  {
    return;
  }

  batteryCanLastTxTick = now;
  (void)FDCAN_SendBatteryWakeFrame(&hfdcan1, 1U);
  (void)FDCAN_SendBatteryWakeFrame(&hfdcan2, 2U);
}//负责初始化接收过滤器并启动 CAN。

/* Bus-Off 中断回调：仅单次读取 PSR，记录代际号，置 pending 态 */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
  uint32_t now = HAL_GetTick();
  uint32_t psr_snapshot;

  if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0U)
  {
    psr_snapshot = hfdcan->Instance->PSR;
    if ((psr_snapshot & FDCAN_PSR_BO) != 0U)
    {
      if (hfdcan->Instance == FDCAN1)
      {
        fdcan1_busoff_generation++;
        fdcan1_busoff_flag = 1U;
        fdcan1_busoff_state = FDCAN_BUSOFF_STATE_PENDING;
        fdcan1BusoffDetectTick = now;
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
float FDCAN_GetBatterySoc(uint8_t batteryIndex)
{
  if (batteryIndex == 1U)
  {
    return battery1_can_soc;
  }
  else if (batteryIndex == 2U)
  {
    return battery2_can_soc;
  }
  return 0.0f;
}

uint8_t FDCAN_IsBatterySocValid(uint8_t batteryIndex)
{
  uint32_t now = HAL_GetTick();

  if (batteryIndex == 1U)
  {
    if ((battery1_can_soc_valid != 0U) &&
        (battery1_can_status_last_rx_tick != 0U) &&
        ((now - battery1_can_status_last_rx_tick) <= 2000U))
    {
      return 1U;
    }
  }
  else if (batteryIndex == 2U)
  {
    if ((battery2_can_soc_valid != 0U) &&
        (battery2_can_status_last_rx_tick != 0U) &&
        ((now - battery2_can_status_last_rx_tick) <= 2000U))
    {
      return 1U;
    }
  }

  return 0U;
}

uint8_t FDCAN_IsAnyBusOff(void)
{
  if ((fdcan1_busoff_flag != 0U) || (fdcan2_busoff_flag != 0U) || (fdcan3_busoff_flag != 0U))
  {
    return 1U;
  }
  if (((FDCAN1->PSR & FDCAN_PSR_BO) != 0U) ||
      ((FDCAN2->PSR & FDCAN_PSR_BO) != 0U) ||
      ((FDCAN3->PSR & FDCAN_PSR_BO) != 0U))
  {
    return 1U;
  }
  return 0U;
}
/* USER CODE END 1 */

