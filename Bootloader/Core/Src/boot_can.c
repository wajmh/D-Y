#include "boot_can.h"
#include "boot_main.h"
#include "boot_flash.h"
#include "boot_jump.h"
#include <string.h>

FDCAN_HandleTypeDef hfdcan2;
volatile BootState_t g_bootState = BOOT_STATE_IDLE;
volatile uint8_t g_bootForceStay = 0U;

static uint32_t g_firmwareTotalSize = 0U;
static uint32_t g_firmwareWrittenBytes = 0U;
static uint16_t g_expectedPacketIndex = 0U;
static uint8_t  g_doubleWordBuffer[8] = {0};
static uint8_t  g_doubleWordByteCount = 0U;

/* 发送应答帧给上位机 */
HAL_StatusTypeDef Boot_CAN_SendResponse(uint8_t cmd, uint8_t ack, const uint8_t *payload, uint8_t payloadLen)
{
  FDCAN_TxHeaderTypeDef txHeader;
  uint8_t txData[8] = {0};

  txHeader.Identifier = BOOT_CAN_RESP_ID;
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0U;

  txData[0] = cmd;
  txData[1] = ack;

  if (payload != NULL && payloadLen > 0U)
  {
    uint8_t copyLen = (payloadLen > 6U) ? 6U : payloadLen;
    memcpy(&txData[2], payload, copyLen);
  }

  /* 等待发送 FIFO 有空闲空间 */
  uint32_t timeout = 1000U;
  while ((HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2) == 0U) && (timeout > 0U))
  {
    timeout--;
  }

  return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &txHeader, txData);
}

void Boot_CAN_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  FDCAN_FilterTypeDef filterConfig = {0};

  /* 1. 配置 FDCAN 内核时钟源为 PCLK1 (170MHz，确保 250kbps 波特率精准匹配) */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
  PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    return;
  }

  /* 2. 使能 GPIO 与 FDCAN 外设时钟 */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_FDCAN_CLK_ENABLE();

  /* 3. 配置引脚: PB5 -> RX (AF9), PB13 -> TX (AF9) */
  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF9_FDCAN2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 3. 配置 FDCAN2 核心参数 (250kbps 经典扩展帧，与主板完全兼容) */
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
  hfdcan2.Init.StdFiltersNbr = 0;
  hfdcan2.Init.ExtFiltersNbr = 1;
  hfdcan2.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

  if (HAL_FDCAN_Init(&hfdcan2) != HAL_OK)
  {
    return;
  }

  /* 4. 过滤器配置: 仅接收 BOOT_CAN_CMD_ID (0x04700000) */
  filterConfig.IdType = FDCAN_EXTENDED_ID;
  filterConfig.FilterIndex = 0U;
  filterConfig.FilterType = FDCAN_FILTER_MASK;
  filterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filterConfig.FilterID1 = BOOT_CAN_CMD_ID;
  filterConfig.FilterID2 = 0x1FFFFFFFU;

  (void)HAL_FDCAN_ConfigFilter(&hfdcan2, &filterConfig);
  (void)HAL_FDCAN_ConfigGlobalFilter(&hfdcan2, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);

  /* 5. 启动 FDCAN 控制器 */
  (void)HAL_FDCAN_Start(&hfdcan2);
}

static void Boot_CAN_HandlePing(const uint8_t *rxData)
{
  (void)rxData;
  uint8_t payload[6] = {0};

  payload[0] = 0x01U; /* 当前处于 Bootloader 运行态 */
  payload[1] = 0x01U; /* Bootloader Major Version */
  payload[2] = 0x00U; /* Bootloader Minor Version */
  payload[3] = Boot_Flash_IsAppValid() ? 0x01U : 0x00U; /* App 固件有效状态 */
  payload[4] = 0x00U;
  payload[5] = 0x00U;

  g_bootForceStay = 1U;
  Boot_CAN_SendResponse(BOOT_CMD_PING, BOOT_ACK_OK, payload, sizeof(payload));
}

static void Boot_CAN_HandleStartUpgrade(const uint8_t *rxData)
{
  uint32_t fwSize = ((uint32_t)rxData[1] << 24) |
                    ((uint32_t)rxData[2] << 16) |
                    ((uint32_t)rxData[3] << 8)  |
                    ((uint32_t)rxData[4]);

  uint8_t payload[6] = {0};

  if ((fwSize == 0U) || (fwSize > BOOT_APP_MAX_SIZE))
  {
    payload[0] = 0x01U; /* 大小超限错误 */
    Boot_CAN_SendResponse(BOOT_CMD_START_UPGRADE, BOOT_ACK_ERR_PARAM, payload, 1U);
    return;
  }

  g_firmwareTotalSize = fwSize;
  g_firmwareWrittenBytes = 0U;
  g_expectedPacketIndex = 0U;
  g_doubleWordByteCount = 0U;
  memset(g_doubleWordBuffer, 0xFF, sizeof(g_doubleWordBuffer));

  g_bootState = BOOT_STATE_READY;
  g_bootForceStay = 1U;

  payload[0] = (uint8_t)(Boot_Flash_GetPageSize() / 1024U); /* Flash 页大小: 2KB 或 4KB */
  payload[1] = (uint8_t)(BOOT_APP_MAX_SIZE / 1024U); /* 最大容量 104KB */
  Boot_CAN_SendResponse(BOOT_CMD_START_UPGRADE, BOOT_ACK_OK, payload, 2U);
}

static void Boot_CAN_HandleEraseApp(const uint8_t *rxData)
{
  /* 检查安全魔数 0x5AA5 */
  if ((rxData[1] != 0x5AU) || (rxData[2] != 0xA5U))
  {
    Boot_CAN_SendResponse(BOOT_CMD_ERASE_APP, BOOT_ACK_ERR_PARAM, NULL, 0U);
    return;
  }

  HAL_StatusTypeDef status = Boot_Flash_EraseApp(g_firmwareTotalSize);
  if (status == HAL_OK)
  {
    /* 擦除完成后保持 Flash 解锁，为接下来的连续分包烧录做准备 */
    Boot_Flash_Unlock();
    g_bootState = BOOT_STATE_RECEIVING;
    Boot_CAN_SendResponse(BOOT_CMD_ERASE_APP, BOOT_ACK_OK, NULL, 0U);
  }
  else
  {
    Boot_Flash_Lock();
    Boot_CAN_SendResponse(BOOT_CMD_ERASE_APP, BOOT_ACK_ERR_ERASE, NULL, 0U);
  }
}

static void Boot_CAN_HandleDataPacket(const uint8_t *rxData)
{
  uint16_t packetIdx = ((uint16_t)rxData[1] << 8) | ((uint16_t)rxData[2]);
  uint8_t len = rxData[3];

  if ((len == 0U) || (len > 4U) || (g_bootState != BOOT_STATE_RECEIVING))
  {
    Boot_CAN_SendResponse(BOOT_CMD_DATA_PACKET, BOOT_ACK_ERR_PARAM, NULL, 0U);
    return;
  }

  /* 序号检查 (支持重传同序号帧) */
  if (packetIdx != g_expectedPacketIndex)
  {
    uint8_t payload[4];
    payload[0] = (uint8_t)(g_expectedPacketIndex >> 8);
    payload[1] = (uint8_t)(g_expectedPacketIndex & 0xFFU);
    Boot_CAN_SendResponse(BOOT_CMD_DATA_PACKET, BOOT_ACK_ERR_SEQ, payload, 2U);
    return;
  }

  /* 逐字节填入 64-bit (8 字节) 缓存 */
  for (uint8_t i = 0U; i < len; i++)
  {
    g_doubleWordBuffer[g_doubleWordByteCount++] = rxData[4U + i];

    if (g_doubleWordByteCount == 8U)
    {
      uint32_t writeAddr = BOOT_APP_START_ADDR + g_firmwareWrittenBytes;
      uint64_t dwData = 0U;
      memcpy(&dwData, g_doubleWordBuffer, 8U);

      HAL_StatusTypeDef status = Boot_Flash_WriteDoubleWord(writeAddr, dwData);

      if (status != HAL_OK)
      {
        uint32_t flashError = HAL_FLASH_GetError();
        uint8_t errPayload[6];
        errPayload[0] = (uint8_t)status;
        errPayload[1] = (uint8_t)(flashError & 0xFFU);
        errPayload[2] = (uint8_t)((flashError >> 8) & 0xFFU);
        errPayload[3] = (uint8_t)(packetIdx >> 8);
        errPayload[4] = (uint8_t)(packetIdx & 0xFFU);
        errPayload[5] = 0U;
        Boot_Flash_Lock();
        Boot_CAN_SendResponse(BOOT_CMD_DATA_PACKET, BOOT_ACK_ERR_WRITE, errPayload, 6U);
        return;
      }

      g_firmwareWrittenBytes += 8U;
      g_doubleWordByteCount = 0U;
      memset(g_doubleWordBuffer, 0xFF, sizeof(g_doubleWordBuffer));
    }
  }

  g_expectedPacketIndex++;

  /* 每满 16 包 (64 字节) 或传输完成时，向上位机回复 ACK 报文 */
  uint8_t isComplete = (g_firmwareWrittenBytes + g_doubleWordByteCount >= g_firmwareTotalSize);
  if ((packetIdx % 16U == 15U) || isComplete)
  {
    uint8_t payload[6];
    payload[0] = (uint8_t)(packetIdx >> 8);
    payload[1] = (uint8_t)(packetIdx & 0xFFU);
    uint32_t progress = g_firmwareWrittenBytes + g_doubleWordByteCount;
    payload[2] = (uint8_t)(progress >> 24);
    payload[3] = (uint8_t)(progress >> 16);
    payload[4] = (uint8_t)(progress >> 8);
    payload[5] = (uint8_t)(progress & 0xFFU);
    Boot_CAN_SendResponse(BOOT_CMD_DATA_PACKET, BOOT_ACK_OK, payload, 6U);
  }
}

static void Boot_CAN_HandleVerifyApp(const uint8_t *rxData)
{
  uint32_t expectedCrc = ((uint32_t)rxData[1] << 24) |
                         ((uint32_t)rxData[2] << 16) |
                         ((uint32_t)rxData[3] << 8)  |
                         ((uint32_t)rxData[4]);

  /* 1. 若最后还有未满 8 字节的尾部数据，补 0xFF 写入 */
  if (g_doubleWordByteCount > 0U)
  {
    uint32_t writeAddr = BOOT_APP_START_ADDR + g_firmwareWrittenBytes;
    uint64_t dwData = 0U;
    memcpy(&dwData, g_doubleWordBuffer, 8U);

    (void)Boot_Flash_WriteDoubleWord(writeAddr, dwData);

    g_firmwareWrittenBytes += 8U;
    g_doubleWordByteCount = 0U;
  }

  /* 2. 计算 Flash 中固件的 CRC32 校验码 */
  uint32_t actualCrc = Boot_Flash_CalculateCRC32(BOOT_APP_START_ADDR, g_firmwareTotalSize);

  uint8_t payload[6] = {0};
  payload[0] = (uint8_t)(actualCrc >> 24);
  payload[1] = (uint8_t)(actualCrc >> 16);
  payload[2] = (uint8_t)(actualCrc >> 8);
  payload[3] = (uint8_t)(actualCrc & 0xFFU);

  if (actualCrc == expectedCrc)
  {
    /* 3. 写入元数据标志区 */
    BootAppInfo_t appInfo;
    appInfo.magic = BOOT_APP_MAGIC_VALID;
    appInfo.app_size = g_firmwareTotalSize;
    appInfo.app_crc32 = actualCrc;
    appInfo.upgrade_cnt = 1U;

    Boot_Flash_WriteAppInfo(&appInfo);
    g_bootState = BOOT_STATE_VERIFIED;

    Boot_CAN_SendResponse(BOOT_CMD_VERIFY_APP, BOOT_ACK_OK, payload, 4U);
  }
  else
  {
    Boot_CAN_SendResponse(BOOT_CMD_VERIFY_APP, BOOT_ACK_ERR_CRC, payload, 4U);
  }

  /* 校验结束，锁定 Flash */
  Boot_Flash_Lock();
}

static void Boot_CAN_HandleRunApp(const uint8_t *rxData)
{
  (void)rxData;

  Boot_CAN_SendResponse(BOOT_CMD_RUN_APP, BOOT_ACK_OK, NULL, 0U);

  /* 延时 20ms 保证 ACK 送上 CAN 总线 */
  HAL_Delay(20);

  /* 写入热启动魔数与当前接力电池掩码到 TAMP->BKP1R */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  TAMP->BKP1R = (HOT_BOOT_MAGIC & 0xFFFF0000U) | (g_hotBootActiveBatMask & 0xFFFFU);

  /* 停止并反初始化 FDCAN2 外设 */
  (void)HAL_FDCAN_Stop(&hfdcan2);
  (void)HAL_FDCAN_DeInit(&hfdcan2);

  /* 无缝热跳转回 App */
  Boot_JumpToApp();
}

void Boot_CAN_Process(void)
{
  FDCAN_RxHeaderTypeDef rxHeader;
  uint8_t rxData[8] = {0};

  while (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan2, FDCAN_RX_FIFO0) > 0U)
  {
    if (HAL_FDCAN_GetRxMessage(&hfdcan2, FDCAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK)
    {
      break;
    }

    if ((rxHeader.IdType != FDCAN_EXTENDED_ID) ||
        (rxHeader.RxFrameType != FDCAN_DATA_FRAME) ||
        (rxHeader.Identifier != BOOT_CAN_CMD_ID) ||
        (rxHeader.DataLength < FDCAN_DLC_BYTES_1))
    {
      continue;
    }

    uint8_t cmd = rxData[0];
    switch (cmd)
    {
      case BOOT_CMD_PING:
        Boot_CAN_HandlePing(rxData);
        break;

      case BOOT_CMD_START_UPGRADE:
        Boot_CAN_HandleStartUpgrade(rxData);
        break;

      case BOOT_CMD_ERASE_APP:
        Boot_CAN_HandleEraseApp(rxData);
        break;

      case BOOT_CMD_DATA_PACKET:
        Boot_CAN_HandleDataPacket(rxData);
        break;

      case BOOT_CMD_VERIFY_APP:
        Boot_CAN_HandleVerifyApp(rxData);
        break;

      case BOOT_CMD_RUN_APP:
        Boot_CAN_HandleRunApp(rxData);
        break;

      default:
        Boot_CAN_SendResponse(cmd, BOOT_ACK_ERR_PARAM, NULL, 0U);
        break;
    }
  }
}
