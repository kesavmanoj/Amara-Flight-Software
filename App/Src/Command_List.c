/*
 * Command_List.c
 *
 *  Created on: 21-Mar-2026
 *      Author: KESAV
 */
#include "Command_List.h"
#include "Command_Parser.h"
#include "UART_Driver.h"
#include "Logger.h"
#include "ADC_Monitor.h"
#include "Telemetry.h"


#include <stdlib.h>
#include <stdio.h>

#define CMD_ID_PING     0x01U
#define CMD_ID_GET_ADC  0x02U
#define CMD_ID_SET_RATE 0x03U

static UART_Driver_Status_t Command_WriteResponse(const char *response)
{
	UART_Driver_Status_t status = UART_WriteString(response);

	if(status != UART_DRIVER_OK){
		Logger_Warn("Command response write failed: %s", UART_Driver_StatusToString(status));
	}

	return status;
}

static void Command_SendAck(uint8_t command_id, int8_t status_code, uint32_t argument)
{
	Telemetry_Status_t status = Telemetry_SendCommandAckEx(command_id, status_code, argument);

	if(status != TELEM_STATUS_OK){
		Logger_Warn("Command ACK queue failed: %s", Telemetry_StatusToString(status));
	}
}

static void CMD_PING(int argc, char *argv[]){
	(void)argc;
	(void)argv;

	(void)Command_WriteResponse("PONG\r\n");
	Command_SendAck(CMD_ID_PING, 0, 0U);

}

static void CMD_GET_ADC(int argc, char *argv[]){

	ADC_HealthData_t data;
	ADC_Monitor_Status_t adc_status;

	(void)argc;
	(void)argv;

	adc_status = ADC_Monitor_GetData(&data);

	if(adc_status == ADC_MONITOR_OK){

		char response[128];
		snprintf(response, sizeof(response),
				 "VDDA=%.3f, TEMP=%.2f, BATT=%.3f\r\n",
				 data.vdda_voltage, data.mcu_temp_c, data.battery_voltage);

		(void)Command_WriteResponse(response);
		Command_SendAck(CMD_ID_GET_ADC, 0, 0U);

	} else {

		(void)Command_WriteResponse("ERR: ADC Not Ready\r\n");
		Command_SendAck(CMD_ID_GET_ADC, -1, (uint32_t)adc_status);

	}

}

static void CMD_SET_RATE(int argc, char *argv[]){
	char *endptr = NULL;

	if(argc < 2){
		(void)Command_WriteResponse("ERR: Missing Argument\r\n");
		Command_SendAck(CMD_ID_SET_RATE, -1, 0U);
		return;
	}

	long rate = strtol(argv[1], &endptr, 10);
	if((endptr == argv[1]) || (*endptr != '\0')){
		(void)Command_WriteResponse("ERR: Invalid Argument\r\n");
		Command_SendAck(CMD_ID_SET_RATE, -2, 0U);
		return;
	}

	if((rate < 1L) || (rate > 1000L)){
		(void)Command_WriteResponse("ERR: Rate Out Of Range\r\n");
		Command_SendAck(CMD_ID_SET_RATE, -2, 0U);
		return;
	}

	char msg[64];
	snprintf(msg, sizeof(msg), "Rate set to %ld\r\n", rate);
	(void)Command_WriteResponse(msg);
	Command_SendAck(CMD_ID_SET_RATE, 0, (uint32_t)rate);

}

// COMMAND TABLE

const CommandEntry_t command_table[] = {
		{"PING"		, 		CMD_PING	 },
		{"GET_ADC"	, 		CMD_GET_ADC	 },
		{"SET_RATE"	, 		CMD_SET_RATE }
};

const uint32_t command_count =
    sizeof(command_table) / sizeof(CommandEntry_t);
