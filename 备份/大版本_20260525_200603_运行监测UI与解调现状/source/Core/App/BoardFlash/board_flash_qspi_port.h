#ifndef BOARD_FLASH_QSPI_PORT_H
#define BOARD_FLASH_QSPI_PORT_H

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

HAL_StatusTypeDef board_flash_qspi_port_init(void);
QSPI_HandleTypeDef *board_flash_qspi_port_handle(void);
HAL_StatusTypeDef board_flash_qspi_port_command(QSPI_CommandTypeDef *cmd, uint32_t timeout_ms);
HAL_StatusTypeDef board_flash_qspi_port_transmit(uint8_t *data, uint32_t timeout_ms);
HAL_StatusTypeDef board_flash_qspi_port_receive(uint8_t *data, uint32_t timeout_ms);
HAL_StatusTypeDef board_flash_qspi_port_auto_polling(QSPI_CommandTypeDef *cmd,
                                                     QSPI_AutoPollingTypeDef *cfg,
                                                     uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_FLASH_QSPI_PORT_H */
