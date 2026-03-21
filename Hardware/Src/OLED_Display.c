/*
 * OledDisplay.c
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#include <OLED_Display.h>

/* Private helper functions */

static void OLED_WriteData(OLED_HandleTypeDef *oled, uint8_t *data, uint16_t size)
{
    HAL_I2C_Mem_Write(
        oled->hi2c,
        OLED_I2C_ADDR,
        0x40,
        1,
        data,
        size,
        HAL_MAX_DELAY
    );
}

static void OLED_WriteCommand(OLED_HandleTypeDef *oled, uint8_t cmd)
{
    HAL_I2C_Mem_Write(
        oled->hi2c,
        OLED_I2C_ADDR,
        0x00,
        1,
        &cmd,
        1,
        HAL_MAX_DELAY
    );
}

/* Public driver functions */

void OLED_Init(OLED_HandleTypeDef *oled)
{
    uint8_t cmd;

    cmd = 0xAE; OLED_WriteCommand(oled, cmd);
    cmd = 0x20; OLED_WriteCommand(oled, cmd);
    cmd = 0x00; OLED_WriteCommand(oled, cmd);
    cmd = 0xAF; OLED_WriteCommand(oled, cmd);
}

void OLED_Clear(OLED_HandleTypeDef *oled)
{
    uint8_t buffer[128] = {0};

    for(int page = 0; page < 8; page++)
    {
        OLED_WriteCommand(oled, 0xB0 + page);
        OLED_WriteCommand(oled, 0x00);
        OLED_WriteCommand(oled, 0x10);
        OLED_WriteData(oled, buffer, 128);
    }
}

void OLED_DisplayHI(OLED_HandleTypeDef *oled)
{
    uint8_t data[] =
    {
        0xFF,0x18,0x18,0xFF,0x00,
        0xFF,0x81,0x81,0xFF
    };

    OLED_WriteCommand(oled,0xB0);
    OLED_WriteCommand(oled,0x00);
    OLED_WriteCommand(oled,0x10);

    OLED_WriteData(oled,data,sizeof(data));
}
