#ifndef APP_BOARD_FLASH_H
#define APP_BOARD_FLASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_BOARD_FLASH_TOTAL_SIZE        (8UL * 1024UL * 1024UL)
#define APP_BOARD_FLASH_SECTOR_SIZE       4096UL
#define APP_BOARD_FLASH_BLOCK64_SIZE      (64UL * 1024UL)
#define APP_BOARD_FLASH_PAGE_SIZE         256UL
#define APP_BOARD_FLASH_EXPECTED_JEDEC_ID 0x00EF4017UL

typedef enum
{
  APP_BOARD_FLASH_OK = 0,
  APP_BOARD_FLASH_ERROR_ARG,
  APP_BOARD_FLASH_ERROR_RANGE,
  APP_BOARD_FLASH_ERROR_ALIGN,
  APP_BOARD_FLASH_ERROR_INIT,
  APP_BOARD_FLASH_ERROR_ID,
  APP_BOARD_FLASH_ERROR_BUSY,
  APP_BOARD_FLASH_ERROR_QSPI,
  APP_BOARD_FLASH_ERROR_VERIFY
} app_board_flash_result_t;

typedef struct
{
  app_board_flash_result_t status;
  uint32_t jedec_id;
  uint32_t fail_address;
  uint8_t expected;
  uint8_t actual;
} app_board_flash_test_result_t;

app_board_flash_result_t app_board_flash_init(void);
app_board_flash_result_t app_board_flash_read_id(uint32_t *jedec_id);
app_board_flash_result_t app_board_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
app_board_flash_result_t app_board_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len);
app_board_flash_result_t app_board_flash_erase_4k(uint32_t addr);
app_board_flash_result_t app_board_flash_erase_64k(uint32_t addr);
app_board_flash_result_t app_board_flash_self_test(uint32_t scratch_addr,
                                                   app_board_flash_test_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* APP_BOARD_FLASH_H */
