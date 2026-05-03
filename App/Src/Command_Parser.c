/**
 * @file Command_Parser.c
 * @brief Console UART byte-stream parser implementation.
 */


#include "Command_Parser.h"
#include "Command_List.h"

#include "UART_Driver.h"
#include "Logger.h"
#include "Telemetry.h"

static char cmd_buffer[COMMAND_MAX_LINE_LENGTH];
static uint16_t cmd_index = 0;

/**
 * @brief Consume one console byte and update the current command-line buffer.
 *
 * Bytes are appended until a line terminator arrives. At that point the buffered line
 * is dispatched through @ref Command_DispatchLine. Overflow resets the line buffer and
 * reports the condition through logging, telemetry, and a console error response.
 */
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

/**
 * @copydoc CommandParser_Process
 *
 * Runtime ownership note:
 * - UART_RxCpltCallback() only queues bytes
 * - CommTask later consumes queued bytes in task context through this function
 */
void CommandParser_Process(void)
{
	uint8_t byte;

	while(UART_ReadByte(&byte)){
		CommandParser_ProcessByte(byte);
	}
}

/** @copydoc CommandParser_Init */
void CommandParser_Init(void)
{
	cmd_index = 0U;
}
