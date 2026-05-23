#include "board_flash_w25q64.h"

#include "board_flash_qspi_port.h"

#include <string.h>

#define W25Q64_CMD_WRITE_ENABLE       0x06U
#define W25Q64_CMD_READ_STATUS_REG1   0x05U
#define W25Q64_CMD_PAGE_PROGRAM       0x02U
#define W25Q64_CMD_READ_DATA          0x03U
#define W25Q64_CMD_SECTOR_ERASE_4K    0x20U
#define W25Q64_CMD_BLOCK_ERASE_64K    0xD8U
#define W25Q64_CMD_READ_JEDEC_ID      0x9FU
#define W25Q64_CMD_ENABLE_RESET       0x66U
#define W25Q64_CMD_RESET_DEVICE       0x99U

#define W25Q64_STATUS_BUSY            0x01U
#define W25Q64_STATUS_WEL             0x02U

#define W25Q64_CMD_TIMEOUT_MS         1000U
#define W25Q64_WRITE_TIMEOUT_MS       1000U
#define W25Q64_ERASE_4K_TIMEOUT_MS    1000U
#define W25Q64_ERASE_64K_TIMEOUT_MS   5000U

static void w25q64_prepare_command(QSPI_CommandTypeDef *cmd)
{
  memset(cmd, 0, sizeof(*cmd));
  cmd->InstructionMode = QSPI_INSTRUCTION_1_LINE;
  cmd->AddressSize = QSPI_ADDRESS_24_BITS;
  cmd->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  cmd->DdrMode = QSPI_DDR_MODE_DISABLE;
  cmd->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
  cmd->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
}

static board_flash_w25q64_result_t w25q64_send_simple_cmd(uint8_t instruction)
{
  QSPI_CommandTypeDef cmd;

  w25q64_prepare_command(&cmd);
  cmd.Instruction = instruction;
  cmd.AddressMode = QSPI_ADDRESS_NONE;
  cmd.DataMode = QSPI_DATA_NONE;

  if (board_flash_qspi_port_command(&cmd, W25Q64_CMD_TIMEOUT_MS) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_QSPI;
  }

  return BOARD_FLASH_W25Q64_OK;
}

static board_flash_w25q64_result_t w25q64_wait_status_match(uint8_t match,
                                                            uint8_t mask,
                                                            uint32_t timeout_ms)
{
  QSPI_CommandTypeDef cmd;
  QSPI_AutoPollingTypeDef cfg;

  w25q64_prepare_command(&cmd);
  cmd.Instruction = W25Q64_CMD_READ_STATUS_REG1;
  cmd.AddressMode = QSPI_ADDRESS_NONE;
  cmd.DataMode = QSPI_DATA_1_LINE;
  cmd.NbData = 1U;

  memset(&cfg, 0, sizeof(cfg));
  cfg.Match = match;
  cfg.Mask = mask;
  cfg.MatchMode = QSPI_MATCH_MODE_AND;
  cfg.StatusBytesSize = 1U;
  cfg.Interval = 0x10U;
  cfg.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

  if (board_flash_qspi_port_auto_polling(&cmd, &cfg, timeout_ms) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_BUSY;
  }

  return BOARD_FLASH_W25Q64_OK;
}

static board_flash_w25q64_result_t w25q64_wait_ready(uint32_t timeout_ms)
{
  return w25q64_wait_status_match(0U, W25Q64_STATUS_BUSY, timeout_ms);
}

static board_flash_w25q64_result_t w25q64_write_enable(void)
{
  board_flash_w25q64_result_t result;

  result = w25q64_send_simple_cmd(W25Q64_CMD_WRITE_ENABLE);
  if (result != BOARD_FLASH_W25Q64_OK) {
    return result;
  }

  return w25q64_wait_status_match(W25Q64_STATUS_WEL, W25Q64_STATUS_WEL, W25Q64_CMD_TIMEOUT_MS);
}

static board_flash_w25q64_result_t w25q64_reset(void)
{
  board_flash_w25q64_result_t result;

  result = w25q64_send_simple_cmd(W25Q64_CMD_ENABLE_RESET);
  if (result != BOARD_FLASH_W25Q64_OK) {
    return result;
  }

  result = w25q64_send_simple_cmd(W25Q64_CMD_RESET_DEVICE);
  if (result != BOARD_FLASH_W25Q64_OK) {
    return result;
  }

  return w25q64_wait_ready(W25Q64_CMD_TIMEOUT_MS);
}

static board_flash_w25q64_result_t w25q64_erase(uint32_t addr,
                                                uint8_t instruction,
                                                uint32_t timeout_ms)
{
  QSPI_CommandTypeDef cmd;
  board_flash_w25q64_result_t result;

  result = w25q64_write_enable();
  if (result != BOARD_FLASH_W25Q64_OK) {
    return result;
  }

  w25q64_prepare_command(&cmd);
  cmd.Instruction = instruction;
  cmd.AddressMode = QSPI_ADDRESS_1_LINE;
  cmd.Address = addr;
  cmd.DataMode = QSPI_DATA_NONE;

  if (board_flash_qspi_port_command(&cmd, W25Q64_CMD_TIMEOUT_MS) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_QSPI;
  }

  return w25q64_wait_ready(timeout_ms);
}

static board_flash_w25q64_result_t w25q64_write_page(uint32_t addr,
                                                     const uint8_t *buf,
                                                     uint32_t len)
{
  QSPI_CommandTypeDef cmd;
  board_flash_w25q64_result_t result;

  if ((buf == 0) || (len == 0U) || (len > BOARD_FLASH_W25Q64_PAGE_SIZE)) {
    return BOARD_FLASH_W25Q64_ERROR_ARG;
  }

  result = w25q64_write_enable();
  if (result != BOARD_FLASH_W25Q64_OK) {
    return result;
  }

  w25q64_prepare_command(&cmd);
  cmd.Instruction = W25Q64_CMD_PAGE_PROGRAM;
  cmd.AddressMode = QSPI_ADDRESS_1_LINE;
  cmd.Address = addr;
  cmd.DataMode = QSPI_DATA_1_LINE;
  cmd.NbData = len;

  if (board_flash_qspi_port_command(&cmd, W25Q64_CMD_TIMEOUT_MS) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_QSPI;
  }

  if (board_flash_qspi_port_transmit((uint8_t *)buf, W25Q64_WRITE_TIMEOUT_MS) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_QSPI;
  }

  return w25q64_wait_ready(W25Q64_WRITE_TIMEOUT_MS);
}

board_flash_w25q64_result_t board_flash_w25q64_init(void)
{
  uint32_t jedec_id = 0U;
  board_flash_w25q64_result_t result;

  if (board_flash_qspi_port_init() != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_INIT;
  }

  result = w25q64_reset();
  if (result != BOARD_FLASH_W25Q64_OK) {
    return result;
  }

  result = board_flash_w25q64_read_id(&jedec_id);
  if (result != BOARD_FLASH_W25Q64_OK) {
    return result;
  }

  if (jedec_id != BOARD_FLASH_W25Q64_JEDEC_ID) {
    return BOARD_FLASH_W25Q64_ERROR_ID;
  }

  return BOARD_FLASH_W25Q64_OK;
}

board_flash_w25q64_result_t board_flash_w25q64_read_id(uint32_t *jedec_id)
{
  QSPI_CommandTypeDef cmd;
  uint8_t id_bytes[3];

  if (jedec_id == 0) {
    return BOARD_FLASH_W25Q64_ERROR_ARG;
  }

  w25q64_prepare_command(&cmd);
  cmd.Instruction = W25Q64_CMD_READ_JEDEC_ID;
  cmd.AddressMode = QSPI_ADDRESS_NONE;
  cmd.DataMode = QSPI_DATA_1_LINE;
  cmd.NbData = sizeof(id_bytes);

  if (board_flash_qspi_port_command(&cmd, W25Q64_CMD_TIMEOUT_MS) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_QSPI;
  }

  if (board_flash_qspi_port_receive(id_bytes, W25Q64_CMD_TIMEOUT_MS) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_QSPI;
  }

  *jedec_id = ((uint32_t)id_bytes[0] << 16) |
              ((uint32_t)id_bytes[1] << 8) |
              (uint32_t)id_bytes[2];

  return BOARD_FLASH_W25Q64_OK;
}

board_flash_w25q64_result_t board_flash_w25q64_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
  QSPI_CommandTypeDef cmd;

  if (len == 0U) {
    return BOARD_FLASH_W25Q64_OK;
  }

  if (buf == 0) {
    return BOARD_FLASH_W25Q64_ERROR_ARG;
  }

  w25q64_prepare_command(&cmd);
  cmd.Instruction = W25Q64_CMD_READ_DATA;
  cmd.AddressMode = QSPI_ADDRESS_1_LINE;
  cmd.Address = addr;
  cmd.DataMode = QSPI_DATA_1_LINE;
  cmd.NbData = len;

  if (board_flash_qspi_port_command(&cmd, W25Q64_CMD_TIMEOUT_MS) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_QSPI;
  }

  if (board_flash_qspi_port_receive(buf, W25Q64_CMD_TIMEOUT_MS) != HAL_OK) {
    return BOARD_FLASH_W25Q64_ERROR_QSPI;
  }

  return BOARD_FLASH_W25Q64_OK;
}

board_flash_w25q64_result_t board_flash_w25q64_write(uint32_t addr,
                                                     const uint8_t *buf,
                                                     uint32_t len)
{
  uint32_t current_addr = addr;
  uint32_t remaining = len;
  const uint8_t *current_buf = buf;

  if (len == 0U) {
    return BOARD_FLASH_W25Q64_OK;
  }

  if (buf == 0) {
    return BOARD_FLASH_W25Q64_ERROR_ARG;
  }

  while (remaining > 0U) {
    uint32_t page_remain = BOARD_FLASH_W25Q64_PAGE_SIZE -
                           (current_addr % BOARD_FLASH_W25Q64_PAGE_SIZE);
    uint32_t write_len = (remaining < page_remain) ? remaining : page_remain;
    board_flash_w25q64_result_t result = w25q64_write_page(current_addr, current_buf, write_len);

    if (result != BOARD_FLASH_W25Q64_OK) {
      return result;
    }

    current_addr += write_len;
    current_buf += write_len;
    remaining -= write_len;
  }

  return BOARD_FLASH_W25Q64_OK;
}

board_flash_w25q64_result_t board_flash_w25q64_erase_4k(uint32_t addr)
{
  return w25q64_erase(addr, W25Q64_CMD_SECTOR_ERASE_4K, W25Q64_ERASE_4K_TIMEOUT_MS);
}

board_flash_w25q64_result_t board_flash_w25q64_erase_64k(uint32_t addr)
{
  return w25q64_erase(addr, W25Q64_CMD_BLOCK_ERASE_64K, W25Q64_ERASE_64K_TIMEOUT_MS);
}
