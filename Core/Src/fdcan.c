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
#define FDCAN_BATTERY_STATUS_ID_BASE 0x04028000U
#define FDCAN_BATTERY_STATUS_ID_MASK 0x1FFFFF00U
#define FDCAN_BATTERY_MOS_ID_BASE    0x04068000U
#define FDCAN_BATTERY_MOS_ID_MASK    0x1FFFFF00U
#define FDCAN_BATTERY_INFO_ID_BASE   0x04038000U
#define FDCAN_BATTERY_FAULT_ID_BASE  0x040E8000U
#define FDCAN_BATTERY_TEMP_ID_BASE   0x04078000U
#define FDCAN_BATTERY_ID_MASK        0x1FFFFF00U

static uint8_t batteryCanStarted = 0U;
static uint32_t batteryCanLastTxTick = 0U;
volatile uint32_t battery_can_forward_count = 0U;
volatile uint32_t battery_can_forward_drop_count = 0U;
volatile float battery1_can_sum_voltage = 0.0f;
volatile float battery1_can_current = 0.0f;
volatile uint32_t battery1_can_rx_id = 0U;
volatile uint32_t battery1_can_rx_count = 0U;
volatile uint32_t battery1_can_status_last_rx_tick = 0U;
volatile uint8_t battery1_can_charge_mos_state = 0U;
volatile uint8_t battery1_can_discharge_mos_state = 0U;
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
volatile uint32_t battery2_can_mos_rx_id = 0U;
volatile uint32_t battery2_can_mos_rx_count = 0U;
volatile uint32_t battery2_can_mos_last_rx_tick = 0U;

static void FDCAN_IncrementDebugCounter(volatile uint32_t *counter)
{
  if (*counter < 0xFFFFFFFFU)
  {
    (*counter)++;
  }
}

static void FDCAN_ConfigBatteryRxFilters(FDCAN_HandleTypeDef *hfdcan)
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
      Error_Handler();
    }
  }
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
  uint8_t dischargeMosState = (rxData[1] != 0U) ? 1U : 0U;

  if (batteryIndex == 1U)
  {
    battery1_can_charge_mos_state = chargeMosState;
    battery1_can_discharge_mos_state = dischargeMosState;
    battery1_can_mos_rx_id = rxId;
    FDCAN_IncrementDebugCounter(&battery1_can_mos_rx_count);
    battery1_can_mos_last_rx_tick = HAL_GetTick();
  }
  else
  {
    battery2_can_charge_mos_state = chargeMosState;
    battery2_can_discharge_mos_state = dischargeMosState;
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
  batteryCanLastTxTick = HAL_GetTick();
}//负责初始化接收过滤器并启动 CAN。

void FDCAN_BatteryCanTask(void)
{
  if (batteryCanStarted == 0U)
  {
    return;
  }

  FDCAN_PollBatteryRx(&hfdcan1, 1U);
  FDCAN_PollBatteryRx(&hfdcan2, 2U);

  if ((HAL_GetTick() - batteryCanLastTxTick) < 2000U)
  {
    return;
  }

  batteryCanLastTxTick = HAL_GetTick();
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

