#ifndef APP_SI5351_MY_IIC_H
#define APP_SI5351_MY_IIC_H

#include "main.h"

void I2C2_Write_REG(uint8_t dev_addr, uint8_t reg_addr, uint8_t value);
uint8_t I2C2_Read_REG(uint8_t dev_addr, uint8_t reg_addr);

#endif /* APP_SI5351_MY_IIC_H */
