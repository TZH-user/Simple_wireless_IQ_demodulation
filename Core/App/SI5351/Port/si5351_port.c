#include "si5351_port.h"

#include "../Config/app_si5351_variant.h"
#include "my_IIC.h"

static HAL_StatusTypeDef g_app_si5351_port_error = HAL_OK;

/* 只锁存第一次错误，避免后续重试把最初的故障现场覆盖掉。 */
static void app_si5351_port_latch_error(HAL_StatusTypeDef status)
{
    if ((g_app_si5351_port_error == HAL_OK) && (status != HAL_OK))
    {
        g_app_si5351_port_error = status;
    }
}

#if APP_SI5351_PORT_MODE == APP_SI5351_PORT_SOFT_I2C
/* bit-bang 延时用空转保持纯 C 可移植性，后续只需按主频重新整定 cycles。 */
static void app_si5351_soft_i2c_delay(void)
{
    volatile uint32_t i;

    for (i = 0U; i < APP_SI5351_SOFT_I2C_DELAY_CYCLES; ++i)
    {
        __NOP();
    }
}

static void app_si5351_soft_i2c_sda_out(GPIO_PinState state)
{
    HAL_GPIO_WritePin(APP_SI5351_SOFT_I2C_SDA_PORT, APP_SI5351_SOFT_I2C_SDA_PIN, state);
}

static void app_si5351_soft_i2c_scl_out(GPIO_PinState state)
{
    HAL_GPIO_WritePin(APP_SI5351_SOFT_I2C_SCL_PORT, APP_SI5351_SOFT_I2C_SCL_PIN, state);
}

static void app_si5351_soft_i2c_start(void)
{
    app_si5351_soft_i2c_sda_out(GPIO_PIN_SET);
    app_si5351_soft_i2c_scl_out(GPIO_PIN_SET);
    app_si5351_soft_i2c_delay();
    app_si5351_soft_i2c_sda_out(GPIO_PIN_RESET);
    app_si5351_soft_i2c_delay();
    app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
}

static void app_si5351_soft_i2c_stop(void)
{
    app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
    app_si5351_soft_i2c_sda_out(GPIO_PIN_RESET);
    app_si5351_soft_i2c_delay();
    app_si5351_soft_i2c_scl_out(GPIO_PIN_SET);
    app_si5351_soft_i2c_delay();
    app_si5351_soft_i2c_sda_out(GPIO_PIN_SET);
    app_si5351_soft_i2c_delay();
}

static bool app_si5351_soft_i2c_write_byte(uint8_t data)
{
    uint8_t bit;

    for (bit = 0U; bit < 8U; ++bit)
    {
        app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
        app_si5351_soft_i2c_sda_out((data & 0x80U) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
        app_si5351_soft_i2c_delay();
        app_si5351_soft_i2c_scl_out(GPIO_PIN_SET);
        app_si5351_soft_i2c_delay();
        data <<= 1;
    }

    app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
    app_si5351_soft_i2c_sda_out(GPIO_PIN_SET);
    app_si5351_soft_i2c_delay();
    app_si5351_soft_i2c_scl_out(GPIO_PIN_SET);
    app_si5351_soft_i2c_delay();

    if (HAL_GPIO_ReadPin(APP_SI5351_SOFT_I2C_SDA_PORT, APP_SI5351_SOFT_I2C_SDA_PIN) != GPIO_PIN_RESET)
    {
        app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
        return false;
    }

    app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
    return true;
}

static uint8_t app_si5351_soft_i2c_read_byte(bool ack)
{
    uint8_t bit;
    uint8_t data = 0U;

    app_si5351_soft_i2c_sda_out(GPIO_PIN_SET);
    for (bit = 0U; bit < 8U; ++bit)
    {
        data <<= 1;
        app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
        app_si5351_soft_i2c_delay();
        app_si5351_soft_i2c_scl_out(GPIO_PIN_SET);
        app_si5351_soft_i2c_delay();
        if (HAL_GPIO_ReadPin(APP_SI5351_SOFT_I2C_SDA_PORT, APP_SI5351_SOFT_I2C_SDA_PIN) == GPIO_PIN_SET)
        {
            data |= 0x01U;
        }
    }

    app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
    app_si5351_soft_i2c_sda_out(ack ? GPIO_PIN_RESET : GPIO_PIN_SET);
    app_si5351_soft_i2c_delay();
    app_si5351_soft_i2c_scl_out(GPIO_PIN_SET);
    app_si5351_soft_i2c_delay();
    app_si5351_soft_i2c_scl_out(GPIO_PIN_RESET);
    app_si5351_soft_i2c_sda_out(GPIO_PIN_SET);

    return data;
}
#endif

HAL_StatusTypeDef app_si5351_port_probe(uint16_t *found_addr)
{
    static const uint16_t addr_candidates[] = {
        APP_SI5351_DEVICE_ADDR0,
        APP_SI5351_DEVICE_ADDR1,
    };
    uint32_t idx;

    if (found_addr == NULL)
    {
        return HAL_ERROR;
    }

    *found_addr = 0U;

#if APP_SI5351_PORT_MODE == APP_SI5351_PORT_HAL_I2C
    /* HAL I2C 模式优先用 IsDeviceReady，便于尽早区分“地址不通”和“寄存器不通”。 */
    for (idx = 0U; idx < (sizeof(addr_candidates) / sizeof(addr_candidates[0])); ++idx)
    {
        if (HAL_I2C_IsDeviceReady(&APP_SI5351_HAL_I2C_HANDLE,
                                  addr_candidates[idx],
                                  3U,
                                  APP_SI5351_I2C_TIMEOUT_MS) == HAL_OK)
        {
            *found_addr = addr_candidates[idx];
            return HAL_OK;
        }
    }
    return HAL_ERROR;
#else
    /* 软件 I2C 没有现成的 probe 接口，这里退回读状态寄存器来判断 ACK。 */
    for (idx = 0U; idx < (sizeof(addr_candidates) / sizeof(addr_candidates[0])); ++idx)
    {
        uint8_t dummy = 0U;
        if (app_si5351_port_read_reg(addr_candidates[idx], 0U, &dummy) == HAL_OK)
        {
            *found_addr = addr_candidates[idx];
            return HAL_OK;
        }
    }
    return HAL_ERROR;
#endif
}

HAL_StatusTypeDef app_si5351_port_read_reg(uint16_t dev_addr, uint8_t reg_addr, uint8_t *value)
{
#if APP_SI5351_PORT_MODE == APP_SI5351_PORT_HAL_I2C
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&APP_SI5351_HAL_I2C_HANDLE,
                                                dev_addr,
                                                reg_addr,
                                                I2C_MEMADD_SIZE_8BIT,
                                                value,
                                                1U,
                                                APP_SI5351_I2C_TIMEOUT_MS);
    app_si5351_port_latch_error(status);
    return status;
#else
    if (value == NULL)
    {
        app_si5351_port_latch_error(HAL_ERROR);
        return HAL_ERROR;
    }

    app_si5351_soft_i2c_start();
    if (!app_si5351_soft_i2c_write_byte((uint8_t)(dev_addr & 0xFEU)) ||
        !app_si5351_soft_i2c_write_byte(reg_addr))
    {
        app_si5351_soft_i2c_stop();
        app_si5351_port_latch_error(HAL_ERROR);
        return HAL_ERROR;
    }

    app_si5351_soft_i2c_start();
    if (!app_si5351_soft_i2c_write_byte((uint8_t)(dev_addr | 0x01U)))
    {
        app_si5351_soft_i2c_stop();
        app_si5351_port_latch_error(HAL_ERROR);
        return HAL_ERROR;
    }

    *value = app_si5351_soft_i2c_read_byte(false);
    app_si5351_soft_i2c_stop();
    return HAL_OK;
#endif
}

HAL_StatusTypeDef app_si5351_port_write_reg(uint16_t dev_addr, uint8_t reg_addr, uint8_t value)
{
#if APP_SI5351_PORT_MODE == APP_SI5351_PORT_HAL_I2C
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&APP_SI5351_HAL_I2C_HANDLE,
                                                 dev_addr,
                                                 reg_addr,
                                                 I2C_MEMADD_SIZE_8BIT,
                                                 &value,
                                                 1U,
                                                 APP_SI5351_I2C_TIMEOUT_MS);
    app_si5351_port_latch_error(status);
    return status;
#else
    app_si5351_soft_i2c_start();
    if (!app_si5351_soft_i2c_write_byte((uint8_t)(dev_addr & 0xFEU)) ||
        !app_si5351_soft_i2c_write_byte(reg_addr) ||
        !app_si5351_soft_i2c_write_byte(value))
    {
        app_si5351_soft_i2c_stop();
        app_si5351_port_latch_error(HAL_ERROR);
        return HAL_ERROR;
    }
    app_si5351_soft_i2c_stop();
    return HAL_OK;
#endif
}

HAL_StatusTypeDef app_si5351_port_write_block(uint16_t dev_addr, uint8_t start_reg, const uint8_t *data, uint16_t size)
{
#if APP_SI5351_PORT_MODE == APP_SI5351_PORT_HAL_I2C
    /* 连续写参数块主要服务于 PLL/MS 这类 8 字节寄存器窗口。 */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&APP_SI5351_HAL_I2C_HANDLE,
                                                 dev_addr,
                                                 start_reg,
                                                 I2C_MEMADD_SIZE_8BIT,
                                                 (uint8_t *)data,
                                                 size,
                                                 APP_SI5351_I2C_TIMEOUT_MS);
    app_si5351_port_latch_error(status);
    return status;
#else
    uint16_t i;

    if (data == NULL)
    {
        app_si5351_port_latch_error(HAL_ERROR);
        return HAL_ERROR;
    }

    app_si5351_soft_i2c_start();
    if (!app_si5351_soft_i2c_write_byte((uint8_t)(dev_addr & 0xFEU)) ||
        !app_si5351_soft_i2c_write_byte(start_reg))
    {
        app_si5351_soft_i2c_stop();
        app_si5351_port_latch_error(HAL_ERROR);
        return HAL_ERROR;
    }

    for (i = 0U; i < size; ++i)
    {
        if (!app_si5351_soft_i2c_write_byte(data[i]))
        {
            app_si5351_soft_i2c_stop();
            app_si5351_port_latch_error(HAL_ERROR);
            return HAL_ERROR;
        }
    }

    app_si5351_soft_i2c_stop();
    return HAL_OK;
#endif
}

void app_si5351_port_clear_error(void)
{
    g_app_si5351_port_error = HAL_OK;
}

HAL_StatusTypeDef app_si5351_port_get_error(void)
{
    return g_app_si5351_port_error;
}

const char *app_si5351_port_mode_name(void)
{
#if APP_SI5351_PORT_MODE == APP_SI5351_PORT_HAL_I2C
    return "hal_i2c";
#else
    return "soft_i2c";
#endif
}

/* 保留旧函数名，方便 legacy 代码直接复用，不必整体重写 I2C 访问层。 */
void I2C2_Write_REG(uint8_t dev_addr, uint8_t reg_addr, uint8_t value)
{
    (void)app_si5351_port_write_reg(dev_addr, reg_addr, value);
}

uint8_t I2C2_Read_REG(uint8_t dev_addr, uint8_t reg_addr)
{
    uint8_t value = 0U;

    (void)app_si5351_port_read_reg(dev_addr, reg_addr, &value);
    return value;
}
