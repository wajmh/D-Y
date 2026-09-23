/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.c
  * @brief   This file provides code for the configuration
  *          of the ADC instances.
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
#include "adc.h"

/* USER CODE BEGIN 0 */

/* ADC2 DMA 缓冲区: 
 * adc2_buffer[0]: PA4 (ADC2_IN17) 外设放电电流采样
 * adc2_buffer[1]: PB15 (ADC2_IN15) VBUS 总线电压采样
 */
uint16_t adc2_buffer[2];

/* 全局测量变量 */
float vbus_voltage = 0.0f;
float peripheral_discharge_current = 0.0f;

/* 兼容保留的全局变量（旧板载采样已移除，数据由 BMS CAN 提供） */
float battery1_voltage = 0.0f;
float battery2_voltage = 0.0f;
float leg_current[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float battery1_current = 0.0f;
float battery2_current = 0.0f;

#define ADC_CURRENT_OFFSET_SAMPLE_COUNT               500U
#define ADC_PERIPHERAL_CURRENT_FILTER_ALPHA           0.20f
#define ADC_VBUS_VOLTAGE_MEDIAN_SIZE                  5U

static uint16_t peripheralDischargeCurrentOffsetRaw = 0U;
static uint8_t peripheralDischargeCurrentFilterInitialized = 0U;
static float vbusVoltageSamples[ADC_VBUS_VOLTAGE_MEDIAN_SIZE] = {0.0f};
static uint8_t vbusVoltageSampleIndex = 0U;
static uint8_t vbusVoltageSampleCount = 0U;

static float ADC_RawToVoltage(uint16_t raw)
{
    return (float)raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_RAW_VALUE;
}

static float ADC_PeripheralDischargeCurrentFromRaw(uint16_t raw)
{
    return (ADC_RawToVoltage(raw) - ADC_RawToVoltage(peripheralDischargeCurrentOffsetRaw)) * PERIPHERAL_DISCHARGE_CURRENT_AMPS_PER_VOLT;
}

static float ADC_LowPassFilter(float previous, float sample)
{
    return previous + (ADC_PERIPHERAL_CURRENT_FILTER_ALPHA * (sample - previous));
}

static float ADC_MedianFilterVbus(float sample,
                                  float samples[],
                                  uint8_t *sampleIndex,
                                  uint8_t *sampleCount)
{
    float sortedSamples[ADC_VBUS_VOLTAGE_MEDIAN_SIZE];
    float temp;
    uint8_t i;
    uint8_t j;

    samples[*sampleIndex] = sample;
    *sampleIndex = (uint8_t)((*sampleIndex + 1U) % ADC_VBUS_VOLTAGE_MEDIAN_SIZE);

    if (*sampleCount < ADC_VBUS_VOLTAGE_MEDIAN_SIZE)
    {
        (*sampleCount)++;
    }

    for (i = 0U; i < *sampleCount; i++)
    {
        sortedSamples[i] = samples[i];
    }

    for (i = 1U; i < *sampleCount; i++)
    {
        temp = sortedSamples[i];
        j = i;
        while ((j > 0U) && (sortedSamples[j - 1U] > temp))
        {
            sortedSamples[j] = sortedSamples[j - 1U];
            j--;
        }
        sortedSamples[j] = temp;
    }

    return sortedSamples[*sampleCount / 2U];
}

void ADC2_StartDMA(void)
{
    /* 启动前执行 STM32G4 逐次逼近型 ADC 硬件零点与电容自校准，消除偏置误差 */
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
    HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_buffer, 2U);
}

void ADC_CalibrateCurrentOffsets(void)
{
    uint32_t peripheralDischargeCurrentSum = 0U;
    uint16_t i;

    for (i = 0U; i < ADC_CURRENT_OFFSET_SAMPLE_COUNT; i++)
    {
        peripheralDischargeCurrentSum += adc2_buffer[0];
        HAL_Delay(1U);
    }

    peripheralDischargeCurrentOffsetRaw = (uint16_t)(peripheralDischargeCurrentSum / ADC_CURRENT_OFFSET_SAMPLE_COUNT);
    peripheralDischargeCurrentFilterInitialized = 0U;
}

void ADC_CalibrateLegCurrentOffsets(void)
{
    ADC_CalibrateCurrentOffsets();
}

void ADC_UpdateCurrents(void)
{
    float peripheralDischargeCurrentSample;

    peripheralDischargeCurrentSample = ADC_PeripheralDischargeCurrentFromRaw(adc2_buffer[0]);
    if (peripheralDischargeCurrentFilterInitialized == 0U)
    {
        peripheral_discharge_current = peripheralDischargeCurrentSample;
        peripheralDischargeCurrentFilterInitialized = 1U;
    }
    else
    {
        peripheral_discharge_current = ADC_LowPassFilter(peripheral_discharge_current, peripheralDischargeCurrentSample);
    }
}

void ADC2_UpdateVbusVoltage(void)
{
    float rawVoltage = ADC_RawToVoltage(adc2_buffer[1]);
    float calculatedVbus = rawVoltage * VBUS_VOLTAGE_DIVIDER_RATIO;

    vbus_voltage = ADC_MedianFilterVbus(calculatedVbus,
                                       vbusVoltageSamples,
                                       &vbusVoltageSampleIndex,
                                       &vbusVoltageSampleCount);
}

void ADC2_UpdateBatteryVoltages(void)
{
    ADC2_UpdateVbusVoltage();
}

float ADC_GetVbusVoltage(void)
{
    return vbus_voltage;
}

float ADC_GetPeripheralDischargeCurrent(void)
{
    return peripheral_discharge_current;
}

uint16_t ADC2_GetPeripheralDischargeCurrentADC(void)
{
    return adc2_buffer[0];
}

uint16_t ADC2_GetVbusVoltageADC(void)
{
    return adc2_buffer[1];
}

/* 兼容接口实现 */
uint16_t ADC1_GetLegCurrentADC(uint8_t leg) { (void)leg; return 0U; }
uint16_t ADC1_GetLegCurrentOffsetADC(uint8_t leg) { (void)leg; return 0U; }
uint16_t ADC1_GetBattery1CurrentADC(void) { return 0U; }
uint16_t ADC1_GetBattery2CurrentADC(void) { return 0U; }
uint16_t ADC2_GetPeripheralDischargeCurrentOffsetADC(void) { return peripheralDischargeCurrentOffsetRaw; }
uint16_t ADC2_GetBattery1ADC(void) { return 0U; }
uint16_t ADC2_GetBattery2ADC(void) { return 0U; }
float ADC_GetLegCurrent(uint8_t leg) { (void)leg; return 0.0f; }
float ADC_GetLeg1Current(void) { return 0.0f; }
float ADC_GetLeg2Current(void) { return 0.0f; }
float ADC_GetLeg3Current(void) { return 0.0f; }
float ADC_GetLeg4Current(void) { return 0.0f; }
float ADC_GetBattery1Current(void) { return 0.0f; }
float ADC_GetBattery2Current(void) { return 0.0f; }
float ADC2_GetBattery1Voltage(void) { return battery1_voltage; }
float ADC2_GetBattery2Voltage(void) { return battery2_voltage; }

/* USER CODE END 0 */

ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc2;

/* ADC2 init function */
void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc2.Init.Resolution = ADC_RESOLUTION_12B;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.GainCompensation = 0;
  hadc2.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = ENABLE;
  hadc2.Init.NbrOfConversion = 2;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.DMAContinuousRequests = ENABLE;
  hadc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc2.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_17;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_15;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

void HAL_ADC_MspInit(ADC_HandleTypeDef* adcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(adcHandle->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspInit 0 */

  /* USER CODE END ADC2_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC12;
    PeriphClkInit.Adc12ClockSelection = RCC_ADC12CLKSOURCE_SYSCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* ADC2 clock enable */
    __HAL_RCC_ADC12_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ADC2 GPIO Configuration
    PA4     ------> ADC2_IN17
    PB15     ------> ADC2_IN15
    */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* ADC2 DMA Init */
    /* ADC2 Init */
    hdma_adc2.Instance = DMA1_Channel2;
    hdma_adc2.Init.Request = DMA_REQUEST_ADC2;
    hdma_adc2.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc2.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc2.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc2.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc2.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc2.Init.Mode = DMA_CIRCULAR;
    hdma_adc2.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_adc2) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(adcHandle,DMA_Handle,hdma_adc2);

  /* USER CODE BEGIN ADC2_MspInit 1 */

  /* USER CODE END ADC2_MspInit 1 */
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* adcHandle)
{

  if(adcHandle->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspDeInit 0 */

  /* USER CODE END ADC2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ADC12_CLK_DISABLE();

    /**ADC2 GPIO Configuration
    PA4     ------> ADC2_IN17
    PB15     ------> ADC2_IN15
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_15);

    /* ADC2 DMA DeInit */
    HAL_DMA_DeInit(adcHandle->DMA_Handle);
  /* USER CODE BEGIN ADC2_MspDeInit 1 */

  /* USER CODE END ADC2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

