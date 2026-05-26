#ifndef APP_SI5351_LEGACY_COMMON_H
#define APP_SI5351_LEGACY_COMMON_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* 保留旧库里的全局设备句柄命名，减少 legacy 代码迁移改动。 */
struct SI5351_struct {
    uint8_t Slv_Address;
};

typedef enum {
    SI5351_SRC_XTAL  = 0x00,
    SI5351_SRC_CLKIN = 0x04,
    SI5351_SRC_MS    = 0x0C
} SI5351_Source_t;

typedef enum {
    SI5351_DRIVE_2MA = 2,
    SI5351_DRIVE_4MA = 4,
    SI5351_DRIVE_6MA = 6,
    SI5351_DRIVE_8MA = 8
} SI5351_Drive_t;

typedef struct {
    uint8_t status_reg0;
    uint8_t int_status_reg1;
    uint8_t sys_init_reg2;
    uint8_t output_enable_reg3;
    uint8_t pll_reset_reg177;
    uint8_t clk_enable_mask;
    uint8_t phase_reg[6];
} SI5351_Status;

extern struct SI5351_struct SI5351C;

/* common 层承接真正的寄存器逻辑，basic/pro/promax 只负责导出不同能力的旧接口。 */
void si5351_legacy_init_common(void);
void si5351_legacy_set_clock_source_common(uint8_t channel, SI5351_Source_t source);
void si5351_legacy_set_pll_source_common(char pll_x, uint8_t use_clkin);
bool si5351_legacy_is_external_clock_present_common(void);
void si5351_legacy_set_frequency_common(uint8_t channel, uint32_t freq_hz);
void si5351_legacy_set_frequency_choose_pll_common(uint8_t channel,
                                                   uint32_t integer_part,
                                                   uint32_t numerator,
                                                   int force_pll);
void si5351_legacy_set_phase_offset_common(uint8_t channel, double degrees);
uint8_t si5351_legacy_get_phase_offset_raw_common(uint8_t channel);
double si5351_legacy_get_phase_offset_degrees_common(uint8_t channel);
void si5351_legacy_set_phase_and_enable_common(uint8_t channel, double degrees);
void si5351_legacy_set_drive_strength_common(uint8_t channel, SI5351_Drive_t strength);
uint8_t si5351_legacy_get_drive_strength_common(uint8_t channel);
void si5351_legacy_read_status_common(SI5351_Status *status);
void si5351_legacy_set_invert_common(uint8_t channel, bool invert);
bool si5351_legacy_get_invert_common(uint8_t channel);
void si5351_legacy_enable_channel_common(uint8_t channel, bool enable);
uint8_t si5351_legacy_get_clock_control_reg_common(uint8_t channel);
void si5351_legacy_force_channel_pll_common(uint8_t channel, int force_pll);

#endif /* APP_SI5351_LEGACY_COMMON_H */
