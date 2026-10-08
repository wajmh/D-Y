#ifndef __BOOT_MAIN_H__
#define __BOOT_MAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

#define BOOT_FLAG_MAGIC           0x55AA0000U
#define HOT_BOOT_MAGIC            0xAA550000U
#define BOOT_FLAG_SRAM_ADDR       0x2001FC00U  /* 预留 1KB 栈空间安全裕量，防止与 MSP 栈顶碰撞 */

/* 硬件电平宏定义 */
#define POWER_SWITCH_ON           GPIO_PIN_SET
#define POWER_SWITCH_OFF          GPIO_PIN_RESET
#define POWER_DCDC_ON             GPIO_PIN_RESET  /* DCDC 隔离模块为低电平使能 (Active-Low) */
#define POWER_DCDC_OFF            GPIO_PIN_SET

extern volatile uint8_t g_hotBootActiveBatMask;
extern volatile uint8_t g_bootForceStay;

void SystemClock_Config(void);
void Boot_Safety_GPIO_Init(void);
void Boot_Warm_GPIO_Init(uint8_t batMask);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_MAIN_H__ */
