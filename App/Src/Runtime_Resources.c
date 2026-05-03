/*
 * Runtime_Resources.c
 *
 *  Created on: 14-Apr-2026
 *      Author: Codex
 */

#include "Runtime_Resources.h"
#include <string.h>

typedef struct {
    I2C_Bus_Handle_t i2c_bus;
    OLED_HandleTypeDef oled;
    SX1278_Handle_t radio;
    G2S_Link_Handle_t g2s_link;
    IPMS_Config_t ipms_config;
    SystemRuntimeContext_t runtime_context;
    SystemRuntimeHooks_t runtime_hooks;
} RuntimeResources_Data_t;

static RuntimeResources_Data_t g_runtime_resources;

void RuntimeResources_Init(void)
{
    memset(&g_runtime_resources, 0, sizeof(g_runtime_resources));
}

I2C_Bus_Handle_t *RuntimeResources_GetI2CBus(void)
{
    return &g_runtime_resources.i2c_bus;
}

OLED_HandleTypeDef *RuntimeResources_GetOled(void)
{
    return &g_runtime_resources.oled;
}

SX1278_Handle_t *RuntimeResources_GetRadio(void)
{
    return &g_runtime_resources.radio;
}

G2S_Link_Handle_t *RuntimeResources_GetG2SLink(void)
{
    return &g_runtime_resources.g2s_link;
}

IPMS_Config_t *RuntimeResources_GetIpmsConfig(void)
{
    return &g_runtime_resources.ipms_config;
}

SystemRuntimeContext_t *RuntimeResources_GetSystemRuntimeContext(void)
{
    return &g_runtime_resources.runtime_context;
}

SystemRuntimeHooks_t *RuntimeResources_GetSystemRuntimeHooks(void)
{
    return &g_runtime_resources.runtime_hooks;
}
