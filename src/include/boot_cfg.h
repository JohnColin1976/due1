#ifndef BOOT_CFG_H
#define BOOT_CFG_H

#include <stdint.h>

#define FLASH_BASE_ADDR      (0x00080000u)
#define FLASH_SIZE_BYTES     (512u * 1024u)
#define FLASH_END_ADDR       (FLASH_BASE_ADDR + FLASH_SIZE_BYTES - 1u)

#define BL_SIZE_BYTES        (32u * 1024u)
#define APP_START_ADDR       (FLASH_BASE_ADDR + BL_SIZE_BYTES)

#define GPBR_INDEX           (0u)
#define MAGIC_UPDATE         (0xB00710ADu)

#define BL_CMD_INFO          (0x01u)
#define BL_CMD_ERASE         (0x02u)
#define BL_CMD_WRITE         (0x03u)
#define BL_CMD_VERIFY        (0x04u)
#define BL_CMD_RUN           (0x05u)
#define BL_CMD_ABORT         (0x06u)

#define BL_ACK               (0x79u)
#define BL_NAK               (0x1Fu)

#define BL_FRAME_MAGIC       (0xB10Cu)
#define BL_FRAME_VERSION     (1u)
#define BL_FRAME_MAX_PAYLOAD (2048u)

#define BL_ERR_INVALID_COMMAND (0x01u)
#define BL_ERR_INVALID_RANGE   (0x02u)
#define BL_ERR_INVALID_LENGTH  (0x03u)
#define BL_ERR_CRC_MISMATCH    (0x04u)
#define BL_ERR_FLASH_PROGRAM   (0x05u)
#define BL_ERR_FLASH_ERASE     (0x06u)
#define BL_ERR_TIMEOUT         (0x07u)
#define BL_ERR_VERIFY_FAILED   (0x08u)
#define BL_ERR_INTERNAL_STATE  (0x09u)

#define BL_CHUNK_SIZE        (1024u)
#define BL_SYNC_WAIT_MS      (150u)
#define BL_DATA_TIMEOUT_MS   (2000u)

/* protocol_v1 COMMAND extension */
#define ECU_CMD_ENTER_BOOT   (8u)

#endif /* BOOT_CFG_H */
