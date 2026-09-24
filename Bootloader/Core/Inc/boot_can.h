#ifndef __BOOT_CAN_H__
#define __BOOT_CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

#define BOOT_CAN_CMD_ID             0x04700000U
#define BOOT_CAN_RESP_ID            0x04700001U

#define BOOT_CMD_PING               0x01U
#define BOOT_CMD_START_UPGRADE      0x02U
#define BOOT_CMD_ERASE_APP          0x03U
#define BOOT_CMD_DATA_PACKET        0x04U
#define BOOT_CMD_VERIFY_APP         0x05U
#define BOOT_CMD_RUN_APP            0x06U

#define BOOT_ACK_OK                 0x00U
#define BOOT_ACK_ERR_PARAM          0x01U
#define BOOT_ACK_ERR_ERASE          0x02U
#define BOOT_ACK_ERR_WRITE          0x03U
#define BOOT_ACK_ERR_CRC            0x04U
#define BOOT_ACK_ERR_SEQ            0x05U

typedef enum
{
  BOOT_STATE_IDLE = 0,
  BOOT_STATE_READY,
  BOOT_STATE_RECEIVING,
  BOOT_STATE_VERIFIED
} BootState_t;

extern FDCAN_HandleTypeDef hfdcan2;
extern volatile BootState_t g_bootState;
extern volatile uint8_t g_bootForceStay;

void Boot_CAN_Init(void);
void Boot_CAN_Process(void);
HAL_StatusTypeDef Boot_CAN_SendResponse(uint8_t cmd, uint8_t ack, const uint8_t *payload, uint8_t payloadLen);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_CAN_H__ */
