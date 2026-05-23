#include "app_board_flash.h"

#include "board_flash_w25q64.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define APP_BOARD_FLASH_SELF_TEST_LEN 320U

static bool g_board_flash_ready = false;

static app_board_flash_result_t app_board_flash_map_result(board_flash_w25q64_result_t result)
{
  switch (result) {
    case BOARD_FLASH_W25Q64_OK:
      return APP_BOARD_FLASH_OK;
    case BOARD_FLASH_W25Q64_ERROR_ARG:
      return APP_BOARD_FLASH_ERROR_ARG;
    case BOARD_FLASH_W25Q64_ERROR_INIT:
      return APP_BOARD_FLASH_ERROR_INIT;
    case BOARD_FLASH_W25Q64_ERROR_ID:
      return APP_BOARD_FLASH_ERROR_ID;
    case BOARD_FLASH_W25Q64_ERROR_BUSY:
      return APP_BOARD_FLASH_ERROR_BUSY;
    case BOARD_FLASH_W25Q64_ERROR_QSPI:
    default:
      return APP_BOARD_FLASH_ERROR_QSPI;
  }
}

static bool app_board_flash_range_ok(uint32_t addr, uint32_t len)
{
  if (len == 0U) {
    return true;
  }

  if (addr >= APP_BOARD_FLASH_TOTAL_SIZE) {
    return false;
  }

  return (len <= (APP_BOARD_FLASH_TOTAL_SIZE - addr));
}

static app_board_flash_result_t app_board_flash_ensure_ready(void)
{
  app_board_flash_result_t result;

  if (g_board_flash_ready) {
    return APP_BOARD_FLASH_OK;
  }

  result = app_board_flash_init();
  return result;
}

app_board_flash_result_t app_board_flash_init(void)
{
  app_board_flash_result_t result;

  result = app_board_flash_map_result(board_flash_w25q64_init());
  g_board_flash_ready = (result == APP_BOARD_FLASH_OK);

  return result;
}

app_board_flash_result_t app_board_flash_read_id(uint32_t *jedec_id)
{
  app_board_flash_result_t result;

  if (jedec_id == NULL) {
    return APP_BOARD_FLASH_ERROR_ARG;
  }

  if (!g_board_flash_ready) {
    result = app_board_flash_map_result(board_flash_w25q64_init());
    g_board_flash_ready = (result == APP_BOARD_FLASH_OK);
    if ((result != APP_BOARD_FLASH_OK) && (result != APP_BOARD_FLASH_ERROR_ID)) {
      return result;
    }
  }

  return app_board_flash_map_result(board_flash_w25q64_read_id(jedec_id));
}

app_board_flash_result_t app_board_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
  app_board_flash_result_t result;

  if (len == 0U) {
    return APP_BOARD_FLASH_OK;
  }

  if (buf == NULL) {
    return APP_BOARD_FLASH_ERROR_ARG;
  }

  if (!app_board_flash_range_ok(addr, len)) {
    return APP_BOARD_FLASH_ERROR_RANGE;
  }

  result = app_board_flash_ensure_ready();
  if (result != APP_BOARD_FLASH_OK) {
    return result;
  }

  return app_board_flash_map_result(board_flash_w25q64_read(addr, buf, len));
}

app_board_flash_result_t app_board_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
  app_board_flash_result_t result;

  if (len == 0U) {
    return APP_BOARD_FLASH_OK;
  }

  if (buf == NULL) {
    return APP_BOARD_FLASH_ERROR_ARG;
  }

  if (!app_board_flash_range_ok(addr, len)) {
    return APP_BOARD_FLASH_ERROR_RANGE;
  }

  result = app_board_flash_ensure_ready();
  if (result != APP_BOARD_FLASH_OK) {
    return result;
  }

  return app_board_flash_map_result(board_flash_w25q64_write(addr, buf, len));
}

app_board_flash_result_t app_board_flash_erase_4k(uint32_t addr)
{
  app_board_flash_result_t result;

  if ((addr % APP_BOARD_FLASH_SECTOR_SIZE) != 0U) {
    return APP_BOARD_FLASH_ERROR_ALIGN;
  }

  if (!app_board_flash_range_ok(addr, APP_BOARD_FLASH_SECTOR_SIZE)) {
    return APP_BOARD_FLASH_ERROR_RANGE;
  }

  result = app_board_flash_ensure_ready();
  if (result != APP_BOARD_FLASH_OK) {
    return result;
  }

  return app_board_flash_map_result(board_flash_w25q64_erase_4k(addr));
}

app_board_flash_result_t app_board_flash_erase_64k(uint32_t addr)
{
  app_board_flash_result_t result;

  if ((addr % APP_BOARD_FLASH_BLOCK64_SIZE) != 0U) {
    return APP_BOARD_FLASH_ERROR_ALIGN;
  }

  if (!app_board_flash_range_ok(addr, APP_BOARD_FLASH_BLOCK64_SIZE)) {
    return APP_BOARD_FLASH_ERROR_RANGE;
  }

  result = app_board_flash_ensure_ready();
  if (result != APP_BOARD_FLASH_OK) {
    return result;
  }

  return app_board_flash_map_result(board_flash_w25q64_erase_64k(addr));
}

app_board_flash_result_t app_board_flash_self_test(uint32_t scratch_addr,
                                                   app_board_flash_test_result_t *result)
{
  static uint8_t tx_buf[APP_BOARD_FLASH_SELF_TEST_LEN];
  static uint8_t rx_buf[APP_BOARD_FLASH_SELF_TEST_LEN];
  uint32_t index;
  app_board_flash_result_t op_result;

  if (result == NULL) {
    return APP_BOARD_FLASH_ERROR_ARG;
  }

  memset(result, 0, sizeof(*result));

  op_result = app_board_flash_read_id(&result->jedec_id);
  if (op_result != APP_BOARD_FLASH_OK) {
    result->status = op_result;
    return op_result;
  }

  if (result->jedec_id != APP_BOARD_FLASH_EXPECTED_JEDEC_ID) {
    result->status = APP_BOARD_FLASH_ERROR_ID;
    return APP_BOARD_FLASH_ERROR_ID;
  }

  op_result = app_board_flash_erase_4k(scratch_addr);
  if (op_result != APP_BOARD_FLASH_OK) {
    result->status = op_result;
    return op_result;
  }

  memset(rx_buf, 0, sizeof(rx_buf));
  op_result = app_board_flash_read(scratch_addr, rx_buf, sizeof(rx_buf));
  if (op_result != APP_BOARD_FLASH_OK) {
    result->status = op_result;
    return op_result;
  }

  for (index = 0U; index < sizeof(rx_buf); index++) {
    if (rx_buf[index] != 0xFFU) {
      result->status = APP_BOARD_FLASH_ERROR_VERIFY;
      result->fail_address = scratch_addr + index;
      result->expected = 0xFFU;
      result->actual = rx_buf[index];
      return APP_BOARD_FLASH_ERROR_VERIFY;
    }
  }

  for (index = 0U; index < sizeof(tx_buf); index++) {
    tx_buf[index] = (uint8_t)((index * 37U) + 0x5AU);
  }

  op_result = app_board_flash_write(scratch_addr, tx_buf, sizeof(tx_buf));
  if (op_result != APP_BOARD_FLASH_OK) {
    result->status = op_result;
    return op_result;
  }

  memset(rx_buf, 0, sizeof(rx_buf));
  op_result = app_board_flash_read(scratch_addr, rx_buf, sizeof(rx_buf));
  if (op_result != APP_BOARD_FLASH_OK) {
    result->status = op_result;
    return op_result;
  }

  for (index = 0U; index < sizeof(tx_buf); index++) {
    if (rx_buf[index] != tx_buf[index]) {
      result->status = APP_BOARD_FLASH_ERROR_VERIFY;
      result->fail_address = scratch_addr + index;
      result->expected = tx_buf[index];
      result->actual = rx_buf[index];
      return APP_BOARD_FLASH_ERROR_VERIFY;
    }
  }

  result->status = APP_BOARD_FLASH_OK;
  return APP_BOARD_FLASH_OK;
}
