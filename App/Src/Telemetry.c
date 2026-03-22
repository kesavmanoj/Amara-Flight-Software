/*
 * Telemetry.c
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#include "Telemetry.h"
#include "Ring_Buffer.h"
#include "UART_Driver.h"
#include <string.h>

_Static_assert(((sizeof(TelemetryFrame_t) - sizeof(uint32_t)) % 4U) == 0U,
		"Telemetry frame CRC region must stay 32-bit aligned");
_Static_assert(sizeof(TelemetrySystemStatusPayload_t) <= TELEM_PAYLOAD_SIZE,
		"System status payload exceeds telemetry payload size");
_Static_assert(sizeof(TelemetryADCHealthPayload_t) <= TELEM_PAYLOAD_SIZE,
		"ADC health payload exceeds telemetry payload size");
_Static_assert(sizeof(TelemetryEventPayload_t) <= TELEM_PAYLOAD_SIZE,
		"Event payload exceeds telemetry payload size");
_Static_assert(sizeof(TelemetryHeartbeatPayload_t) <= TELEM_PAYLOAD_SIZE,
		"Heartbeat payload exceeds telemetry payload size");
_Static_assert(sizeof(TelemetryCommandAckPayload_t) <= TELEM_PAYLOAD_SIZE,
		"Command ACK payload exceeds telemetry payload size");

static CRC_HandleTypeDef *pCrc = NULL;

static TelemetryFrame_t frame_buffer[TELEM_QUEUE_SIZE];
static TelemetryFrame_t tx_frame;
static FrameQueue_t telem_queue;

static bool frame_pending = false;

static bool Telemetry_IsValidPacketId(TelemetryPacketID_t id)
{
	switch(id){
	case TELEM_ID_SYSTEM_STATUS:
	case TELEM_ID_ADC_HEALTH:
	case TELEM_ID_EVENT:
	case TELEM_ID_HEARTBEAT:
	case TELEM_ID_COMMAND_ACK:
		return true;
	default:
		return false;
	}
}

static void Telemetry_BuildFrame(TelemetryFrame_t *frame, TelemetryPacketID_t id, uint8_t *payload, uint16_t len){

	frame -> sync_word = TELEM_SYNC_WORD;
	frame -> timestamp = HAL_GetTick(); 		// Replace with the RTC value
	frame -> packet_id = (uint8_t)id;

	memset(frame -> payload, 0, TELEM_PAYLOAD_SIZE);

	if(payload != NULL && len > 0){
		memcpy(frame -> payload, payload, len);
	}

	uint32_t word_count = (sizeof(TelemetryFrame_t) - sizeof(uint32_t)) / 4;

	frame -> crc = HAL_CRC_Calculate(pCrc, (uint32_t *)frame, word_count);

}

void Telemetry_Init(CRC_HandleTypeDef *hcrc){
	if(hcrc == NULL){
		pCrc = NULL;
		frame_pending = false;
		return;
	}

	pCrc 	= hcrc;

	FrameQueue_Init(&telem_queue, (uint8_t *)frame_buffer, sizeof(TelemetryFrame_t), TELEM_QUEUE_SIZE);
	frame_pending = false;
	memset(&tx_frame, 0, sizeof(tx_frame));

}


bool Telemetry_QueuePacket(TelemetryPacketID_t id, uint8_t* payload, uint16_t len){

	if(pCrc == NULL) return false;
	if(!Telemetry_IsValidPacketId(id)) return false;
	if((payload == NULL) && (len > 0U)) return false;
	if(len > TELEM_PAYLOAD_SIZE) return false;

	TelemetryFrame_t frame;
	Telemetry_BuildFrame(&frame, id, payload, len);

	return FrameQueue_Push(&telem_queue, &frame);

}

void Telemetry_Process(void){
	if(pCrc == NULL) return;

	if(!frame_pending){
		if(!FrameQueue_Pop(&telem_queue, &tx_frame)){
			return;
		}

		frame_pending = true;
	}

	if(UART_WriteChannel(UART_DRIVER_CHANNEL_TELEMETRY, (uint8_t *)&tx_frame, sizeof(tx_frame)) == UART_DRIVER_OK){
		frame_pending = false;
	}
}

bool Telemetry_SendSystemStatus(uint8_t status)
{
    TelemetrySystemStatusPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return false;
    }

    payload.status_code = status;
    payload.uptime_ms = HAL_GetTick();

    return Telemetry_QueuePacket(
        TELEM_ID_SYSTEM_STATUS,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendADCHealth(float vdda_voltage, float battery_voltage, float mcu_temp_c)
{
    TelemetryADCHealthPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return false;
    }

    payload.vdda_voltage = vdda_voltage;
    payload.battery_voltage = battery_voltage;
    payload.mcu_temp_c = mcu_temp_c;

    return Telemetry_QueuePacket(
        TELEM_ID_ADC_HEALTH,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendEvent(TelemetryEventCode_t event_code, uint32_t event_value)
{
    TelemetryEventPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return false;
    }

    payload.event_code = event_code;
    payload.event_value = event_value;

    return Telemetry_QueuePacket(
        TELEM_ID_EVENT,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendHeartbeat(void)
{
    TelemetryHeartbeatPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return false;
    }

    payload.uptime_ms = HAL_GetTick();
    payload.queue_depth = FrameQueue_Count(&telem_queue);
    payload.frame_pending = frame_pending ? 1U : 0U;
    payload.reserved = 0U;

    return Telemetry_QueuePacket(
        TELEM_ID_HEARTBEAT,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendCommandAck(uint8_t command_id, int8_t status_code, uint32_t argument)
{
    TelemetryCommandAckPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return false;
    }

    payload.command_id = command_id;
    payload.status_code = status_code;
    payload.reserved = 0U;
    payload.argument = argument;

    return Telemetry_QueuePacket(
        TELEM_ID_COMMAND_ACK,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}







