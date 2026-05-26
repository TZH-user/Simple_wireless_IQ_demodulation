#include "board_flash_qspi_port.h"

#include "quadspi.h"

HAL_StatusTypeDef board_flash_qspi_port_init(void)
{
  MX_QUADSPI_Init();
  return HAL_OK;
}

QSPI_HandleTypeDef *board_flash_qspi_port_handle(void)
{
  return &hqspi;
}

HAL_StatusTypeDef board_flash_qspi_port_command(QSPI_CommandTypeDef *cmd, uint32_t timeout_ms)
{
  return HAL_QSPI_Command(board_flash_qspi_port_handle(), cmd, timeout_ms);
}

HAL_StatusTypeDef board_flash_qspi_port_transmit(uint8_t *data, uint32_t timeout_ms)
{
  return HAL_QSPI_Transmit(board_flash_qspi_port_handle(), data, timeout_ms);
}

HAL_StatusTypeDef board_flash_qspi_port_receive(uint8_t *data, uint32_t timeout_ms)
{
  return HAL_QSPI_Receive(board_flash_qspi_port_handle(), data, timeout_ms);
}

HAL_StatusTypeDef board_flash_qspi_port_auto_polling(QSPI_CommandTypeDef *cmd,
                                                     QSPI_AutoPollingTypeDef *cfg,
                                                     uint32_t timeout_ms)
{
  return HAL_QSPI_AutoPolling(board_flash_qspi_port_handle(), cmd, cfg, timeout_ms);
}
