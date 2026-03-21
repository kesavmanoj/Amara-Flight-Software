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


#include <stdlib.h>
#include <stdio.h>

static void CMD_PING(int argc, char *argv[]){

	UART_WriteString("PONG\r\n");

}

static void CMD_GET_ADC(int argc, char *argv[]){

	ADC_HealthData_t data;

	if(ADC_Monitor_GetData(&data) == ADC_MONITOR_OK){

		char response[128];
		snprintf(response, sizeof(response),
				 "VDDA=%.3f, TEMP=%.2f, BATT=%.3f\r\n",
				 data.vdda_voltage, data.mcu_temp_c, data.battery_voltage);

		UART_WriteString(response);

	} else {

		UART_WriteString("ADC Not Ready\r\n");

	}

}

static void CMD_SET_RATE(int argc, char *argv[]){

	if(argc < 2){
		UART_WriteString("ERR: Missing Argument");
	}

	int rate = atoi(argv[1]);

	char msg[64];
	snprintf(msg, sizeof(msg), "Rate set to %d", rate);
	UART_WriteString(msg);

}

// COMMAND TABLE

const CommandEntry_t command_table[] = {
		{"PING"		, 		CMD_PING	 },
		{"GET_ADC"	, 		CMD_GET_ADC	 },
		{"SET_RATE"	, 		CMD_SET_RATE }
};

const uint32_t command_count =
    sizeof(command_table) / sizeof(CommandEntry_t);
