#include "boot_main.h"
#include "boot_flash.h"
#include "boot_can.h"
#include "boot_jump.h"

volatile uint8_t g_hotBootActiveBatMask = 0U;

static void Boot_LED_Toggle(void)
{
  HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_14); /* 翻转蓝灯指示运行态 */
}

void Boot_Safety_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* 默认电平拉低 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, POWER_DCDC_OFF); /* PA7: 12V 使能关断 (Active-Low，拉高关断) */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, POWER_DCDC_OFF); /* PC4: 24V 使能关断 (Active-Low，拉高关断) */

  /* 1. 安全关断 GPIOA: PA5 (外设供电), PA7 (12V 使能) */
  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 2. 安全关断 GPIOB: PB6 (Bat1放电), PB7 (Bat1回充), PB9 (Bat1预放电), PB10 (总充电), PB11 (泄放2), PB12 (泄放1) */
  GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 3. 安全关断 GPIOC: PC4 (24V使能), PC10 (Bat2回充), PC11 (Bat2放电), PC12 (Bat2预放电), PC13 (蜂鸣器), PC15 (红灯), PC2 (绿灯) */
  GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* 4. PC14 配置为蓝灯指示，Bootloader 启动初始点亮 */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_SET);
}

void Boot_Warm_GPIO_Init(uint8_t batMask)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* 保持 12V / 24V DCDC 持续导通 (Active Low, 保持为 0) */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, POWER_DCDC_ON); /* PA7: 12V 导通 (LOW) */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, POWER_DCDC_ON); /* PC4: 24V 导通 (LOW) */

  /* 维持接力电池主放电 MOS 持续导通 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, ((batMask & 0x01U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, ((batMask & 0x02U) != 0U) ? POWER_SWITCH_ON : POWER_SWITCH_OFF);

  /* 其余危险回路关断 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2 | GPIO_PIN_10 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15, GPIO_PIN_RESET);

  /* PC14 蓝灯点亮指示进入 Bootloader 升级模式 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_SET);

  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV6;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV8;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    /* 若外部 HSE 异常，降级使用 HSI 16MHz (PLL 170MHz) */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    (void)HAL_RCC_OscConfig(&RCC_OscInitStruct);
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  (void)HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

int main(void)
{
  /* 1. 初始化 HAL 与基础时钟 */
  HAL_Init();
  SystemClock_Config();
  __enable_irq(); /* 使能全局中断，保证 SysTick 定时器与 HAL_Delay 正常运作 */

  /* 2. 检查升级标记 (SRAM 与 TAMP 备份寄存器) */
  uint8_t hasUpgradeFlag = 0U;
  uint8_t warmBatMask = 0U;
  volatile uint32_t *pSramFlag = (volatile uint32_t *)BOOT_FLAG_SRAM_ADDR;

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  uint32_t bkp0 = TAMP->BKP0R;
  if ((bkp0 & 0xFFFF0000U) == BOOT_FLAG_MAGIC)
  {
    hasUpgradeFlag = 1U;
    warmBatMask = (uint8_t)(bkp0 & 0xFFFFU);
    TAMP->BKP0R = 0U;
    *pSramFlag = 0U;
  }
  else if ((*pSramFlag & 0xFFFF0000U) == BOOT_FLAG_MAGIC)
  {
    hasUpgradeFlag = 1U;
    warmBatMask = (uint8_t)(*pSramFlag & 0xFFFFU);
    *pSramFlag = 0U;
  }

  /* 3. 根据是否热切入，执行对应 GPIO 初始化 */
  if (hasUpgradeFlag != 0U)
  {
    g_hotBootActiveBatMask = warmBatMask;
    Boot_Warm_GPIO_Init(warmBatMask);
    g_bootForceStay = 1U; /* 显式升级请求，停留 Bootloader 等待烧录 */
  }
  else
  {
    Boot_Safety_GPIO_Init();
  }

  /* 4. 初始化 FDCAN2 接口 */
  Boot_CAN_Init();

  /* 5. 检查 App 固件有效性 (轻量校验) */
  uint8_t appValid = Boot_Flash_IsAppValid();

  if (hasUpgradeFlag || (!appValid))
  {
    /* 存在升级请求，或者 App 无效/损坏 -> 永久停留在 Bootloader 等待烧录 */
    g_bootForceStay = 1U;
  }
  else
  {
    /* 冷启动且 App 固件有效：开启 100ms 等待窗口 */
    uint32_t startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < 100U)
    {
      Boot_CAN_Process();
      if (g_bootForceStay != 0U)
      {
        break; /* 收到上位机握手指令，留步 Bootloader */
      }
    }

    /* 若超时仍未收到任何升级握手，且没有留步标志，则跳转运行 App */
    if (g_bootForceStay == 0U)
    {
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_RESET); /* 熄灭蓝灯 */
      (void)HAL_FDCAN_Stop(&hfdcan2);
      (void)HAL_FDCAN_DeInit(&hfdcan2);
      Boot_JumpToApp();
    }
  }

  /* 6. Bootloader 在线等待与升级主循环 */
  uint32_t lastLedTick = HAL_GetTick();
  while (1)
  {
    Boot_CAN_Process();

    /* 蓝灯慢闪 (300ms 翻转一次) 指示 Bootloader 工作中 */
    if ((HAL_GetTick() - lastLedTick) >= 300U)
    {
      lastLedTick = HAL_GetTick();
      Boot_LED_Toggle();
    }
  }
}
