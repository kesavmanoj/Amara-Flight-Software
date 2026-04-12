/*
 * Command_Parser.c
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
 */


#include "Command_Parser.h"
#include "Command_List.h"

#include "UART_Driver.h"
#include "Logger.h"
#include "Telemetry.h"

static char cmd_buffer[COMMAND_MAX_LINE_LENGTH];
static uint16_t cmd_index = 0;

static void CommandParser_ProcessByte(uint8_t byte)
{
	if ((byte == '\r') || (byte == '\n'))
	{
	    cmd_buffer[cmd_index] = '\0';

	    if (cmd_index > 0U)
	    {
	        (void)Command_DispatchLine(cmd_buffer);
	    }

	    cmd_index = 0U;
	    return;
	}

	if(cmd_index < (COMMAND_MAX_LINE_LENGTH - 1U)){
		cmd_buffer[cmd_index++] = (char)byte;
	} else {
		cmd_index = 0U;
		Logger_Warn("Command buffer overflow");
		(void)Telemetry_SendEventEx(TELEM_EVENT_COMMAND_OVERFLOW, COMMAND_MAX_LINE_LENGTH);
		(void)UART_WriteString("ERR: Command Overflow\r\n");
	}
}

// API Functions

void CommandParser_Process(void)
{
	uint8_t byte;

	while(UART_ReadByte(&byte)){
		CommandParser_ProcessByte(byte);
	}
}

void CommandParser_Init(void)
{
	cmd_index = 0U;
}
