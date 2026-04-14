/*
 * Command_List.c
 *
 *  Created on: 21-Mar-2026
 *      Author: KESAV
 */
#include "Command_List.h"
#include "UART_Driver.h"
#include "Logger.h"
#include "ADC_Monitor.h"
#include "Telemetry.h"
#include "IPMS.h"
#include "Storage_Service.h"
#include "bsp_driver_sd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CMD_ID_UNKNOWN  0x00U
#define CMD_ID_PING     0x01U
#define CMD_ID_GET_ADC  0x02U
#define CMD_ID_SET_RATE 0x03U
#define CMD_ID_PWR_STATUS 0x10U
#define CMD_ID_PWR_SIM    0x11U
#define CMD_ID_PWR_POLICY 0x12U
#define CMD_ID_SD_STATUS  0x20U
#define CMD_ID_SD_TEST    0x21U
#define MAX_TOKENS      5

static char *Command_TrimWhitespace(char *input)
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

static int Command_Tokenize(char *input, char *argv[], int max_tokens)
{
	int argc = 0;
	char *token = strtok(input, " \t");

	while((token != NULL) && (argc < max_tokens)){
		argv[argc++] = token;
		token = strtok(NULL, " \t");
	}

	return argc;
}

static UART_Driver_Status_t Command_WriteResponse(const char *response)
{
	UART_Driver_Status_t status = UART_WriteString(response);

	if(status != UART_DRIVER_OK){
		Logger_Warn("Command response write failed: %s", UART_Driver_StatusToString(status));
	}

	return status;
}

static const char *Command_FatFsResultToString(FRESULT result)
{
	switch(result){
	case FR_OK:
		return "FR_OK";
	case FR_DISK_ERR:
		return "FR_DISK_ERR";
	case FR_INT_ERR:
		return "FR_INT_ERR";
	case FR_NOT_READY:
		return "FR_NOT_READY";
	case FR_NO_FILE:
		return "FR_NO_FILE";
	case FR_NO_PATH:
		return "FR_NO_PATH";
	case FR_INVALID_NAME:
		return "FR_INVALID_NAME";
	case FR_DENIED:
		return "FR_DENIED";
	case FR_EXIST:
		return "FR_EXIST";
	case FR_INVALID_OBJECT:
		return "FR_INVALID_OBJECT";
	case FR_WRITE_PROTECTED:
		return "FR_WRITE_PROTECTED";
	case FR_INVALID_DRIVE:
		return "FR_INVALID_DRIVE";
	case FR_NOT_ENABLED:
		return "FR_NOT_ENABLED";
	case FR_NO_FILESYSTEM:
		return "FR_NO_FILESYSTEM";
	case FR_MKFS_ABORTED:
		return "FR_MKFS_ABORTED";
	case FR_TIMEOUT:
		return "FR_TIMEOUT";
	case FR_LOCKED:
		return "FR_LOCKED";
	case FR_NOT_ENOUGH_CORE:
		return "FR_NOT_ENOUGH_CORE";
	case FR_TOO_MANY_OPEN_FILES:
		return "FR_TOO_MANY_OPEN_FILES";
	case FR_INVALID_PARAMETER:
	default:
		return "FR_INVALID_PARAMETER";
	}
}

static void Command_InitResult(Command_Result_t *result, uint8_t command_id)
{
	if(result == NULL){
		return;
	}

	result->command_id = command_id;
	result->ack_status_code = -127;
	result->argument = 0U;
	result->status = COMMAND_STATUS_UNKNOWN;
}

static void Command_SendAck(const Command_Result_t *result)
{
	Telemetry_Status_t status;

	if(result == NULL){
		return;
	}

	status = Telemetry_SendCommandAckEx(result->command_id, result->ack_status_code, result->argument);
	if(status != TELEM_STATUS_OK){
		Logger_Warn("Command ACK queue failed: %s", Telemetry_StatusToString(status));
	}
}

static Command_Status_t CMD_PING(int argc, char *argv[], Command_Result_t *result)
{
	(void)argc;
	(void)argv;

	Command_InitResult(result, CMD_ID_PING);
	(void)Command_WriteResponse("PONG\r\n");

	if(result != NULL){
		result->ack_status_code = 0;
		result->status = COMMAND_STATUS_OK;
	}

	Command_SendAck(result);
	return COMMAND_STATUS_OK;
}

static Command_Status_t CMD_GET_ADC(int argc, char *argv[], Command_Result_t *result)
{
	ADC_HealthData_t data;
	ADC_Monitor_Status_t adc_status;
	char response[128];

	(void)argc;
	(void)argv;

	Command_InitResult(result, CMD_ID_GET_ADC);
	adc_status = ADC_Monitor_GetData(&data);

	if(adc_status != ADC_MONITOR_OK){
		(void)Command_WriteResponse("ERR: ADC Not Ready\r\n");
		if(result != NULL){
			result->ack_status_code = -1;
			result->argument = (uint32_t)adc_status;
			result->status = COMMAND_STATUS_NOT_READY;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_NOT_READY;
	}

	snprintf(response,
			 sizeof(response),
			 "VDDA=%.3f, TEMP=%.2f, BATT=%.3f\r\n",
			 data.vdda_voltage,
			 data.mcu_temp_c,
			 data.battery_voltage);
	(void)Command_WriteResponse(response);

	if(result != NULL){
		result->ack_status_code = 0;
		result->status = COMMAND_STATUS_OK;
	}

	Command_SendAck(result);
	return COMMAND_STATUS_OK;
}

static Command_Status_t CMD_SET_RATE(int argc, char *argv[], Command_Result_t *result)
{
	char *endptr = NULL;
	long rate;
	char msg[64];

	Command_InitResult(result, CMD_ID_SET_RATE);

	if(argc < 2){
		(void)Command_WriteResponse("ERR: Missing Argument\r\n");
		if(result != NULL){
			result->ack_status_code = -1;
			result->status = COMMAND_STATUS_MISSING_ARGUMENT;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_MISSING_ARGUMENT;
	}

	rate = strtol(argv[1], &endptr, 10);
	if((endptr == argv[1]) || (*endptr != '\0')){
		(void)Command_WriteResponse("ERR: Invalid Argument\r\n");
		if(result != NULL){
			result->ack_status_code = -2;
			result->status = COMMAND_STATUS_INVALID_ARGUMENT;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_INVALID_ARGUMENT;
	}

	if((rate < 1L) || (rate > 1000L)){
		(void)Command_WriteResponse("ERR: Rate Out Of Range\r\n");
		if(result != NULL){
			result->ack_status_code = -3;
			result->argument = (uint32_t)rate;
			result->status = COMMAND_STATUS_INVALID_ARGUMENT;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_INVALID_ARGUMENT;
	}

	snprintf(msg, sizeof(msg), "Rate set to %ld\r\n", rate);
	(void)Command_WriteResponse(msg);

	if(result != NULL){
		result->ack_status_code = 0;
		result->argument = (uint32_t)rate;
		result->status = COMMAND_STATUS_OK;
	}

	Command_SendAck(result);
	return COMMAND_STATUS_OK;
}

static Command_Status_t CMD_PWR_STATUS(int argc, char *argv[], Command_Result_t *result)
{
	IPMS_StatusSnapshot_t snapshot;
	char response[160];

	(void)argc;
	(void)argv;

	Command_InitResult(result, CMD_ID_PWR_STATUS);
	IPMS_GetStatus(&snapshot);

	snprintf(response,
			 sizeof(response),
			 "PWR state=%s sim=%s policy=%s action=%s batt=%.3fV eff=%.3fV wake=%s transitions=%lu\r\n",
			 IPMS_PowerStateToString(snapshot.power_state),
			 IPMS_SimulationModeToString(snapshot.simulation_mode),
			 IPMS_PolicyModeToString(snapshot.policy_mode),
			 IPMS_ActionTypeToString(snapshot.pending_action),
			 snapshot.measured_battery_voltage,
			 snapshot.effective_battery_voltage,
			 IPMS_WakeSourceToString(snapshot.last_wake_source),
			 (unsigned long)snapshot.transition_count);
	(void)Command_WriteResponse(response);

	if(result != NULL){
		result->ack_status_code = 0;
		result->argument = (uint32_t)snapshot.power_state;
		result->status = COMMAND_STATUS_OK;
	}

	Command_SendAck(result);
	return COMMAND_STATUS_OK;
}

static Command_Status_t CMD_PWR_SIM(int argc, char *argv[], Command_Result_t *result)
{
	IPMS_Status_t ipms_status;
	IPMS_SimulationMode_t mode;
	char response[64];

	Command_InitResult(result, CMD_ID_PWR_SIM);

	if(argc < 2){
		(void)Command_WriteResponse("ERR: Missing Argument\r\n");
		if(result != NULL){
			result->ack_status_code = -1;
			result->status = COMMAND_STATUS_MISSING_ARGUMENT;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_MISSING_ARGUMENT;
	}

	if(strcmp(argv[1], "AUTO") == 0){
		mode = IPMS_SIMULATION_AUTO;
	} else if(strcmp(argv[1], "SUNLIGHT") == 0){
		mode = IPMS_SIMULATION_SUNLIGHT;
	} else if(strcmp(argv[1], "ECLIPSE") == 0){
		mode = IPMS_SIMULATION_ECLIPSE;
	} else {
		(void)Command_WriteResponse("ERR: Invalid Simulation Mode\r\n");
		if(result != NULL){
			result->ack_status_code = -2;
			result->status = COMMAND_STATUS_INVALID_ARGUMENT;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_INVALID_ARGUMENT;
	}

	ipms_status = IPMS_SetSimulationMode(mode, HAL_GetTick());
	if(ipms_status != IPMS_STATUS_OK){
		snprintf(response, sizeof(response), "ERR: IPMS %s\r\n", IPMS_StatusToString(ipms_status));
		(void)Command_WriteResponse(response);
		if(result != NULL){
			result->ack_status_code = -3;
			result->argument = (uint32_t)ipms_status;
			result->status = COMMAND_STATUS_NOT_READY;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_NOT_READY;
	}

	snprintf(response, sizeof(response), "Power simulation=%s\r\n", IPMS_SimulationModeToString(mode));
	(void)Command_WriteResponse(response);

	if(result != NULL){
		result->ack_status_code = 0;
		result->argument = (uint32_t)mode;
		result->status = COMMAND_STATUS_OK;
	}

	Command_SendAck(result);
	return COMMAND_STATUS_OK;
}

static Command_Status_t CMD_PWR_POLICY(int argc, char *argv[], Command_Result_t *result)
{
	IPMS_Status_t ipms_status;
	IPMS_PolicyMode_t mode;
	char response[72];

	Command_InitResult(result, CMD_ID_PWR_POLICY);

	if(argc < 2){
		(void)Command_WriteResponse("ERR: Missing Argument\r\n");
		if(result != NULL){
			result->ack_status_code = -1;
			result->status = COMMAND_STATUS_MISSING_ARGUMENT;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_MISSING_ARGUMENT;
	}

	if((strcmp(argv[1], "MONITOR") == 0) || (strcmp(argv[1], "MONITOR_ONLY") == 0)){
		mode = IPMS_POLICY_MONITOR_ONLY;
	} else if((strcmp(argv[1], "SLEEP") == 0) || (strcmp(argv[1], "ENABLE_SLEEP") == 0)){
		mode = IPMS_POLICY_ENABLE_SLEEP;
	} else if((strcmp(argv[1], "FULL") == 0) || (strcmp(argv[1], "ENABLE_SLEEP_AND_STOP") == 0)){
		mode = IPMS_POLICY_ENABLE_SLEEP_AND_STOP;
	} else {
		(void)Command_WriteResponse("ERR: Invalid Policy Mode\r\n");
		if(result != NULL){
			result->ack_status_code = -2;
			result->status = COMMAND_STATUS_INVALID_ARGUMENT;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_INVALID_ARGUMENT;
	}

	ipms_status = IPMS_SetPolicyMode(mode, HAL_GetTick());
	if(ipms_status != IPMS_STATUS_OK){
		snprintf(response, sizeof(response), "ERR: IPMS %s\r\n", IPMS_StatusToString(ipms_status));
		(void)Command_WriteResponse(response);
		if(result != NULL){
			result->ack_status_code = -3;
			result->argument = (uint32_t)ipms_status;
			result->status = COMMAND_STATUS_NOT_READY;
		}
		Command_SendAck(result);
		return COMMAND_STATUS_NOT_READY;
	}

	snprintf(response, sizeof(response), "Power policy=%s\r\n", IPMS_PolicyModeToString(mode));
	(void)Command_WriteResponse(response);

	if(result != NULL){
		result->ack_status_code = 0;
		result->argument = (uint32_t)mode;
		result->status = COMMAND_STATUS_OK;
	}

	Command_SendAck(result);
	return COMMAND_STATUS_OK;
}

static Command_Status_t CMD_SD_STATUS(int argc, char *argv[], Command_Result_t *result)
{
    StorageService_SdStatusResult_t storage_result;
    StorageService_Status_t service_status;
    char response[160];

    (void)argc;
    (void)argv;

    Command_InitResult(result, CMD_ID_SD_STATUS);
    service_status = StorageService_RequestSdStatus(&storage_result, 1000U);

    if (service_status != STORAGE_SERVICE_STATUS_OK)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR: SD service %s\r\n",
                 StorageService_StatusToString(service_status));
        (void)Command_WriteResponse(response);

        if(result != NULL){
            result->ack_status_code = -10;
            result->argument = (uint32_t)service_status;
            result->status = COMMAND_STATUS_NOT_READY;
        }
        Command_SendAck(result);
        return COMMAND_STATUS_NOT_READY;
    }

    if (storage_result.op_status == STORAGE_OP_STATUS_OK)
    {
        snprintf(response,
                 sizeof(response),
                 "SD detect=%s init=%s state=%s blocks=%lu block_size=%lu\r\n",
                 (storage_result.detected == SD_PRESENT) ? "PRESENT" : "ABSENT",
                 (storage_result.init_status == MSD_OK) ? "OK" : "ERROR",
                 (storage_result.card_state == SD_TRANSFER_OK) ? "TRANSFER_OK" : "BUSY",
                 (unsigned long)storage_result.block_count,
                 (unsigned long)storage_result.block_size);
        (void)Command_WriteResponse(response);

        if(result != NULL){
            result->ack_status_code = 0;
            result->argument = storage_result.block_count;
            result->status = COMMAND_STATUS_OK;
        }
        Command_SendAck(result);
        return COMMAND_STATUS_OK;
    }

    snprintf(response,
             sizeof(response),
             "ERR: SD %s detect=%s init=%s\r\n",
             StorageService_OpStatusToString(storage_result.op_status),
             (storage_result.detected == SD_PRESENT) ? "PRESENT" : "ABSENT",
             (storage_result.init_status == MSD_OK) ? "OK" : "ERROR");
    (void)Command_WriteResponse(response);

    if(result != NULL){
        result->ack_status_code = -1;
        result->argument = (uint32_t)storage_result.op_status;
        result->status = COMMAND_STATUS_NOT_READY;
    }
    Command_SendAck(result);
    return COMMAND_STATUS_NOT_READY;
}

static Command_Status_t CMD_SD_TEST(int argc, char *argv[], Command_Result_t *result)
{
    StorageService_SmokeTestResult_t storage_result;
    StorageService_Status_t service_status;
    char response[160];

    (void)argc;
    (void)argv;

    Command_InitResult(result, CMD_ID_SD_TEST);
    service_status = StorageService_RequestSmokeTest(&storage_result, 2000U);

    if (service_status != STORAGE_SERVICE_STATUS_OK)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR: SD service %s\r\n",
                 StorageService_StatusToString(service_status));
        (void)Command_WriteResponse(response);
        if(result != NULL){
            result->ack_status_code = -10;
            result->argument = (uint32_t)service_status;
            result->status = COMMAND_STATUS_NOT_READY;
        }
        Command_SendAck(result);
        return COMMAND_STATUS_NOT_READY;
    }

    if (storage_result.op_status != STORAGE_OP_STATUS_OK)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR: SD Test %s %s\r\n",
                 StorageService_OpStatusToString(storage_result.op_status),
                 Command_FatFsResultToString(storage_result.fatfs_result));
        (void)Command_WriteResponse(response);
        if(result != NULL){
            result->ack_status_code = -5;
            result->argument = (uint32_t)storage_result.op_status;
            result->status = COMMAND_STATUS_IO_ERROR;
        }
        Command_SendAck(result);
        return COMMAND_STATUS_IO_ERROR;
    }

    snprintf(response,
             sizeof(response),
             "SD test OK write=%u read=%u sample=\"%s\"\r\n",
             (unsigned int)storage_result.bytes_written,
             (unsigned int)storage_result.bytes_read,
             storage_result.sample);
    (void)Command_WriteResponse(response);

    if(result != NULL){
        result->ack_status_code = 0;
        result->argument = storage_result.bytes_written;
        result->status = COMMAND_STATUS_OK;
    }
    Command_SendAck(result);
    return COMMAND_STATUS_OK;
}

const CommandEntry_t command_table[] = {
		{"PING"		, 		CMD_PING	 },
		{"GET_ADC"	, 		CMD_GET_ADC	 },
		{"SET_RATE"	, 		CMD_SET_RATE },
		{"PWR_STATUS",  CMD_PWR_STATUS},
		{"PWR_SIM",     CMD_PWR_SIM},
		{"PWR_POLICY",  CMD_PWR_POLICY},
        {"SD_STATUS",   CMD_SD_STATUS},
        {"SD_TEST",     CMD_SD_TEST}
};

const uint32_t command_count =
    sizeof(command_table) / sizeof(CommandEntry_t);

Command_Result_t Command_DispatchLine(const char *cmd_line)
{
	Command_Result_t result;
	char command_buffer[COMMAND_MAX_LINE_LENGTH];
	char *argv[MAX_TOKENS];
	char *normalized_line;
	int argc;

	Command_InitResult(&result, CMD_ID_UNKNOWN);

	if(cmd_line == NULL){
		result.status = COMMAND_STATUS_INVALID_ARGUMENT;
		result.ack_status_code = -126;
		return result;
	}

	strncpy(command_buffer, cmd_line, sizeof(command_buffer) - 1U);
	command_buffer[sizeof(command_buffer) - 1U] = '\0';
	normalized_line = Command_TrimWhitespace(command_buffer);
	argc = Command_Tokenize(normalized_line, argv, MAX_TOKENS);

	if(argc == 0){
		result.status = COMMAND_STATUS_INVALID_ARGUMENT;
		result.ack_status_code = -125;
		return result;
	}

	for(uint32_t i = 0; i < command_count; i++){
		if(strcmp(argv[0], command_table[i].name) == 0){
			(void)command_table[i].handler(argc, argv, &result);
			return result;
		}
	}

	Logger_Warn("Unknown command received: %s", argv[0]);
	(void)Telemetry_SendEventEx(TELEM_EVENT_COMMAND_UNKNOWN, 0U);
	(void)Command_WriteResponse("ERR: Unknown Command\r\n");
	result.status = COMMAND_STATUS_UNKNOWN;
	result.ack_status_code = -127;
	return result;
}

const char *Command_StatusToString(Command_Status_t status)
{
	switch(status){
	case COMMAND_STATUS_OK:
		return "OK";
	case COMMAND_STATUS_UNKNOWN:
		return "UNKNOWN";
	case COMMAND_STATUS_MISSING_ARGUMENT:
		return "MISSING_ARGUMENT";
	case COMMAND_STATUS_INVALID_ARGUMENT:
		return "INVALID_ARGUMENT";
	case COMMAND_STATUS_NOT_READY:
		return "NOT_READY";
	case COMMAND_STATUS_IO_ERROR:
	default:
		return "IO_ERROR";
	}
}
