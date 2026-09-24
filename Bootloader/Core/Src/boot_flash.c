#include "boot_flash.h"
#include <string.h>

/* 标准 CRC32 快速计算 (以太网 / ISO 3309 多项式 0xEDB88320 反转多项式) */
static const uint32_t crc32_tab[16] = {
  0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
  0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
  0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
  0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
};

uint32_t Boot_Flash_CalculateCRC32(uint32_t startAddr, uint32_t length)
{
  uint32_t crc = 0xFFFFFFFFU;
  const uint8_t *pData = (const uint8_t *)startAddr;

  for (uint32_t i = 0U; i < length; i++)
  {
    uint8_t byte = pData[i];
    crc = (crc >> 4) ^ crc32_tab[(crc ^ byte) & 0x0FU];
    crc = (crc >> 4) ^ crc32_tab[(crc ^ (byte >> 4)) & 0x0FU];
  }

  return ~crc;
}

HAL_StatusTypeDef Boot_Flash_Unlock(void)
{
  return HAL_FLASH_Unlock();
}

HAL_StatusTypeDef Boot_Flash_Lock(void)
{
  return HAL_FLASH_Lock();
}

HAL_StatusTypeDef Boot_Flash_ErasePages(uint32_t startPage, uint32_t pageCount)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0U;
  HAL_StatusTypeDef status = HAL_OK;

  if ((startPage + pageCount) > BOOT_FLASH_TOTAL_PAGES)
  {
    return HAL_ERROR;
  }

  /* 清除之前的状态标志 */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

#if defined(FLASH_OPTR_DBANK)
  /* 若处于 Dual-Bank 模式 (128KB STM32G474 每 Bank 32 页) */
  if (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) != 0U)
  {
    uint32_t curPage = startPage;
    uint32_t remainPages = pageCount;

    while (remainPages > 0U)
    {
      eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
      if (curPage < 32U)
      {
        uint32_t pagesInBank1 = ((curPage + remainPages) > 32U) ? (32U - curPage) : remainPages;
        eraseInit.Banks = FLASH_BANK_1;
        eraseInit.Page = curPage;
        eraseInit.NbPages = pagesInBank1;
        status = HAL_FLASHEx_Erase(&eraseInit, &pageError);
        if (status != HAL_OK)
        {
          return status;
        }
        remainPages -= pagesInBank1;
        curPage += pagesInBank1;
      }
      else
      {
        eraseInit.Banks = FLASH_BANK_2;
        eraseInit.Page = curPage - 32U;
        eraseInit.NbPages = remainPages;
        status = HAL_FLASHEx_Erase(&eraseInit, &pageError);
        if (status != HAL_OK)
        {
          return status;
        }
        break;
      }
    }
    return HAL_OK;
  }
#endif

  /* Single-Bank 模式直接全量擦除 */
  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.Banks = FLASH_BANK_1;
  eraseInit.Page = startPage;
  eraseInit.NbPages = pageCount;
  status = HAL_FLASHEx_Erase(&eraseInit, &pageError);

  return status;
}

HAL_StatusTypeDef Boot_Flash_EraseApp(uint32_t appSize)
{
  HAL_StatusTypeDef status;
  uint32_t pagesToErase;

  if (appSize == 0U || appSize > BOOT_APP_MAX_SIZE)
  {
    pagesToErase = BOOT_FLASH_TOTAL_PAGES - BOOT_APP_START_PAGE; /* 全部 52 页 */
  }
  else
  {
    pagesToErase = (appSize + BOOT_FLASH_PAGE_SIZE - 1U) / BOOT_FLASH_PAGE_SIZE;
  }

  status = Boot_Flash_Unlock();
  if (status != HAL_OK) return status;

  /* 1. 首先擦除元数据页 (Page 11)，使 App 有效标记失效 */
  status = Boot_Flash_ErasePages(BOOT_APP_INFO_PAGE, 1U);
  if (status != HAL_OK)
  {
    Boot_Flash_Lock();
    return status;
  }

  /* 2. 擦除 App 固件存储区域 (Page 12 起) */
  status = Boot_Flash_ErasePages(BOOT_APP_START_PAGE, pagesToErase);

  Boot_Flash_Lock();
  return status;
}

HAL_StatusTypeDef Boot_Flash_WriteDoubleWord(uint32_t address, uint64_t data)
{
  /* 检查地址合法性 (必须在 App 区或元数据区，且 8 字节对齐) */
  if ((address < BOOT_APP_INFO_ADDR) || (address >= BOOT_APP_END_ADDR))
  {
    return HAL_ERROR;
  }
  if ((address & 0x07U) != 0U)
  {
    return HAL_ERROR;
  }

  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

  return HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, data);
}

HAL_StatusTypeDef Boot_Flash_WriteBuffer(uint32_t address, const uint8_t *data, uint32_t length)
{
  HAL_StatusTypeDef status;
  uint32_t offset = 0U;

  if ((length % 8U) != 0U)
  {
    return HAL_ERROR;
  }

  status = Boot_Flash_Unlock();
  if (status != HAL_OK) return status;

  while (offset < length)
  {
    uint64_t dwData = 0U;
    memcpy(&dwData, data + offset, 8U);

    status = Boot_Flash_WriteDoubleWord(address + offset, dwData);
    if (status != HAL_OK)
    {
      break;
    }
    offset += 8U;
  }

  Boot_Flash_Lock();
  return status;
}

void Boot_Flash_ReadAppInfo(BootAppInfo_t *info)
{
  if (info == NULL) return;
  const BootAppInfo_t *pFlashInfo = (const BootAppInfo_t *)BOOT_APP_INFO_ADDR;
  memcpy(info, pFlashInfo, sizeof(BootAppInfo_t));
}

HAL_StatusTypeDef Boot_Flash_WriteAppInfo(const BootAppInfo_t *info)
{
  HAL_StatusTypeDef status;
  uint8_t buffer[16] = {0};

  if (info == NULL) return HAL_ERROR;

  memcpy(buffer, info, sizeof(BootAppInfo_t));

  status = Boot_Flash_Unlock();
  if (status != HAL_OK) return status;

  /* 擦除元数据页 */
  status = Boot_Flash_ErasePages(BOOT_APP_INFO_PAGE, 1U);
  if (status == HAL_OK)
  {
    /* 写入 16 字节 (2 个双字) */
    uint64_t dword1 = 0U, dword2 = 0U;
    memcpy(&dword1, buffer, 8U);
    memcpy(&dword2, buffer + 8U, 8U);

    status = Boot_Flash_WriteDoubleWord(BOOT_APP_INFO_ADDR, dword1);
    if (status == HAL_OK)
    {
      status = Boot_Flash_WriteDoubleWord(BOOT_APP_INFO_ADDR + 8U, dword2);
    }
  }

  Boot_Flash_Lock();
  return status;
}

uint8_t Boot_Flash_IsAppValid(void)
{
  /* 1. 检查栈顶指针 (SRAM 范围: 0x20000000 ~ 0x20020000，128KB SRAM) */
  uint32_t appMsp = *(volatile uint32_t *)BOOT_APP_START_ADDR;
  if ((appMsp & 0xFFFE0000U) != 0x20000000U)
  {
    return 0U;
  }

  /* 2. 检查复位向量入口地址是否在 App Flash 范围内 (0x08006000 ~ 0x08020000) */
  uint32_t resetHandler = *(volatile uint32_t *)(BOOT_APP_START_ADDR + 4U);
  if ((resetHandler < BOOT_APP_START_ADDR) || (resetHandler >= BOOT_APP_END_ADDR))
  {
    return 0U;
  }

  /* 3. 严格检查元数据标志区 (Page 11)，防止升级中途中断/掉电后误跳入残缺 App 变砖 */
  BootAppInfo_t info;
  Boot_Flash_ReadAppInfo(&info);
  if ((info.magic != BOOT_APP_MAGIC_VALID) || (info.app_size == 0U) || (info.app_size > BOOT_APP_MAX_SIZE))
  {
    return 0U;
  }

  return 1U;
}
