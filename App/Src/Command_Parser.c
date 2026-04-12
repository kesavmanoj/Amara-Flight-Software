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

#include <string.h>

#define CMD_BUFFER_SIZE 128
#define MAX_TOKENS      5

static char cmd_buffer[CMD_BUFFER_SIZE];
static uint16_t cmd_index = 0;

static char *CommandParser_TrimWhitespace(char *input)
{
	char *end;

	while((*input == ' ') || (*input == '\t')){
		input++;
	}

	end = input + strlen(input);
	while((end > input) && ((end[-1] == ' ') || (end[-1] == '\t'))){
		end--;
	}
	*end = '\0';

	return input;
}

static int tokenize(char *input, char *argv[], int max_tokens){
	int argc = 0;
	char *token = strtok(input, " \t");

	while(token != NULL && argc < max_tokens){
		argv[argc++] = token;
		token = strtok(NULL, " \t");
	}

	return argc;
}

static void CommandParser_Execute(char* cmd_line){
	char *normalized_line = CommandParser_TrimWhitespace(cmd_line);
	char *argv[MAX_TOKENS];
	int argc = tokenize(normalized_line, argv, MAX_TOKENS);

	if(argc == 0) return;

	// Command Table
	for(uint32_t i = 0; i < command_count; i++){
		if(strcmp(argv[0], command_table[i].name) == 0){

			command_table[i].handler(argc, argv);
			return;

		}
	}

	Logger_Warn("Unknown command received: %s", argv[0]);
	(void)Telemetry_SendEventEx(TELEM_EVENT_COMMAND_UNKNOWN, 0U);
	UART_WriteString("ERR: Unknown Command\r\n");
}

static void CommandParser_ProcessByte(uint8_t byte){

	if (byte == '\r' || byte == '\n')
	{
	    cmd_buffer[cmd_index] = '\0';

	    if (cmd_index > 0)
	    {
	        CommandParser_Execute(cmd_buffer);
	    }

	    cmd_index = 0;
	    return;
	}

	if(cmd_index < CMD_BUFFER_SIZE - 1){
		cmd_buffer[cmd_index++] = (char)byte;
	} else {
		// Overflow error
		cmd_index = 0;
		Logger_Warn("Command buffer overflow");
		(void)Telemetry_SendEventEx(TELEM_EVENT_COMMAND_OVERFLOW, CMD_BUFFER_SIZE);
		UART_WriteString("ERR: Command Overflow\r\n");
	}
}

// API Functions

void CommandParser_Process(void){

	uint8_t byte;
	while(UART_ReadByte(&byte)){
		CommandParser_ProcessByte(byte);

	}
}

void CommandParser_Init(void){
	cmd_index = 0;
}




