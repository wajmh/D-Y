#include "boot_jump.h"
#include "boot_flash.h"

typedef void (*pFunction)(void);

void Boot_JumpToApp(void)
{
  uint32_t appMsp;
  uint32_t resetHandlerAddr;
  pFunction appEntry;

  /* 检查栈顶地址是否合法 (SRAM 范围: 0x20000400 ~ 0x20020000，且 8 字节对齐) */
  appMsp = *(volatile uint32_t *)BOOT_APP_START_ADDR;
  if ((appMsp < 0x20000400U) || (appMsp > 0x20020000U) || ((appMsp & 0x07U) != 0U))
  {
    return;
  }

  /* 检查复位入口地址是否合法 */
  resetHandlerAddr = *(volatile uint32_t *)(BOOT_APP_START_ADDR + 4U);
  if ((resetHandlerAddr < BOOT_APP_START_ADDR) || (resetHandlerAddr >= BOOT_APP_END_ADDR))
  {
    return;
  }

  /* 1. 注意：热接力绝不能调用 HAL_DeInit()，因为其会强行复位 AHB2 导致 GPIO 掉电。
   * 首先调用 HAL_RCC_DeInit() 将时钟安全回退至 HSI 16MHz，以便 App 正常执行 SystemClock_Config。
   * (此时中断与 SysTick 尚在运行，HAL_RCC_DeInit 内部的 HAL_GetTick 超时机制可正常工作，避免死锁) */
  HAL_RCC_DeInit();

  /* 2. 彻底禁用全局中断，防止跳转过程中产生悬挂中断 */
  __disable_irq();

  /* 3. 停止 SysTick 定时器并清空计数器及其中断使能 (消除 HAL_RCC_DeInit 内部 HAL_InitTick 的副作用) */
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL  = 0U;

  /* 4. 清除 SysTick 挂起中断与所有 NVIC 中断使能和挂起请求 */
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  for (uint8_t i = 0; i < 8; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFU;
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }

  /* 刷新并重置 Flash 预取与指令/数据缓存，防止旧指令残留导致 HardFault */
  __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
  __HAL_FLASH_DATA_CACHE_DISABLE();
  __HAL_FLASH_INSTRUCTION_CACHE_RESET();
  __HAL_FLASH_DATA_CACHE_RESET();
  __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
  __HAL_FLASH_DATA_CACHE_ENABLE();

  /* 5. 重定位中断向量表到 App 基地址 */
  SCB->VTOR = BOOT_APP_START_ADDR;

  /* 6. 复位特权与堆栈控制寄存器，设置主堆栈指针 (MSP) 并执行内存屏障 */
  __set_CONTROL(0U);
  __set_MSP(appMsp);
  __DSB();
  __ISB();

  /* 7. 跳转至 App 复位中断服务例程 */
  appEntry = (pFunction)resetHandlerAddr;
  appEntry();

  /* 正常情况下绝不会执行到这里 */
  while (1)
  {
  }
}
