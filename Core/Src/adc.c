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

/* ADC2 DMA 缓冲区: 通道序号 3(PA6),4(PA7),11,12 */
uint16_t adc1_buffer[6]; /* PC0, PC1, PC2, PC3, PA2, PB12 */
uint16_t adc2_buffer[4]; /* PA6, PA7, PC5, PB2 */

/* 全局电池电压变量 */
float battery1_voltage = 0.0f;
float battery2_voltage = 0.0f;
float leg_current[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float battery1_current = 0.0f;
float battery2_current = 0.0f;
float peripheral_discharge_current = 0.0f;

#define ADC_LEG_CURRENT_OFFSET_SAMPLE_COUNT 500U
#define ADC_LEG_CURRENT_FILTER_ALPHA 0.20f
#define ADC_BATTERY_VOLTAGE_MEDIAN_SIZE 5U
#define ADC_BATTERY_CURRENT_MEDIAN_SIZE 5U

static uint16_t legCurrentOffsetRaw[4] = {0U, 0U, 0U, 0U};
static uint16_t peripheralDischargeCurrentOffsetRaw = 0U;
static uint8_t legCurrentFilterInitialized = 0U;
static uint8_t peripheralDischargeCurrentFilterInitialized = 0U;
static float battery1VoltageSamples[ADC_BATTERY_VOLTAGE_MEDIAN_SIZE] = {0.0f};
static float battery2VoltageSamples[ADC_BATTERY_VOLTAGE_MEDIAN_SIZE] = {0.0f};
static float battery1CurrentSamples[ADC_BATTERY_CURRENT_MEDIAN_SIZE] = {0.0f};
static float battery2CurrentSamples[ADC_BATTERY_CURRENT_MEDIAN_SIZE] = {0.0f};
static uint8_t battery1VoltageSampleIndex = 0U;
static uint8_t battery2VoltageSampleIndex = 0U;
static uint8_t battery1CurrentSampleIndex = 0U;
static uint8_t battery2CurrentSampleIndex = 0U;
static uint8_t battery1VoltageSampleCount = 0U;
static uint8_t battery2VoltageSampleCount = 0U;
static uint8_t battery1CurrentSampleCount = 0U;
static uint8_t battery2CurrentSampleCount = 0U;

static float ADC_RawToVoltage(uint16_t raw)
{
    return (float)raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_RAW_VALUE;
}

static float ADC_CurrentFromRaw(uint16_t raw, float zeroVoltage, float ampsPerVolt)
{
    return (ADC_RawToVoltage(raw) - zeroVoltage) * ampsPerVolt ;
}

static float ADC_LegCurrentFromRaw(uint8_t leg, uint16_t raw)
{
    return (ADC_RawToVoltage(raw) - ADC_RawToVoltage(legCurrentOffsetRaw[leg])) * LEG_CURRENT_AMPS_PER_VOLT;
}

static float ADC_PeripheralDischargeCurrentFromRaw(uint16_t raw)
{
    return (ADC_RawToVoltage(raw) - ADC_RawToVoltage(peripheralDischargeCurrentOffsetRaw)) * PERIPHERAL_DISCHARGE_CURRENT_AMPS_PER_VOLT;
}

static float ADC_LowPassFilter(float previous, float sample)
{
    return previous + (ADC_LEG_CURRENT_FILTER_ALPHA * (sample - previous));
}

static float ADC_MedianFilterVoltage(float sample,
                                     float samples[],
                                     uint8_t *sampleIndex,
                                     uint8_t *sampleCount)
{
    float sortedSamples[ADC_BATTERY_VOLTAGE_MEDIAN_SIZE];
    float temp;
    uint8_t i;
    uint8_t j;

    samples[*sampleIndex] = sample;
    *sampleIndex = (uint8_t)((*sampleIndex + 1U) % ADC_BATTERY_VOLTAGE_MEDIAN_SIZE);

    if (*sampleCount < ADC_BATTERY_VOLTAGE_MEDIAN_SIZE)
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

static float ADC_MedianFilterCurrent(float sample,
                                     float samples[],
                                     uint8_t *sampleIndex,
                                     uint8_t *sampleCount)
{
    float sortedSamples[ADC_BATTERY_CURRENT_MEDIAN_SIZE];
    float temp;
    uint8_t i;
    uint8_t j;

    samples[*sampleIndex] = sample;
    *sampleIndex = (uint8_t)((*sampleIndex + 1U) % ADC_BATTERY_CURRENT_MEDIAN_SIZE);

    if (*sampleCount < ADC_BATTERY_CURRENT_MEDIAN_SIZE)
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

void ADC1_StartDMA(void)
{
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_buffer, 6);
}

void ADC2_StartDMA(void)
{
    HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_buffer, 4);
}

void ADC_CalibrateLegCurrentOffsets(void)
{
    uint32_t sum[4] = {0U, 0U, 0U, 0U};
    uint32_t peripheralDischargeCurrentSum = 0U;
    uint16_t i;

    for (i = 0U; i < ADC_LEG_CURRENT_OFFSET_SAMPLE_COUNT; i++)
    {
        sum[0] += adc1_buffer[0];
        sum[1] += adc1_buffer[1];
        sum[2] += adc1_buffer[2];
        sum[3] += adc1_buffer[3];
        peripheralDischargeCurrentSum += adc2_buffer[3];
        HAL_Delay(1U);
    }

    legCurrentOffsetRaw[0] = (uint16_t)(sum[0] / ADC_LEG_CURRENT_OFFSET_SAMPLE_COUNT);
    legCurrentOffsetRaw[1] = (uint16_t)(sum[1] / ADC_LEG_CURRENT_OFFSET_SAMPLE_COUNT);
    legCurrentOffsetRaw[2] = (uint16_t)(sum[2] / ADC_LEG_CURRENT_OFFSET_SAMPLE_COUNT);
    legCurrentOffsetRaw[3] = (uint16_t)(sum[3] / ADC_LEG_CURRENT_OFFSET_SAMPLE_COUNT);
    peripheralDischargeCurrentOffsetRaw = (uint16_t)(peripheralDischargeCurrentSum / ADC_LEG_CURRENT_OFFSET_SAMPLE_COUNT);
    legCurrentFilterInitialized = 0U;
    peripheralDischargeCurrentFilterInitialized = 0U;
}

void ADC_UpdateCurrents(void)
{
    float legCurrentSample[4];
    float peripheralDischargeCurrentSample;
    float battery1Current;
    float battery2Current;

    legCurrentSample[0] = ADC_LegCurrentFromRaw(0U, adc1_buffer[0]);
    legCurrentSample[1] = ADC_LegCurrentFromRaw(1U, adc1_buffer[1]);
    legCurrentSample[2] = ADC_LegCurrentFromRaw(2U, adc1_buffer[2]);
    legCurrentSample[3] = ADC_LegCurrentFromRaw(3U, adc1_buffer[3]);

    if (legCurrentFilterInitialized == 0U)
    {
        leg_current[0] = legCurrentSample[0];
        leg_current[1] = legCurrentSample[1];
        leg_current[2] = legCurrentSample[2];
        leg_current[3] = legCurrentSample[3];
        legCurrentFilterInitialized = 1U;
    }
    else
    {
        leg_current[0] = ADC_LowPassFilter(leg_current[0], legCurrentSample[0]);
        leg_current[1] = ADC_LowPassFilter(leg_current[1], legCurrentSample[1]);
        leg_current[2] = ADC_LowPassFilter(leg_current[2], legCurrentSample[2]);
        leg_current[3] = ADC_LowPassFilter(leg_current[3], legCurrentSample[3]);
    }

    peripheralDischargeCurrentSample = ADC_PeripheralDischargeCurrentFromRaw(adc2_buffer[3]);
    if (peripheralDischargeCurrentFilterInitialized == 0U)
    {
        peripheral_discharge_current = peripheralDischargeCurrentSample;
        peripheralDischargeCurrentFilterInitialized = 1U;
    }
    else
    {
        peripheral_discharge_current = ADC_LowPassFilter(peripheral_discharge_current, peripheralDischargeCurrentSample);
    }

    battery1Current = ADC_CurrentFromRaw(adc1_buffer[4], CURRENT_ZERO_VOLTAGE, BATTERY_CURRENT_AMPS_PER_VOLT);
    battery2Current = ADC_CurrentFromRaw(adc1_buffer[5], CURRENT_ZERO_VOLTAGE, BATTERY_CURRENT_AMPS_PER_VOLT);
    battery1_current = ADC_MedianFilterCurrent(battery1Current,
                                               battery1CurrentSamples,
                                               &battery1CurrentSampleIndex,
                                               &battery1CurrentSampleCount);
    battery2_current = ADC_MedianFilterCurrent(battery2Current,
                                               battery2CurrentSamples,
                                               &battery2CurrentSampleIndex,
                                               &battery2CurrentSampleCount);
}

void ADC2_UpdateBatteryVoltages(void)
{
    float battery1Voltage = ADC_RawToVoltage(adc2_buffer[0]) * BATTERY_VOLTAGE_DIVIDER_RATIO + 0.9f;
    float battery2Voltage = ADC_RawToVoltage(adc2_buffer[1]) * BATTERY_VOLTAGE_DIVIDER_RATIO + 0.9f;

    battery1_voltage = ADC_MedianFilterVoltage(battery1Voltage,
                                               battery1VoltageSamples,
                                               &battery1VoltageSampleIndex,
                                               &battery1VoltageSampleCount);
    battery2_voltage = ADC_MedianFilterVoltage(battery2Voltage,
                                               battery2VoltageSamples,
                                               &battery2VoltageSampleIndex,
                                               &battery2VoltageSampleCount);
}

uint16_t ADC1_GetLegCurrentADC(uint8_t leg)
{
    if (leg >= 4U)
    {
        return 0U;
    }

    return adc1_buffer[leg];
}

uint16_t ADC1_GetLegCurrentOffsetADC(uint8_t leg)
{
    if (leg >= 4U)
    {
        return 0U;
    }

    return legCurrentOffsetRaw[leg];
}

uint16_t ADC2_GetPeripheralDischargeCurrentADC(void)
{
    return adc2_buffer[3];
}

uint16_t ADC2_GetPeripheralDischargeCurrentOffsetADC(void)
{
    return peripheralDischargeCurrentOffsetRaw;
}

uint16_t ADC2_GetBattery1ADC(void)
{
    return adc2_buffer[0];
}

uint16_t ADC2_GetBattery2ADC(void)
{
    return adc2_buffer[1];
}
uint16_t ADC1_GetBattery1CurrentADC(void)
{
    return adc1_buffer[4];
}

uint16_t ADC1_GetBattery2CurrentADC(void)
{
    return adc1_buffer[5];
}
float ADC_GetLegCurrent(uint8_t leg)
{
    if (leg >= 4U)
    {
        return 0.0f;
    }

    return leg_current[leg];
}

float ADC_GetLeg1Current(void)
{
    return leg_current[0];
}

float ADC_GetLeg2Current(void)
{
    return leg_current[1];
}

float ADC_GetLeg3Current(void)
{
    return leg_current[2];
}

float ADC_GetLeg4Current(void)
{
    return leg_current[3];
}

float ADC_GetPeripheralDischargeCurrent(void)
{
    return peripheral_discharge_current;
}

float ADC2_GetBattery1Voltage(void)
{
    return battery1_voltage;
}

float ADC2_GetBattery2Voltage(void)
{
    return battery2_voltage;
}
float ADC_GetBattery1Current(void)
{
    return battery1_current;
}

float ADC_GetBattery2Current(void)
{
    return battery2_current;
}

/* USER CODE END 0 */

ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc1;
DMA_HandleTypeDef hdma_adc2;

/* ADC1 init function */
void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.GainCompensation = 0;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.NbrOfConversion = 6;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_7;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_11;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}
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
  hadc2.Init.NbrOfConversion = 4;
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
  sConfig.Channel = ADC_CHANNEL_3;
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
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_11;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_12;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

static uint32_t HAL_RCC_ADC12_CLK_ENABLED=0;

void HAL_ADC_MspInit(ADC_HandleTypeDef* adcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspInit 0 */

  /* USER CODE END ADC1_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC12;
    PeriphClkInit.Adc12ClockSelection = RCC_ADC12CLKSOURCE_SYSCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* ADC1 clock enable */
    HAL_RCC_ADC12_CLK_ENABLED++;
    if(HAL_RCC_ADC12_CLK_ENABLED==1){
      __HAL_RCC_ADC12_CLK_ENABLE();
    }

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ADC1 GPIO Configuration
    PC0     ------> ADC1_IN6
    PC1     ------> ADC1_IN7
    PC2     ------> ADC1_IN8
    PC3     ------> ADC1_IN9
    PA2     ------> ADC1_IN3
    PB12     ------> ADC1_IN11
    PB14     ------> ADC1_IN5
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* ADC1 DMA Init */
    /* ADC1 Init */
    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Request = DMA_REQUEST_ADC1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(adcHandle,DMA_Handle,hdma_adc1);

  /* USER CODE BEGIN ADC1_MspInit 1 */

  /* USER CODE END ADC1_MspInit 1 */
  }
  else if(adcHandle->Instance==ADC2)
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
    HAL_RCC_ADC12_CLK_ENABLED++;
    if(HAL_RCC_ADC12_CLK_ENABLED==1){
      __HAL_RCC_ADC12_CLK_ENABLE();
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ADC2 GPIO Configuration
    PA6     ------> ADC2_IN3
    PA7     ------> ADC2_IN4
    PC5     ------> ADC2_IN11
    PB2     ------> ADC2_IN12
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
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

  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspDeInit 0 */

  /* USER CODE END ADC1_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_RCC_ADC12_CLK_ENABLED--;
    if(HAL_RCC_ADC12_CLK_ENABLED==0){
      __HAL_RCC_ADC12_CLK_DISABLE();
    }

    /**ADC1 GPIO Configuration
    PC0     ------> ADC1_IN6
    PC1     ------> ADC1_IN7
    PC2     ------> ADC1_IN8
    PC3     ------> ADC1_IN9
    PA2     ------> ADC1_IN3
    PB12     ------> ADC1_IN11
    PB14     ------> ADC1_IN5
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_12|GPIO_PIN_14);

    /* ADC1 DMA DeInit */
    HAL_DMA_DeInit(adcHandle->DMA_Handle);
  /* USER CODE BEGIN ADC1_MspDeInit 1 */

  /* USER CODE END ADC1_MspDeInit 1 */
  }
  else if(adcHandle->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspDeInit 0 */

  /* USER CODE END ADC2_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_RCC_ADC12_CLK_ENABLED--;
    if(HAL_RCC_ADC12_CLK_ENABLED==0){
      __HAL_RCC_ADC12_CLK_DISABLE();
    }

    /**ADC2 GPIO Configuration
    PA6     ------> ADC2_IN3
    PA7     ------> ADC2_IN4
    PC5     ------> ADC2_IN11
    PB2     ------> ADC2_IN12
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_6|GPIO_PIN_7);

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_5);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_2);

    /* ADC2 DMA DeInit */
    HAL_DMA_DeInit(adcHandle->DMA_Handle);
  /* USER CODE BEGIN ADC2_MspDeInit 1 */

  /* USER CODE END ADC2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

