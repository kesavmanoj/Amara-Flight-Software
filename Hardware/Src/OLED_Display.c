/*
 * OLED_Display.c
 *
 * SSD1306 I2C OLED driver for 128x64 panels.
 */

#include "OLED_Display.h"

#include <string.h>

#define OLED_CONTROL_BYTE_COMMAND   0x00U
#define OLED_CONTROL_BYTE_DATA      0x40U
#define OLED_I2C_DATA_CHUNK_SIZE    32U

#define OLED_CMD_DISPLAY_OFF        0xAEU
#define OLED_CMD_DISPLAY_ON         0xAFU
#define OLED_CMD_SET_CLOCK_DIV      0xD5U
#define OLED_CMD_SET_MULTIPLEX      0xA8U
#define OLED_CMD_SET_OFFSET         0xD3U
#define OLED_CMD_SET_START_LINE     0x40U
#define OLED_CMD_ENABLE_CHARGE_PUMP 0x8DU
#define OLED_CMD_SET_MEMORY_MODE    0x20U
#define OLED_CMD_SET_SEG_REMAP      0xA1U
#define OLED_CMD_SET_COM_SCAN_DEC   0xC8U
#define OLED_CMD_SET_COM_PINS       0xDAU
#define OLED_CMD_SET_CONTRAST       0x81U
#define OLED_CMD_SET_PRECHARGE      0xD9U
#define OLED_CMD_SET_VCOMH          0xDBU
#define OLED_CMD_RESUME_RAM_DISPLAY 0xA4U
#define OLED_CMD_NORMAL_DISPLAY     0xA6U
#define OLED_CMD_INVERT_DISPLAY     0xA7U
#define OLED_CMD_DEACTIVATE_SCROLL  0x2EU

_Static_assert((OLED_SSD1306_HEIGHT % 8U) == 0U, "OLED height must be divisible by 8");

static const uint8_t oled_font_5x7[95][5] =
{
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5FU, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7FU, 0x14, 0x7FU, 0x14},
    {0x24, 0x2AU, 0x7FU, 0x2AU, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1CU, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1CU, 0x00},
    {0x14, 0x08, 0x3EU, 0x08, 0x14}, {0x08, 0x08, 0x3EU, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F}, {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02}, {0x08, 0x14, 0x54, 0x54, 0x3C},
    {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00}, {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7F, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x08, 0x04, 0x08, 0x10, 0x08}
};

static OLED_Status_t OLED_ValidateConfiguredHandle(const OLED_HandleTypeDef *oled)
{
    if ((oled == NULL) || (oled->bus == NULL) || (oled->bus->hi2c == NULL) || (oled->i2c_addr_7bit > 0x7FU))
    {
        return OLED_STATUS_INVALID_PARAM;
    }

    return OLED_STATUS_OK;
}

static OLED_Status_t OLED_ValidateReadyHandle(const OLED_HandleTypeDef *oled)
{
    OLED_Status_t status = OLED_ValidateConfiguredHandle(oled);

    if (status != OLED_STATUS_OK)
    {
        return status;
    }

    if (!oled->initialized)
    {
        return OLED_STATUS_NOT_INITIALIZED;
    }

    return OLED_STATUS_OK;
}

static OLED_Status_t OLED_ConvertBusStatus(I2C_Status_t bus_status)
{
    return (bus_status == I2C_OK) ? OLED_STATUS_OK : OLED_STATUS_I2C_ERROR;
}

static OLED_Status_t OLED_Transmit(OLED_HandleTypeDef *oled, const uint8_t *buffer, uint16_t length)
{
    OLED_Status_t status = OLED_ValidateConfiguredHandle(oled);

    if ((status != OLED_STATUS_OK) || (buffer == NULL) || (length == 0U))
    {
        return OLED_STATUS_INVALID_PARAM;
    }

    return OLED_ConvertBusStatus(
        I2C_Bus_Write(oled->bus,
                      oled->i2c_addr_7bit,
                      buffer,
                      length));
}

static OLED_Status_t OLED_WriteCommandList(OLED_HandleTypeDef *oled,
                                           const uint8_t *commands,
                                           uint16_t length)
{
    uint16_t offset = 0U;
    uint16_t chunk_len;
    uint8_t tx_buffer[1U + OLED_I2C_DATA_CHUNK_SIZE];

    if ((commands == NULL) || (length == 0U))
    {
        return OLED_STATUS_INVALID_PARAM;
    }

    while (offset < length)
    {
        chunk_len = length - offset;
        if (chunk_len > OLED_I2C_DATA_CHUNK_SIZE)
        {
            chunk_len = OLED_I2C_DATA_CHUNK_SIZE;
        }

        tx_buffer[0] = OLED_CONTROL_BYTE_COMMAND;
        memcpy(&tx_buffer[1], &commands[offset], chunk_len);

        if (OLED_Transmit(oled, tx_buffer, (uint16_t)(chunk_len + 1U)) != OLED_STATUS_OK)
        {
            return OLED_STATUS_I2C_ERROR;
        }

        offset = (uint16_t)(offset + chunk_len);
    }

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_Init(OLED_HandleTypeDef *oled,
                        I2C_Bus_Handle_t *bus,
                        uint8_t i2c_addr_7bit)
{
    static const uint8_t init_sequence[] =
    {
        OLED_CMD_DISPLAY_OFF,
        OLED_CMD_SET_CLOCK_DIV,      0x80U,
        OLED_CMD_SET_MULTIPLEX,      0x3FU,
        OLED_CMD_SET_OFFSET,         0x00U,
        OLED_CMD_SET_START_LINE,
        OLED_CMD_ENABLE_CHARGE_PUMP, 0x14U,
        OLED_CMD_SET_MEMORY_MODE,    0x02U,
        OLED_CMD_SET_SEG_REMAP,
        OLED_CMD_SET_COM_SCAN_DEC,
        OLED_CMD_SET_COM_PINS,       0x12U,
        OLED_CMD_SET_CONTRAST,       0x7FU,
        OLED_CMD_SET_PRECHARGE,      0xF1U,
        OLED_CMD_SET_VCOMH,          0x40U,
        OLED_CMD_RESUME_RAM_DISPLAY,
        OLED_CMD_NORMAL_DISPLAY,
        OLED_CMD_DEACTIVATE_SCROLL,
        OLED_CMD_DISPLAY_ON
    };

    if ((oled == NULL) || (bus == NULL) || (bus->hi2c == NULL) || (i2c_addr_7bit > 0x7FU))
    {
        return OLED_STATUS_INVALID_PARAM;
    }

    memset(oled, 0, sizeof(*oled));
    oled->bus = bus;
    oled->i2c_addr_7bit = i2c_addr_7bit;
    oled->width = OLED_SSD1306_WIDTH;
    oled->height = OLED_SSD1306_HEIGHT;
    oled->page_count = OLED_SSD1306_PAGE_COUNT;
    oled->controller = OLED_CONTROLLER_SSD1306;

    if (I2C_Bus_IsDeviceReady(bus, oled->i2c_addr_7bit, 2U) != I2C_OK)
    {
        return OLED_STATUS_I2C_ERROR;
    }

    if (OLED_WriteCommandList(oled, init_sequence, sizeof(init_sequence)) != OLED_STATUS_OK)
    {
        return OLED_STATUS_I2C_ERROR;
    }

    oled->initialized = true;

    if (OLED_Clear(oled) != OLED_STATUS_OK)
    {
        oled->initialized = false;
        return OLED_STATUS_I2C_ERROR;
    }

    if (OLED_UpdateScreen(oled) != OLED_STATUS_OK)
    {
        oled->initialized = false;
        return OLED_STATUS_I2C_ERROR;
    }

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_WriteCommand(OLED_HandleTypeDef *oled, uint8_t command)
{
    uint8_t tx_buffer[2];

    tx_buffer[0] = OLED_CONTROL_BYTE_COMMAND;
    tx_buffer[1] = command;

    return OLED_Transmit(oled, tx_buffer, sizeof(tx_buffer));
}

OLED_Status_t OLED_WriteData(OLED_HandleTypeDef *oled, const uint8_t *data, uint16_t length)
{
    uint16_t offset = 0U;
    uint16_t chunk_len;
    uint8_t tx_buffer[1U + OLED_I2C_DATA_CHUNK_SIZE];

    if ((OLED_ValidateConfiguredHandle(oled) != OLED_STATUS_OK) || (data == NULL))
    {
        return OLED_STATUS_INVALID_PARAM;
    }

    while (offset < length)
    {
        chunk_len = length - offset;
        if (chunk_len > OLED_I2C_DATA_CHUNK_SIZE)
        {
            chunk_len = OLED_I2C_DATA_CHUNK_SIZE;
        }

        tx_buffer[0] = OLED_CONTROL_BYTE_DATA;
        memcpy(&tx_buffer[1], &data[offset], chunk_len);

        if (OLED_Transmit(oled, tx_buffer, (uint16_t)(chunk_len + 1U)) != OLED_STATUS_OK)
        {
            return OLED_STATUS_I2C_ERROR;
        }

        offset = (uint16_t)(offset + chunk_len);
    }

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_UpdateScreen(OLED_HandleTypeDef *oled)
{
    uint8_t page;
    uint8_t commands[3];
    OLED_Status_t status = OLED_ValidateReadyHandle(oled);

    if (status != OLED_STATUS_OK)
    {
        return status;
    }

    for (page = 0U; page < oled->page_count; page++)
    {
        commands[0] = (uint8_t)(0xB0U | page);
        commands[1] = 0x00U;
        commands[2] = 0x10U;

        status = OLED_WriteCommandList(oled, commands, sizeof(commands));
        if (status != OLED_STATUS_OK)
        {
            return status;
        }

        status = OLED_WriteData(oled,
                                &oled->framebuffer[(uint16_t)page * oled->width],
                                oled->width);
        if (status != OLED_STATUS_OK)
        {
            return status;
        }
    }

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_Clear(OLED_HandleTypeDef *oled)
{
    return OLED_Fill(oled, OLED_COLOR_BLACK);
}

OLED_Status_t OLED_Fill(OLED_HandleTypeDef *oled, OLED_Color_t color)
{
    OLED_Status_t status = OLED_ValidateReadyHandle(oled);

    if (status != OLED_STATUS_OK)
    {
        return status;
    }

    memset(oled->framebuffer,
           (color == OLED_COLOR_WHITE) ? 0xFF : 0x00,
           sizeof(oled->framebuffer));

    oled->cursor_x = 0U;
    oled->cursor_y = 0U;

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_SetCursor(OLED_HandleTypeDef *oled, uint8_t x, uint8_t y)
{
    OLED_Status_t status = OLED_ValidateReadyHandle(oled);

    if (status != OLED_STATUS_OK)
    {
        return status;
    }

    if ((x >= oled->width) || (y >= oled->height))
    {
        return OLED_STATUS_INVALID_PARAM;
    }

    oled->cursor_x = x;
    oled->cursor_y = y;

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_DrawPixel(OLED_HandleTypeDef *oled, uint8_t x, uint8_t y, OLED_Color_t color)
{
    uint16_t index;
    uint8_t bit_mask;
    OLED_Status_t status = OLED_ValidateReadyHandle(oled);

    if (status != OLED_STATUS_OK)
    {
        return status;
    }

    if ((x >= oled->width) || (y >= oled->height))
    {
        return OLED_STATUS_INVALID_PARAM;
    }

    index = (uint16_t)x + ((uint16_t)(y / 8U) * oled->width);
    bit_mask = (uint8_t)(1U << (y % 8U));

    if (color == OLED_COLOR_WHITE)
    {
        oled->framebuffer[index] |= bit_mask;
    }
    else
    {
        oled->framebuffer[index] &= (uint8_t)~bit_mask;
    }

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_WriteChar(OLED_HandleTypeDef *oled, char character, OLED_Color_t color)
{
    uint8_t glyph_index;
    uint8_t column;
    uint8_t row;
    uint8_t glyph_column;
    OLED_Status_t status = OLED_ValidateReadyHandle(oled);

    if (status != OLED_STATUS_OK)
    {
        return status;
    }

    if ((character < 32) || (character > 126))
    {
        character = '?';
    }

    if ((uint16_t)oled->cursor_x + OLED_CHAR_WIDTH > oled->width)
    {
        oled->cursor_x = 0U;
        oled->cursor_y = (uint8_t)(oled->cursor_y + OLED_FONT_HEIGHT);
    }

    if ((uint16_t)oled->cursor_y + OLED_FONT_HEIGHT > oled->height)
    {
        return OLED_STATUS_INVALID_PARAM;
    }

    glyph_index = (uint8_t)(character - 32);

    for (column = 0U; column < OLED_CHAR_WIDTH; column++)
    {
        glyph_column = (column < OLED_FONT_WIDTH) ? oled_font_5x7[glyph_index][column] : 0x00U;

        for (row = 0U; row < OLED_FONT_HEIGHT; row++)
        {
            OLED_Color_t pixel_color = ((glyph_column & (1U << row)) != 0U)
                                         ? color
                                         : OLED_COLOR_BLACK;

            status = OLED_DrawPixel(oled,
                                    (uint8_t)(oled->cursor_x + column),
                                    (uint8_t)(oled->cursor_y + row),
                                    pixel_color);
            if (status != OLED_STATUS_OK)
            {
                return status;
            }
        }
    }

    oled->cursor_x = (uint8_t)(oled->cursor_x + OLED_CHAR_WIDTH);

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_WriteString(OLED_HandleTypeDef *oled, const char *string, OLED_Color_t color)
{
    OLED_Status_t status = OLED_ValidateReadyHandle(oled);

    if ((status != OLED_STATUS_OK) || (string == NULL))
    {
        return (status == OLED_STATUS_OK) ? OLED_STATUS_INVALID_PARAM : status;
    }

    while (*string != '\0')
    {
        if (*string == '\r')
        {
            string++;
            continue;
        }

        if (*string == '\n')
        {
            oled->cursor_x = 0U;
            oled->cursor_y = (uint8_t)(oled->cursor_y + OLED_FONT_HEIGHT);

            if (oled->cursor_y >= oled->height)
            {
                return OLED_STATUS_INVALID_PARAM;
            }

            string++;
            continue;
        }

        status = OLED_WriteChar(oled, *string, color);
        if (status != OLED_STATUS_OK)
        {
            return status;
        }

        string++;
    }

    return OLED_STATUS_OK;
}

OLED_Status_t OLED_SetDisplayInverted(OLED_HandleTypeDef *oled, bool invert)
{
    OLED_Status_t status = OLED_ValidateReadyHandle(oled);

    if (status != OLED_STATUS_OK)
    {
        return status;
    }

    status = OLED_WriteCommand(oled, invert ? OLED_CMD_INVERT_DISPLAY : OLED_CMD_NORMAL_DISPLAY);
    if (status == OLED_STATUS_OK)
    {
        oled->inverted = invert;
    }

    return status;
}
