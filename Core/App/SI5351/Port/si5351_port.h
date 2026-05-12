#ifndef APP_SI5351_PORT_H
#define APP_SI5351_PORT_H

#include "main.h"
#include <stdbool.h>

/* 自动探测 0x60/0x61，适配不同板级对 A0 的拉法。 */
HAL_StatusTypeDef app_si5351_port_probe(uint16_t *found_addr);
HAL_StatusTypeDef app_si5351_port_read_reg(uint16_t dev_addr, uint8_t reg_addr, uint8_t *value);
HAL_StatusTypeDef app_si5351_port_write_reg(uint16_t dev_addr, uint8_t reg_addr, uint8_t value);
HAL_StatusTypeDef app_si5351_port_write_block(uint16_t dev_addr, uint8_t start_reg, const uint8_t *data, uint16_t size);
/* 端口层会锁存首次 HAL 错误，便于上层把 fault 日志和真正的底层故障对应起来。 */
void app_si5351_port_clear_error(void);
HAL_StatusTypeDef app_si5351_port_get_error(void);
const char *app_si5351_port_mode_name(void);

#endif /* APP_SI5351_PORT_H */
