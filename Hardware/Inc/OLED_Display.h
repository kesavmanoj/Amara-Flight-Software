/*
 * OLED_Display.h
 *
 * SSD1306 I2C OLED driver for 128x64 panels.
 *
 * This driver keeps a full local framebuffer in RAM and updates the panel
 * explicitly through OLED_UpdateScreen(). Drawing APIs only modify the local
 * framebuffer; they do not talk to the display until an update is requested.
 */

#ifndef INC_OLED_DISPLAY_H_
#define INC_OLED_DISPLAY_H_

#include "I2C_Bus.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OLED_SSD1306_WIDTH              128U
#define OLED_SSD1306_HEIGHT             64U
#define OLED_SSD1306_PAGE_COUNT         (OLED_SSD1306_HEIGHT / 8U)
#define OLED_SSD1306_FRAMEBUFFER_SIZE   (OLED_SSD1306_WIDTH * OLED_SSD1306_PAGE_COUNT)

#define OLED_I2C_ADDR_0x3C              0x3CU
#define OLED_I2C_ADDR_0x3D              0x3DU

#define OLED_FONT_WIDTH                 5U
#define OLED_FONT_HEIGHT                8U
#define OLED_FONT_SPACING               1U
#define OLED_CHAR_WIDTH                 (OLED_FONT_WIDTH + OLED_FONT_SPACING)

typedef enum
{
    OLED_STATUS_OK = 0,
    OLED_STATUS_INVALID_PARAM,
    OLED_STATUS_NOT_INITIALIZED,
    OLED_STATUS_I2C_ERROR
} OLED_Status_t;

typedef enum
{
    OLED_COLOR_BLACK = 0,
    OLED_COLOR_WHITE
} OLED_Color_t;

typedef enum
{
    OLED_CONTROLLER_SSD1306 = 0
} OLED_Controller_t;

typedef struct
{
    I2C_Bus_Handle_t *bus;
    uint8_t i2c_addr_7bit;
    uint8_t width;
    uint8_t height;
    uint8_t page_count;
    uint8_t cursor_x;
    uint8_t cursor_y;
    bool initialized;
    bool inverted;
    OLED_Controller_t controller;
    uint8_t framebuffer[OLED_SSD1306_FRAMEBUFFER_SIZE];
} OLED_HandleTypeDef;

OLED_Status_t OLED_Init(OLED_HandleTypeDef *oled,
                        I2C_Bus_Handle_t *bus,
                        uint8_t i2c_addr_7bit);

OLED_Status_t OLED_WriteCommand(OLED_HandleTypeDef *oled, uint8_t command);
OLED_Status_t OLED_WriteData(OLED_HandleTypeDef *oled, const uint8_t *data, uint16_t length);

OLED_Status_t OLED_UpdateScreen(OLED_HandleTypeDef *oled);
OLED_Status_t OLED_Clear(OLED_HandleTypeDef *oled);
OLED_Status_t OLED_Fill(OLED_HandleTypeDef *oled, OLED_Color_t color);
OLED_Status_t OLED_SetCursor(OLED_HandleTypeDef *oled, uint8_t x, uint8_t y);
OLED_Status_t OLED_DrawPixel(OLED_HandleTypeDef *oled, uint8_t x, uint8_t y, OLED_Color_t color);
OLED_Status_t OLED_WriteChar(OLED_HandleTypeDef *oled, char character, OLED_Color_t color);
OLED_Status_t OLED_WriteString(OLED_HandleTypeDef *oled, const char *string, OLED_Color_t color);
OLED_Status_t OLED_SetDisplayInverted(OLED_HandleTypeDef *oled, bool invert);

#endif /* INC_OLED_DISPLAY_H_ */
