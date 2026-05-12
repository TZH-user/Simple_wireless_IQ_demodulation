#include "si5351_legacy_common.h"

#include "../../Config/app_si5351_variant.h"
#include "../../Port/my_IIC.h"

#define SI5351_REG_PLL_INPUT        15U
#define SI5351_REG_PLLA_PARAMS      26U
#define SI5351_REG_PLLB_PARAMS      34U
#define SI5351_REG_PLL_RESET        177U
#define SI5351_REG_OUTPUT_ENABLE    3U
#define SI5351_REG_CLK_CTRL_BASE    16U
#define SI5351_REG_STATUS0          0U
#define SI5351_REG_STATUS1          1U
#define SI5351_REG_STATUS2          2U
#define SI5351_REG_PHASE_BASE       165U

#define SI5351_CLK_CTRL_SRC_MS      0x0CU
#define SI5351_CLK_CTRL_PLLB        0x20U
#define SI5351_CLK_CTRL_MS_INT      0x40U
#define SI5351_CLK_CTRL_INV         0x10U
#define SI5351_CLK_CTRL_DRIVE_MASK  0x03U

static const uint8_t g_si5351_ms_base_addr[8] = {42U, 50U, 58U, 66U, 74U, 82U, 90U, 98U};

/* 该掩码与 SI5351 寄存器 3 保持同语义：位为 1 表示该通道关闭。 */
static uint8_t g_si5351_clk_enable_mask = 0xFFU;

static void si5351_pack_params(uint32_t p1, uint32_t p2, uint32_t p3, uint8_t regs[8])
{
    regs[0] = (uint8_t)((p3 >> 8) & 0xFFU);
    regs[1] = (uint8_t)(p3 & 0xFFU);
    regs[2] = (uint8_t)((p1 >> 16) & 0x03U);
    regs[3] = (uint8_t)((p1 >> 8) & 0xFFU);
    regs[4] = (uint8_t)(p1 & 0xFFU);
    regs[5] = (uint8_t)(((p3 >> 12) & 0xF0U) | ((p2 >> 16) & 0x0FU));
    regs[6] = (uint8_t)((p2 >> 8) & 0xFFU);
    regs[7] = (uint8_t)(p2 & 0xFFU);
}

static void si5351_write_regs(uint8_t start_reg, const uint8_t regs[8])
{
    uint8_t i;

    for (i = 0U; i < 8U; ++i)
    {
        I2C2_Write_REG(SI5351C.Slv_Address, (uint8_t)(start_reg + i), regs[i]);
    }
}

static uint32_t si5351_get_active_input_hz(bool use_clkin)
{
    return use_clkin ? APP_SI5351_CLKIN_HZ : APP_SI5351_XTAL_HZ;
}

/* 读取 reg15 的目的不是做运行时花式切换，而是让 PLL 重算时沿用当前已选参考源。 */
static bool si5351_pll_uses_clkin(bool use_pllb)
{
    uint8_t reg15 = I2C2_Read_REG(SI5351C.Slv_Address, SI5351_REG_PLL_INPUT);
    uint8_t mask = use_pllb ? 0x08U : 0x04U;
    return ((reg15 & mask) != 0U);
}

static void si5351_program_pll(bool use_pllb)
{
    uint32_t input_hz = si5351_get_active_input_hz(si5351_pll_uses_clkin(use_pllb));
    uint32_t mult = APP_SI5351_PLL_HZ / input_hz;
    uint32_t p1 = (128U * mult) - 512U;
    uint8_t regs[8];

    si5351_pack_params(p1, 0U, 1U, regs);
    si5351_write_regs(use_pllb ? SI5351_REG_PLLB_PARAMS : SI5351_REG_PLLA_PARAMS, regs);
}

/* common 层统一用“整数 + 分数”接口生成 MS 参数，避免三套变体各自重复拼寄存器。 */
static void si5351_program_ms(uint8_t channel, uint32_t integer_part, uint32_t numerator, uint32_t denominator)
{
    uint32_t frac_floor;
    uint32_t p1;
    uint32_t p2;
    uint32_t p3;
    uint8_t regs[8];
    uint8_t ctrl_reg;
    uint8_t ctrl;

    if (denominator == 0U)
    {
        denominator = 1U;
    }

    frac_floor = (128U * numerator) / denominator;
    p1 = (128U * integer_part) + frac_floor - 512U;
    p2 = (128U * numerator) - (denominator * frac_floor);
    p3 = denominator;

    si5351_pack_params(p1, p2, p3, regs);
    regs[2] |= 0x00U;
    si5351_write_regs(g_si5351_ms_base_addr[channel], regs);

    ctrl_reg = (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel);
    ctrl = I2C2_Read_REG(SI5351C.Slv_Address, ctrl_reg);
    ctrl &= (uint8_t)(~SI5351_CLK_CTRL_MS_INT);
    if (numerator == 0U)
    {
        ctrl |= SI5351_CLK_CTRL_MS_INT;
    }
    I2C2_Write_REG(SI5351C.Slv_Address, ctrl_reg, ctrl);
}

static void si5351_apply_default_clk_ctrl(void)
{
    uint8_t channel;

    for (channel = 0U; channel < 8U; ++channel)
    {
        /* 默认全挂到 MultiSynth；4~7 号通道按旧库习惯先归到 PLLB。 */
        uint8_t ctrl = (uint8_t)(SI5351_CLK_CTRL_SRC_MS | SI5351_CLK_CTRL_DRIVE_MASK);
        if (channel >= 4U)
        {
            ctrl |= SI5351_CLK_CTRL_PLLB;
        }
        I2C2_Write_REG(SI5351C.Slv_Address, (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel), ctrl);
    }
}

static uint8_t si5351_drive_to_bits(uint8_t ma)
{
    switch (ma)
    {
    case 2U:
        return 0x00U;
    case 4U:
        return 0x01U;
    case 6U:
        return 0x02U;
    default:
        return 0x03U;
    }
}

static uint8_t si5351_bits_to_drive(uint8_t bits)
{
    switch (bits & SI5351_CLK_CTRL_DRIVE_MASK)
    {
    case 0x00U:
        return 2U;
    case 0x01U:
        return 4U;
    case 0x02U:
        return 6U;
    default:
        return 8U;
    }
}

struct SI5351_struct SI5351C = {.Slv_Address = APP_SI5351_DEVICE_ADDR1};

void si5351_legacy_init_common(void)
{
    uint8_t pll_input = 0x00U;

    /* 先全关输出再重建 PLL/MS 配置，避免上电或重配阶段把未锁定时钟送到后级。 */
    g_si5351_clk_enable_mask = 0xFFU;
    I2C2_Write_REG(SI5351C.Slv_Address, SI5351_REG_OUTPUT_ENABLE, g_si5351_clk_enable_mask);
    I2C2_Write_REG(SI5351C.Slv_Address, SI5351_REG_STATUS0, 0x00U);

    if (APP_SI5351_USE_CLKIN != 0U)
    {
        pll_input = 0x0CU;
    }

    I2C2_Write_REG(SI5351C.Slv_Address, SI5351_REG_PLL_INPUT, pll_input);
    si5351_program_pll(false);
    si5351_program_pll(true);
    si5351_apply_default_clk_ctrl();
    I2C2_Write_REG(SI5351C.Slv_Address, SI5351_REG_PLL_RESET, 0xA0U);
}

void si5351_legacy_set_clock_source_common(uint8_t channel, SI5351_Source_t source)
{
    uint8_t reg;
    uint8_t cur;

    if (channel > 7U)
    {
        return;
    }

    reg = (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel);
    cur = I2C2_Read_REG(SI5351C.Slv_Address, reg);
    cur = (uint8_t)((cur & (uint8_t)(~0x0CU)) | (source & 0x0CU));
    I2C2_Write_REG(SI5351C.Slv_Address, reg, cur);
}

void si5351_legacy_set_pll_source_common(char pll_x, uint8_t use_clkin)
{
    uint8_t reg15 = I2C2_Read_REG(SI5351C.Slv_Address, SI5351_REG_PLL_INPUT);
    bool use_pllb;

    if ((pll_x != 'A') && (pll_x != 'B'))
    {
        return;
    }

    use_pllb = (pll_x == 'B');
    if (use_pllb)
    {
        reg15 = (uint8_t)(reg15 & (uint8_t)(~0x08U));
        if (use_clkin != 0U)
        {
            reg15 |= 0x08U;
        }
    }
    else
    {
        reg15 = (uint8_t)(reg15 & (uint8_t)(~0x04U));
        if (use_clkin != 0U)
        {
            reg15 |= 0x04U;
        }
    }

    I2C2_Write_REG(SI5351C.Slv_Address, SI5351_REG_PLL_INPUT, reg15);
    /* 切参考源后必须按新的输入频率重算 PLL，并触发对应 PLL reset。 */
    si5351_program_pll(use_pllb);
    I2C2_Write_REG(SI5351C.Slv_Address, SI5351_REG_PLL_RESET, use_pllb ? 0x80U : 0x20U);
}

bool si5351_legacy_is_external_clock_present_common(void)
{
    return ((I2C2_Read_REG(SI5351C.Slv_Address, SI5351_REG_STATUS0) & 0x10U) == 0U);
}

void si5351_legacy_set_frequency_common(uint8_t channel, uint32_t freq_hz)
{
    uint32_t pll_freq = APP_SI5351_PLL_HZ;
    double div_ratio;
    uint32_t integer_part;
    uint32_t denominator = 1048575U;
    uint32_t numerator = 0U;
    double frac;

    if ((channel > 7U) || (freq_hz == 0U))
    {
        return;
    }

    div_ratio = (double)pll_freq / (double)freq_hz;
    integer_part = (uint32_t)div_ratio;
    if (integer_part < 4U)
    {
        integer_part = 4U;
    }
    if (integer_part > 180U)
    {
        integer_part = 180U;
    }

    frac = div_ratio - (double)integer_part;
    if (frac > 0.0)
    {
        numerator = (uint32_t)(frac * (double)denominator + 0.5);
    }

    /* 这里默认保持当前通道已有 PLL 归属，只改分频参数。 */
    si5351_program_ms(channel, integer_part, numerator, denominator);
}

void si5351_legacy_force_channel_pll_common(uint8_t channel, int force_pll)
{
    uint8_t reg;
    uint8_t cur;

    if ((channel > 7U) || (force_pll < 0))
    {
        return;
    }

    reg = (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel);
    cur = I2C2_Read_REG(SI5351C.Slv_Address, reg);
    cur &= (uint8_t)(~SI5351_CLK_CTRL_PLLB);
    if (force_pll != 0)
    {
        cur |= SI5351_CLK_CTRL_PLLB;
    }
    I2C2_Write_REG(SI5351C.Slv_Address, reg, cur);
}

void si5351_legacy_set_frequency_choose_pll_common(uint8_t channel,
                                                   uint32_t integer_part,
                                                   uint32_t numerator,
                                                   int force_pll)
{
    /* 老接口历史上带 a/p2 参数，这里保留签名只为了兼容旧调用点。 */
    (void)integer_part;
    (void)numerator;
    si5351_legacy_force_channel_pll_common(channel, force_pll);
}

void si5351_legacy_set_phase_offset_common(uint8_t channel, double degrees)
{
    uint8_t raw;

    if (channel > 5U)
    {
        return;
    }

    while (degrees < 0.0)
    {
        degrees += 360.0;
    }
    while (degrees >= 360.0)
    {
        degrees -= 360.0;
    }

    raw = (uint8_t)(((degrees * 128.0) / 360.0) + 0.5);
    I2C2_Write_REG(SI5351C.Slv_Address, (uint8_t)(SI5351_REG_PHASE_BASE + channel), (uint8_t)(raw & 0x7FU));
}

uint8_t si5351_legacy_get_phase_offset_raw_common(uint8_t channel)
{
    if (channel > 5U)
    {
        return 0U;
    }
    return (uint8_t)(I2C2_Read_REG(SI5351C.Slv_Address, (uint8_t)(SI5351_REG_PHASE_BASE + channel)) & 0x7FU);
}

double si5351_legacy_get_phase_offset_degrees_common(uint8_t channel)
{
    return ((double)si5351_legacy_get_phase_offset_raw_common(channel) * 360.0) / 128.0;
}

void si5351_legacy_set_phase_and_enable_common(uint8_t channel, double degrees)
{
    si5351_legacy_set_phase_offset_common(channel, degrees);
    si5351_legacy_enable_channel_common(channel, true);
}

void si5351_legacy_set_drive_strength_common(uint8_t channel, SI5351_Drive_t strength)
{
    uint8_t reg;
    uint8_t cur;

    if (channel > 7U)
    {
        return;
    }

    reg = (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel);
    cur = I2C2_Read_REG(SI5351C.Slv_Address, reg);
    cur = (uint8_t)((cur & (uint8_t)(~SI5351_CLK_CTRL_DRIVE_MASK)) | si5351_drive_to_bits((uint8_t)strength));
    I2C2_Write_REG(SI5351C.Slv_Address, reg, cur);
}

uint8_t si5351_legacy_get_drive_strength_common(uint8_t channel)
{
    if (channel > 7U)
    {
        return 0U;
    }
    return si5351_bits_to_drive(I2C2_Read_REG(SI5351C.Slv_Address, (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel)));
}

void si5351_legacy_read_status_common(SI5351_Status *status)
{
    uint8_t i;

    if (status == NULL)
    {
        return;
    }

    status->status_reg0 = I2C2_Read_REG(SI5351C.Slv_Address, SI5351_REG_STATUS0);
    status->int_status_reg1 = I2C2_Read_REG(SI5351C.Slv_Address, SI5351_REG_STATUS1);
    status->sys_init_reg2 = I2C2_Read_REG(SI5351C.Slv_Address, SI5351_REG_STATUS2);
    status->output_enable_reg3 = I2C2_Read_REG(SI5351C.Slv_Address, SI5351_REG_OUTPUT_ENABLE);
    status->pll_reset_reg177 = I2C2_Read_REG(SI5351C.Slv_Address, SI5351_REG_PLL_RESET);
    status->clk_enable_mask = status->output_enable_reg3;
    for (i = 0U; i < 6U; ++i)
    {
        status->phase_reg[i] = si5351_legacy_get_phase_offset_raw_common(i);
    }
}

void si5351_legacy_set_invert_common(uint8_t channel, bool invert)
{
    uint8_t reg;
    uint8_t cur;

    if (channel > 7U)
    {
        return;
    }

    reg = (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel);
    cur = I2C2_Read_REG(SI5351C.Slv_Address, reg);
    if (invert)
    {
        cur |= SI5351_CLK_CTRL_INV;
    }
    else
    {
        cur = (uint8_t)(cur & (uint8_t)(~SI5351_CLK_CTRL_INV));
    }
    I2C2_Write_REG(SI5351C.Slv_Address, reg, cur);
}

bool si5351_legacy_get_invert_common(uint8_t channel)
{
    if (channel > 7U)
    {
        return false;
    }
    return ((I2C2_Read_REG(SI5351C.Slv_Address, (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel)) & SI5351_CLK_CTRL_INV) != 0U);
}

void si5351_legacy_enable_channel_common(uint8_t channel, bool enable)
{
    if (channel > 7U)
    {
        return;
    }

    /* SI5351 的输出使能寄存器是低有效语义，这里统一做一次翻译。 */
    if (enable)
    {
        g_si5351_clk_enable_mask &= (uint8_t)(~(1U << channel));
    }
    else
    {
        g_si5351_clk_enable_mask |= (uint8_t)(1U << channel);
    }

    I2C2_Write_REG(SI5351C.Slv_Address, SI5351_REG_OUTPUT_ENABLE, g_si5351_clk_enable_mask);
}

uint8_t si5351_legacy_get_clock_control_reg_common(uint8_t channel)
{
    if (channel > 7U)
    {
        return 0xFFU;
    }
    return I2C2_Read_REG(SI5351C.Slv_Address, (uint8_t)(SI5351_REG_CLK_CTRL_BASE + channel));
}
