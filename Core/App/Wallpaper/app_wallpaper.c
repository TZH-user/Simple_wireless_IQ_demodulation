#include "app_wallpaper.h"

#include "../app_memory_map.h"
#include "../BoardFlash/app_board_flash.h"

#include <stddef.h>
#include <string.h>

#define APP_WALLPAPER_CHECKSUM_SEED 2166136261UL
#define APP_WALLPAPER_READ_CHUNK    4096UL

#if (APP_WALLPAPER_FLASH_RESERVED_END_ADDR > (APP_BOARD_FLASH_TOTAL_SIZE - (2UL * APP_BOARD_FLASH_SECTOR_SIZE)))
#error "Wallpaper region overlaps persisted calibration sectors"
#endif

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t pixel_format;
    uint16_t width;
    uint16_t height;
    uint32_t data_size;
    uint32_t checksum;
} app_wallpaper_header_t;

static uint8_t g_wallpaper_available = 0U;

static uint32_t app_wallpaper_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t checksum = APP_WALLPAPER_CHECKSUM_SEED;
    uint32_t index;

    for (index = 0UL; index < len; index++)
    {
        checksum ^= (uint32_t)data[index];
        checksum *= 16777619UL;
    }

    return checksum;
}

app_wallpaper_result_t app_wallpaper_load(void)
{
    app_wallpaper_header_t header;
    uint8_t *destination = (uint8_t *)APP_UI_WALLPAPER_BUF_ADDR;
    uint32_t offset;
    uint32_t chunk_len;

    g_wallpaper_available = 0U;
    memset(&header, 0, sizeof(header));
    if (app_board_flash_read(APP_WALLPAPER_FLASH_ADDR, (uint8_t *)&header, sizeof(header)) != APP_BOARD_FLASH_OK)
    {
        return APP_WALLPAPER_FLASH_ERROR;
    }

    if (header.magic == 0xFFFFFFFFUL)
    {
        return APP_WALLPAPER_NOT_PROGRAMMED;
    }

    if ((header.magic != APP_WALLPAPER_MAGIC) ||
        (header.version != APP_WALLPAPER_VERSION) ||
        (header.pixel_format != APP_WALLPAPER_FORMAT_RGB565) ||
        (header.width != APP_LCD_HOR_RES) ||
        (header.height != APP_LCD_VER_RES) ||
        (header.data_size != APP_UI_WALLPAPER_BUF_SIZE_BYTES))
    {
        return APP_WALLPAPER_INVALID_HEADER;
    }

    for (offset = 0UL; offset < header.data_size; offset += chunk_len)
    {
        chunk_len = header.data_size - offset;
        if (chunk_len > APP_WALLPAPER_READ_CHUNK)
        {
            chunk_len = APP_WALLPAPER_READ_CHUNK;
        }

        if (app_board_flash_read(APP_WALLPAPER_DATA_ADDR + offset, destination + offset, chunk_len) != APP_BOARD_FLASH_OK)
        {
            return APP_WALLPAPER_FLASH_ERROR;
        }
    }

    if (app_wallpaper_checksum(destination, header.data_size) != header.checksum)
    {
        return APP_WALLPAPER_CHECKSUM_ERROR;
    }

    g_wallpaper_available = 1U;
    return APP_WALLPAPER_OK;
}

uint8_t app_wallpaper_is_available(void)
{
    return g_wallpaper_available;
}

uint16_t *app_wallpaper_get_rgb565_buffer(void)
{
    return (uint16_t *)APP_UI_WALLPAPER_BUF_ADDR;
}
