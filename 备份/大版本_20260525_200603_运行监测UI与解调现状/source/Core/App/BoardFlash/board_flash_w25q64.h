#ifndef BOARD_FLASH_W25Q64_H
#define BOARD_FLASH_W25Q64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_FLASH_W25Q64_TOTAL_SIZE   (8UL * 1024UL * 1024UL)
#define BOARD_FLASH_W25Q64_SECTOR_SIZE  4096UL
#define BOARD_FLASH_W25Q64_BLOCK64_SIZE (64UL * 1024UL)
#define BOARD_FLASH_W25Q64_PAGE_SIZE    256UL
#define BOARD_FLASH_W25Q64_JEDEC_ID     0x00EF4017UL

typedef enum
{
  BOARD_FLASH_W25Q64_OK = 0,
  BOARD_FLASH_W25Q64_ERROR_ARG,
  BOARD_FLASH_W25Q64_ERROR_INIT,
  BOARD_FLASH_W25Q64_ERROR_ID,
  BOARD_FLASH_W25Q64_ERROR_BUSY,
  BOARD_FLASH_W25Q64_ERROR_QSPI
} board_flash_w25q64_result_t;

board_flash_w25q64_result_t board_flash_w25q64_init(void);
board_flash_w25q64_result_t board_flash_w25q64_read_id(uint32_t *jedec_id);
board_flash_w25q64_result_t board_flash_w25q64_read(uint32_t addr, uint8_t *buf, uint32_t len);
board_flash_w25q64_result_t board_flash_w25q64_write(uint32_t addr, const uint8_t *buf, uint32_t len);
board_flash_w25q64_result_t board_flash_w25q64_erase_4k(uint32_t addr);
board_flash_w25q64_result_t board_flash_w25q64_erase_64k(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_FLASH_W25Q64_H */
