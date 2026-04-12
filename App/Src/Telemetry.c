/*
 * Telemetry.c
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#include "Telemetry.h"
#include "Ring_Buffer.h"
#include "UART_Driver.h"
#include "rtc.h"
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
static volatile uint32_t g_telem_queued_frames = 0U;
static volatile uint32_t g_telem_sent_frames = 0U;
static volatile uint32_t g_telem_dropped_frames = 0U;
static volatile uint32_t g_telem_tx_busy_retries = 0U;
static volatile uint32_t g_telem_transport_errors = 0U;
static volatile uint32_t g_telem_rtc_fallback_count = 0U;
static volatile uint16_t g_telem_max_queue_depth = 0U;
static volatile Telemetry_Status_t g_telem_last_enqueue_status = TELEM_STATUS_NOT_INITIALIZED;
static volatile Telemetry_Status_t g_telem_last_process_status = TELEM_STATUS_NOT_INITIALIZED;
static volatile TelemetryTimestampSource_t g_telem_last_timestamp_source = TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK;

static bool Telemetry_IsLeapYear(uint32_t year)
{
	return ((year % 4U) == 0U) && ((((year % 100U) != 0U)) || ((year % 400U) == 0U));
}

static uint32_t Telemetry_GetRtcTimestampSeconds(TelemetryTimestampSource_t *source)
{
	static const uint8_t days_in_month[] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
	RTC_TimeTypeDef sTime;
	RTC_DateTypeDef sDate;
	uint32_t year;
	uint32_t days = 0U;

	if((HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) ||
	   (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)){
		if(source != NULL){
			*source = TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK;
		}
		return HAL_GetTick() / 1000U;
	}

	/*
	 * CubeMX initializes the RTC to the default epoch on boot. Until an external
	 * agent sets a real wall-clock time, treat the timestamp as uptime seconds
	 * so consumers do not mistake the default epoch for a valid mission time.
	 */
	if((sDate.Year == 0U) &&
	   (sDate.Month == RTC_MONTH_JANUARY) &&
	   (sDate.Date == 1U) &&
	   (sTime.Hours == 0U) &&
	   (sTime.Minutes == 0U) &&
	   (sTime.Seconds == 0U)){
		if(source != NULL){
			*source = TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK;
		}
		return HAL_GetTick() / 1000U;
	}

	year = TELEM_TIMESTAMP_EPOCH_YEAR + (uint32_t)sDate.Year;

	for(uint32_t y = TELEM_TIMESTAMP_EPOCH_YEAR; y < year; y++){
		days += Telemetry_IsLeapYear(y) ? 366U : 365U;
	}

	for(uint32_t month = 1U; month < (uint32_t)sDate.Month; month++){
		days += days_in_month[month - 1U];
		if((month == 2U) && Telemetry_IsLeapYear(year)){
			days += 1U;
		}
	}

	days += (uint32_t)(sDate.Date - 1U);

	if(source != NULL){
		*source = TELEM_TIMESTAMP_SOURCE_RTC;
	}

	return (days * 86400U) +
		   ((uint32_t)sTime.Hours * 3600U) +
		   ((uint32_t)sTime.Minutes * 60U) +
		   (uint32_t)sTime.Seconds;
}

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

static void Telemetry_UpdateQueueDepthPeak(void)
{
	uint16_t depth = FrameQueue_Count(&telem_queue);

	if(depth > g_telem_max_queue_depth){
		g_telem_max_queue_depth = depth;
	}
}

static void Telemetry_BuildFrame(TelemetryFrame_t *frame, TelemetryPacketID_t id, uint8_t *payload, uint16_t len){
	TelemetryTimestampSource_t timestamp_source = TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK;

	frame -> sync_word = TELEM_SYNC_WORD;
	frame -> timestamp = Telemetry_GetRtcTimestampSeconds(&timestamp_source);
	frame -> packet_id = (uint8_t)id;
	g_telem_last_timestamp_source = timestamp_source;

	if(timestamp_source == TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK){
		g_telem_rtc_fallback_count++;
	}

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
		g_telem_last_enqueue_status = TELEM_STATUS_NOT_INITIALIZED;
		g_telem_last_process_status = TELEM_STATUS_NOT_INITIALIZED;
		return;
	}

	pCrc 	= hcrc;

	FrameQueue_Init(&telem_queue, (uint8_t *)frame_buffer, sizeof(TelemetryFrame_t), TELEM_QUEUE_SIZE);
	frame_pending = false;
	memset(&tx_frame, 0, sizeof(tx_frame));
	g_telem_queued_frames = 0U;
	g_telem_sent_frames = 0U;
	g_telem_dropped_frames = 0U;
	g_telem_tx_busy_retries = 0U;
	g_telem_transport_errors = 0U;
	g_telem_rtc_fallback_count = 0U;
	g_telem_max_queue_depth = 0U;
	g_telem_last_enqueue_status = TELEM_STATUS_OK;
	g_telem_last_process_status = TELEM_STATUS_IDLE;
	g_telem_last_timestamp_source = TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK;

}


Telemetry_Status_t Telemetry_QueuePacketEx(TelemetryPacketID_t id, uint8_t* payload, uint16_t len){

	if(pCrc == NULL){
		g_telem_last_enqueue_status = TELEM_STATUS_NOT_INITIALIZED;
		return TELEM_STATUS_NOT_INITIALIZED;
	}
	if(!Telemetry_IsValidPacketId(id)){
		g_telem_last_enqueue_status = TELEM_STATUS_INVALID_PACKET_ID;
		return TELEM_STATUS_INVALID_PACKET_ID;
	}
	if((payload == NULL) && (len > 0U)){
		g_telem_last_enqueue_status = TELEM_STATUS_INVALID_PARAM;
		return TELEM_STATUS_INVALID_PARAM;
	}
	if(len > TELEM_PAYLOAD_SIZE){
		g_telem_last_enqueue_status = TELEM_STATUS_INVALID_PARAM;
		return TELEM_STATUS_INVALID_PARAM;
	}

	TelemetryFrame_t frame;
	Telemetry_BuildFrame(&frame, id, payload, len);

	if(!FrameQueue_Push(&telem_queue, &frame)){
		g_telem_dropped_frames++;
		g_telem_last_enqueue_status = TELEM_STATUS_QUEUE_FULL;
		return TELEM_STATUS_QUEUE_FULL;
	}

	g_telem_queued_frames++;
	Telemetry_UpdateQueueDepthPeak();
	g_telem_last_enqueue_status = TELEM_STATUS_OK;

	return TELEM_STATUS_OK;

}

bool Telemetry_QueuePacket(TelemetryPacketID_t id, uint8_t *payload, uint16_t len)
{
	return (Telemetry_QueuePacketEx(id, payload, len) == TELEM_STATUS_OK);
}

Telemetry_Status_t Telemetry_ProcessStep(void){
	UART_Driver_Status_t uart_status;

	if(pCrc == NULL){
		g_telem_last_process_status = TELEM_STATUS_NOT_INITIALIZED;
		return TELEM_STATUS_NOT_INITIALIZED;
	}

	if(!frame_pending){
		if(!FrameQueue_Pop(&telem_queue, &tx_frame)){
			g_telem_last_process_status = TELEM_STATUS_IDLE;
			return TELEM_STATUS_IDLE;
		}

		frame_pending = true;
	}

	uart_status = UART_WriteChannel(UART_DRIVER_CHANNEL_TELEMETRY, (uint8_t *)&tx_frame, sizeof(tx_frame));
	if(uart_status == UART_DRIVER_OK){
		frame_pending = false;
		g_telem_sent_frames++;
		g_telem_last_process_status = TELEM_STATUS_OK;
		return TELEM_STATUS_OK;
	}

	if((uart_status == UART_DRIVER_BUSY) || (uart_status == UART_DRIVER_BUFFER_FULL)){
		g_telem_tx_busy_retries++;
		g_telem_last_process_status = TELEM_STATUS_TX_BUSY;
		return TELEM_STATUS_TX_BUSY;
	}

	g_telem_transport_errors++;
	g_telem_last_process_status = TELEM_STATUS_TRANSPORT_ERROR;
	return TELEM_STATUS_TRANSPORT_ERROR;
}

void Telemetry_Process(void){
	(void)Telemetry_ProcessStep();
}

Telemetry_Status_t Telemetry_SendSystemStatusEx(uint8_t status)
{
    TelemetrySystemStatusPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return TELEM_STATUS_INVALID_PARAM;
    }

    payload.status_code = status;
    payload.uptime_ms = HAL_GetTick();

    return Telemetry_QueuePacketEx(
        TELEM_ID_SYSTEM_STATUS,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendSystemStatus(uint8_t status)
{
	return (Telemetry_SendSystemStatusEx(status) == TELEM_STATUS_OK);
}

Telemetry_Status_t Telemetry_SendADCHealthEx(float vdda_voltage, float battery_voltage, float mcu_temp_c)
{
    TelemetryADCHealthPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return TELEM_STATUS_INVALID_PARAM;
    }

    payload.vdda_voltage = vdda_voltage;
    payload.battery_voltage = battery_voltage;
    payload.mcu_temp_c = mcu_temp_c;

    return Telemetry_QueuePacketEx(
        TELEM_ID_ADC_HEALTH,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendADCHealth(float vdda_voltage, float battery_voltage, float mcu_temp_c)
{
	return (Telemetry_SendADCHealthEx(vdda_voltage, battery_voltage, mcu_temp_c) == TELEM_STATUS_OK);
}

Telemetry_Status_t Telemetry_SendEventEx(TelemetryEventCode_t event_code, uint32_t event_value)
{
    TelemetryEventPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return TELEM_STATUS_INVALID_PARAM;
    }

    payload.event_code = event_code;
    payload.event_value = event_value;

    return Telemetry_QueuePacketEx(
        TELEM_ID_EVENT,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendEvent(TelemetryEventCode_t event_code, uint32_t event_value)
{
	return (Telemetry_SendEventEx(event_code, event_value) == TELEM_STATUS_OK);
}

Telemetry_Status_t Telemetry_SendHeartbeatEx(void)
{
    TelemetryHeartbeatPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return TELEM_STATUS_INVALID_PARAM;
    }

    payload.uptime_ms = HAL_GetTick();
    payload.queue_depth = FrameQueue_Count(&telem_queue);
    payload.frame_pending = frame_pending ? 1U : 0U;
    payload.reserved = 0U;

    return Telemetry_QueuePacketEx(
        TELEM_ID_HEARTBEAT,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendHeartbeat(void)
{
	return (Telemetry_SendHeartbeatEx() == TELEM_STATUS_OK);
}

Telemetry_Status_t Telemetry_SendCommandAckEx(uint8_t command_id, int8_t status_code, uint32_t argument)
{
    TelemetryCommandAckPayload_t payload;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return TELEM_STATUS_INVALID_PARAM;
    }

    payload.command_id = command_id;
    payload.status_code = status_code;
    payload.reserved = 0U;
    payload.argument = argument;

    return Telemetry_QueuePacketEx(
        TELEM_ID_COMMAND_ACK,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

bool Telemetry_SendCommandAck(uint8_t command_id, int8_t status_code, uint32_t argument)
{
	return (Telemetry_SendCommandAckEx(command_id, status_code, argument) == TELEM_STATUS_OK);
}

void Telemetry_GetStats(TelemetryStats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->queue_depth = FrameQueue_Count(&telem_queue);
    stats->queue_capacity = (uint16_t)(TELEM_QUEUE_SIZE - 1U);
    stats->max_queue_depth = g_telem_max_queue_depth;
    stats->queued_frames = g_telem_queued_frames;
    stats->sent_frames = g_telem_sent_frames;
    stats->dropped_frames = g_telem_dropped_frames;
    stats->tx_busy_retries = g_telem_tx_busy_retries;
    stats->transport_errors = g_telem_transport_errors;
    stats->rtc_fallback_count = g_telem_rtc_fallback_count;
    stats->frame_pending = frame_pending ? 1U : 0U;
    stats->last_enqueue_status = g_telem_last_enqueue_status;
    stats->last_process_status = g_telem_last_process_status;
    stats->last_timestamp_source = g_telem_last_timestamp_source;
}

const char *Telemetry_StatusToString(Telemetry_Status_t status)
{
	switch(status){
	case TELEM_STATUS_OK:
		return "OK";
	case TELEM_STATUS_IDLE:
		return "IDLE";
	case TELEM_STATUS_INVALID_PARAM:
		return "INVALID_PARAM";
	case TELEM_STATUS_NOT_INITIALIZED:
		return "NOT_INITIALIZED";
	case TELEM_STATUS_INVALID_PACKET_ID:
		return "INVALID_PACKET_ID";
	case TELEM_STATUS_QUEUE_FULL:
		return "QUEUE_FULL";
	case TELEM_STATUS_TX_BUSY:
		return "TX_BUSY";
	case TELEM_STATUS_TRANSPORT_ERROR:
	default:
		return "TRANSPORT_ERROR";
	}
}


