#ifndef APP_WALLPAPER_H
#define APP_WALLPAPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_WALLPAPER_FLASH_ADDR              0x000000UL
#define APP_WALLPAPER_DATA_ADDR               0x001000UL
#define APP_WALLPAPER_MAGIC                   0x47424C57UL /* little-endian: WLBG */
#define APP_WALLPAPER_FORMAT_RGB565           1U
#define APP_WALLPAPER_VERSION                 1U
#define APP_WALLPAPER_FLASH_RESERVED_END_ADDR 0x0BD000UL

typedef enum
{
    APP_WALLPAPER_OK = 0,
    APP_WALLPAPER_NOT_PROGRAMMED,
    APP_WALLPAPER_FLASH_ERROR,
    APP_WALLPAPER_INVALID_HEADER,
    APP_WALLPAPER_CHECKSUM_ERROR
} app_wallpaper_result_t;

app_wallpaper_result_t app_wallpaper_load(void);
uint8_t app_wallpaper_is_available(void);
uint16_t *app_wallpaper_get_rgb565_buffer(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WALLPAPER_H */
