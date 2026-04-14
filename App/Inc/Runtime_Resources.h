/*
 * Runtime_Resources.h
 *
 *  Created on: 14-Apr-2026
 *      Author: Codex
 */

#ifndef INC_RUNTIME_RESOURCES_H_
#define INC_RUNTIME_RESOURCES_H_

#include "I2C_Bus.h"
#include "OLED_Display.h"
#include "SX1278.h"
#include "G2S_Link.h"
#include "IPMS.h"
#include "System_Runtime.h"

void RuntimeResources_Init(void);

I2C_Bus_Handle_t *RuntimeResources_GetI2CBus(void);
OLED_HandleTypeDef *RuntimeResources_GetOled(void);
SX1278_Handle_t *RuntimeResources_GetRadio(void);
G2S_Link_Handle_t *RuntimeResources_GetG2SLink(void);
IPMS_Config_t *RuntimeResources_GetIpmsConfig(void);
SystemRuntimeContext_t *RuntimeResources_GetSystemRuntimeContext(void);
SystemRuntimeHooks_t *RuntimeResources_GetSystemRuntimeHooks(void);

#endif /* INC_RUNTIME_RESOURCES_H_ */
