#ifndef __BOOT_FLASH_H__
#define __BOOT_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

/* STM32G474 Flash 扇区与内存布局宏定义 (Single-Bank 模式: 每页 4KB, 共 32 页) */
#define BOOT_FLASH_BASE_ADDR        0x08000000U
#define BOOT_FLASH_PAGE_SIZE        0x00001000U  /* 4096 Bytes (4 KB) */
#define BOOT_FLASH_TOTAL_PAGES      32U

#define BOOT_LOADER_START_PAGE      0U
#define BOOT_LOADER_PAGE_COUNT      5U           /* Page 0 ~ 4, 共 20KB */

#define BOOT_APP_INFO_PAGE          5U           /* Page 5: 0x08005000 ~ 0x08005FFF (4KB) */
#define BOOT_APP_INFO_ADDR          0x08005000U

#define BOOT_APP_START_PAGE         6U           /* Page 6: 0x08006000 */
#define BOOT_APP_START_ADDR         0x08006000U
#define BOOT_APP_MAX_SIZE           (104U * 1024U) /* 104 KB (Page 6 ~ 31, 共 26 页) */
#define BOOT_APP_END_ADDR           (BOOT_APP_START_ADDR + BOOT_APP_MAX_SIZE)

#define BOOT_APP_MAGIC_VALID        0xA5A55A5AU
#define BOOT_APP_MAGIC_INVALID      0xFFFFFFFFU
#define BOOT_APP_MAGIC_ERASED       0xDEADBEEFU

#pragma pack(push, 4)
typedef struct
{
  uint32_t magic;       /* 固件有效标志: 0xA5A55A5A (有效), 0xDEADBEEF (正在升级), 0xFFFFFFFF (空白/出厂) */
  uint32_t app_size;    /* 固件实际大小(字节) */
  uint32_t app_crc32;   /* 全局 CRC32 校验码 */
  uint32_t upgrade_cnt; /* 累计升级次数计数 */
} BootAppInfo_t;
#pragma pack(pop)

/* Flash 驱动函数接口 */
HAL_StatusTypeDef Boot_Flash_Unlock(void);
HAL_StatusTypeDef Boot_Flash_Lock(void);
uint32_t Boot_Flash_GetPageSize(void);
uint32_t Boot_Flash_GetPage(uint32_t address);
HAL_StatusTypeDef Boot_Flash_ErasePages(uint32_t startPage, uint32_t pageCount);
HAL_StatusTypeDef Boot_Flash_EraseApp(uint32_t appSize);
HAL_StatusTypeDef Boot_Flash_WriteDoubleWord(uint32_t address, uint64_t data);
HAL_StatusTypeDef Boot_Flash_WriteBuffer(uint32_t address, const uint8_t *data, uint32_t length);

/* CRC32 校验与元数据管理 */
uint32_t Boot_Flash_CalculateCRC32(uint32_t startAddr, uint32_t length);
void Boot_Flash_ReadAppInfo(BootAppInfo_t *info);
HAL_StatusTypeDef Boot_Flash_WriteAppInfo(const BootAppInfo_t *info);
uint8_t Boot_Flash_IsAppValid(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_FLASH_H__ */
