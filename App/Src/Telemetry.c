/**
 * @file Telemetry.c
 * @brief Telemetry module implementation (frame builder + queue + transport processing).
 */

#include "Telemetry.h"
#include "Ring_Buffer.h"
#include "UART_Driver.h"
#include "rtc.h"
#include <string.h>


_Static_assert(((sizeof(TelemetryFrame_t) - sizeof(uint32_t)) % 4U) == 0U,
			"Telemetry frame CRC region must stay 32-bit aligned");
_Static_assert(sizeof(TelemetryFrame_t) <= G2S_MAX_PAYLOAD_SIZE,
			"Telemetry frame must fit into a single G2S payload");
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

/* Process-state flags for the single-frame transport state machine. */
static bool frame_pending = false;
static bool process_active = false;

/* Diagnostic counters exposed through Telemetry_GetStats(). */
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
static volatile TelemetryDownlinkMode_t g_telem_downlink_mode = TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR;
static volatile uint32_t g_telem_radio_sent_frames = 0U;
static volatile uint32_t g_telem_uart_sent_frames = 0U;

/** @brief Enter module critical section by masking interrupts. */
static uint32_t Telemetry_EnterCritical(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}

/** @brief Exit module critical section and restore prior interrupt mask state. */
static void Telemetry_ExitCritical(uint32_t primask)
{
	if(primask == 0U){
		__enable_irq();
	}
}

/** @brief Store last enqueue status atomically and return it. */
static Telemetry_Status_t Telemetry_SetEnqueueStatus(Telemetry_Status_t status)
{
	uint32_t primask = Telemetry_EnterCritical();
	g_telem_last_enqueue_status = status;
	Telemetry_ExitCritical(primask);
	return status;
}

/** @brief Store last process status atomically and return it. */
static Telemetry_Status_t Telemetry_SetProcessStatus(Telemetry_Status_t status)
{
	uint32_t primask = Telemetry_EnterCritical();
	g_telem_last_process_status = status;
	Telemetry_ExitCritical(primask);
	return status;
}

/** @brief Check whether current downlink mode includes radio transport. */
static bool Telemetry_DownlinkUsesRadio(TelemetryDownlinkMode_t mode)
{
	return (mode == TELEM_DOWNLINK_RADIO_ONLY) || (mode == TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR);
}

/** @brief Check whether current downlink mode includes UART transport. */
static bool Telemetry_DownlinkUsesUart(TelemetryDownlinkMode_t mode)
{
	return (mode == TELEM_DOWNLINK_UART_ONLY) || (mode == TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR);
}

/** @brief Return true when @p year is a leap year in Gregorian calendar. */
static bool Telemetry_IsLeapYear(uint32_t year)
{
	return ((year % 4U) == 0U) && ((((year % 100U) != 0U)) || ((year % 400U) == 0U));
}

/**
 * @brief Generate frame timestamp in seconds from configured telemetry epoch.
 * @param source Optional output indicating whether RTC or uptime fallback was used.
 * @return Timestamp in seconds.
 */
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

/** @brief Validate packet identifier for telemetry frame creation. */
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

/** @brief Update queue depth high-water mark; caller must already hold critical section. */
static void Telemetry_UpdateQueueDepthPeakLocked(void)
{
	uint16_t depth = FrameQueue_Count(&telem_queue);

	if(depth > g_telem_max_queue_depth){
		g_telem_max_queue_depth = depth;
	}
}

/**
 * @brief Build a telemetry frame from payload data and compute CRC.
 *
 * Notes:
 * - Unused payload bytes are zero-filled for deterministic CRC.
 * - Timestamp source is tracked for diagnostics (RTC vs fallback).
 */
static void Telemetry_BuildFrame(TelemetryFrame_t *frame, CRC_HandleTypeDef *crc, TelemetryPacketID_t id, const uint8_t *payload, uint16_t len){
	TelemetryTimestampSource_t timestamp_source = TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK;
	uint32_t primask;

	frame -> sync_word = TELEM_SYNC_WORD;
	frame -> timestamp = Telemetry_GetRtcTimestampSeconds(&timestamp_source);
	frame -> packet_id = (uint8_t)id;
	frame -> payload_length = (uint8_t)len;

	memset(frame -> payload, 0, TELEM_PAYLOAD_SIZE);

	if(payload != NULL && len > 0){
		memcpy(frame -> payload, payload, len);
	}

	uint32_t word_count = (sizeof(TelemetryFrame_t) - sizeof(uint32_t)) / 4;

	frame -> crc = HAL_CRC_Calculate(crc, (uint32_t *)frame, word_count);

	primask = Telemetry_EnterCritical();
	g_telem_last_timestamp_source = timestamp_source;
	if(timestamp_source == TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK){
		g_telem_rtc_fallback_count++;
	}
	Telemetry_ExitCritical(primask);

}

/** @copydoc Telemetry_Init */
void Telemetry_Init(CRC_HandleTypeDef *hcrc){
	uint32_t primask = Telemetry_EnterCritical();

	if(hcrc == NULL){
		pCrc = NULL;
		frame_pending = false;
		process_active = false;
		g_telem_last_enqueue_status = TELEM_STATUS_NOT_INITIALIZED;
		g_telem_last_process_status = TELEM_STATUS_NOT_INITIALIZED;
		g_telem_downlink_mode = TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR;
		Telemetry_ExitCritical(primask);
		return;
	}

	pCrc 	= hcrc;

	FrameQueue_Init(&telem_queue, (uint8_t *)frame_buffer, sizeof(TelemetryFrame_t), TELEM_QUEUE_SIZE);
	frame_pending = false;
	process_active = false;
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
	g_telem_downlink_mode = TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR;
	g_telem_radio_sent_frames = 0U;
	g_telem_uart_sent_frames = 0U;
	Telemetry_ExitCritical(primask);

}


/** @copydoc Telemetry_QueuePacketEx */
Telemetry_Status_t Telemetry_QueuePacketEx(TelemetryPacketID_t id, const uint8_t *payload, uint16_t len){
	CRC_HandleTypeDef *crc_handle;
	uint32_t primask;

	primask = Telemetry_EnterCritical();
	crc_handle = pCrc;
	Telemetry_ExitCritical(primask);

	if(crc_handle == NULL){
		return Telemetry_SetEnqueueStatus(TELEM_STATUS_NOT_INITIALIZED);
	}
	if(!Telemetry_IsValidPacketId(id)){
		return Telemetry_SetEnqueueStatus(TELEM_STATUS_INVALID_PACKET_ID);
	}
	if((payload == NULL) && (len > 0U)){
		return Telemetry_SetEnqueueStatus(TELEM_STATUS_INVALID_PARAM);
	}
	if(len > TELEM_PAYLOAD_SIZE){
		return Telemetry_SetEnqueueStatus(TELEM_STATUS_INVALID_PARAM);
	}

	TelemetryFrame_t frame;
	Telemetry_BuildFrame(&frame, crc_handle, id, payload, len);

	primask = Telemetry_EnterCritical();
	if(!FrameQueue_Push(&telem_queue, &frame)){
		g_telem_dropped_frames++;
		g_telem_last_enqueue_status = TELEM_STATUS_QUEUE_FULL;
		Telemetry_ExitCritical(primask);
		return TELEM_STATUS_QUEUE_FULL;
	}

	g_telem_queued_frames++;
	Telemetry_UpdateQueueDepthPeakLocked();
	g_telem_last_enqueue_status = TELEM_STATUS_OK;
	Telemetry_ExitCritical(primask);

	return TELEM_STATUS_OK;

}

/** @copydoc Telemetry_QueuePacket */
bool Telemetry_QueuePacket(TelemetryPacketID_t id, const uint8_t *payload, uint16_t len)
{
	return (Telemetry_QueuePacketEx(id, payload, len) == TELEM_STATUS_OK);
}

/**
 * @copydoc Telemetry_ProcessStep
 *
 * This is the telemetry transport owner for one queued frame. Producers only enqueue
 * packets; they do not transmit directly. Each call claims processing ownership,
 * loads at most one pending frame into the transmit buffer, snapshots the selected
 * downlink mode, attempts radio and/or UART delivery according to that policy, and
 * clears the pending frame only after the configured delivery path succeeds.
 */
Telemetry_Status_t Telemetry_ProcessStep(G2S_Link_Handle_t *g2s_link){
	UART_Driver_Status_t uart_status = UART_DRIVER_OK;
	G2S_Status_t g2s_status = G2S_STATUS_OK;
	CRC_HandleTypeDef *crc_handle;
	TelemetryDownlinkMode_t downlink_mode;
	TelemetryFrame_t frame_snapshot;
	bool send_radio;
	bool send_uart;
	bool radio_ok = true;
	bool uart_ok = true;
	uint32_t primask;

	primask = Telemetry_EnterCritical();
	crc_handle = pCrc;
	Telemetry_ExitCritical(primask);

	if(crc_handle == NULL){
		return Telemetry_SetProcessStatus(TELEM_STATUS_NOT_INITIALIZED);
	}

	primask = Telemetry_EnterCritical();
	if(process_active){
		g_telem_tx_busy_retries++;
		g_telem_last_process_status = TELEM_STATUS_TX_BUSY;
		Telemetry_ExitCritical(primask);
		return TELEM_STATUS_TX_BUSY;
	}
	process_active = true;

	if(!frame_pending){
		if(!FrameQueue_Pop(&telem_queue, &tx_frame)){
			process_active = false;
			g_telem_last_process_status = TELEM_STATUS_IDLE;
			Telemetry_ExitCritical(primask);
			return TELEM_STATUS_IDLE;
		}

		frame_pending = true;
	}

	frame_snapshot = tx_frame;
	downlink_mode = g_telem_downlink_mode;
	Telemetry_ExitCritical(primask);

	send_radio = Telemetry_DownlinkUsesRadio(downlink_mode);
	send_uart = Telemetry_DownlinkUsesUart(downlink_mode);

	if(send_radio){
		if(g2s_link == NULL){
			g2s_status = G2S_STATUS_NOT_INITIALIZED;
			radio_ok = false;
		} else {
			g2s_status = G2S_Link_SendTelemetry(g2s_link, (const uint8_t *)&frame_snapshot, (uint16_t)sizeof(frame_snapshot));
			radio_ok = (g2s_status == G2S_STATUS_OK);
		}
	}

	if(send_uart){
		uart_status = UART_WriteChannel(UART_DRIVER_CHANNEL_TELEMETRY, (uint8_t *)&frame_snapshot, (uint16_t)sizeof(frame_snapshot));
		uart_ok = (uart_status == UART_DRIVER_OK);
	}

	primask = Telemetry_EnterCritical();
	process_active = false;

	if(send_radio && radio_ok){
		g_telem_radio_sent_frames++;
	}
	if(send_uart && uart_ok){
		g_telem_uart_sent_frames++;
	}

	if((downlink_mode == TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR) &&
	   ((radio_ok && send_radio) || (uart_ok && send_uart))){
		if((send_radio && !radio_ok) || (send_uart && !uart_ok)){
			g_telem_transport_errors++;
		}
		frame_pending = false;
		g_telem_sent_frames++;
		g_telem_last_process_status = TELEM_STATUS_OK;
		Telemetry_ExitCritical(primask);
		return TELEM_STATUS_OK;
	}

	if((!send_radio || radio_ok) && (!send_uart || uart_ok)){
		frame_pending = false;
		g_telem_sent_frames++;
		g_telem_last_process_status = TELEM_STATUS_OK;
		Telemetry_ExitCritical(primask);
		return TELEM_STATUS_OK;
	}

	if((send_uart && ((uart_status == UART_DRIVER_BUSY) || (uart_status == UART_DRIVER_BUFFER_FULL))) ||
	   (send_radio && (g2s_status == G2S_STATUS_NOT_INITIALIZED))){
		g_telem_tx_busy_retries++;
		g_telem_last_process_status = TELEM_STATUS_TX_BUSY;
		Telemetry_ExitCritical(primask);
		return TELEM_STATUS_TX_BUSY;
	}

	g_telem_transport_errors++;
	g_telem_last_process_status = TELEM_STATUS_TRANSPORT_ERROR;
	Telemetry_ExitCritical(primask);
	return TELEM_STATUS_TRANSPORT_ERROR;
}

/** @copydoc Telemetry_Process */
void Telemetry_Process(G2S_Link_Handle_t *g2s_link){
	(void)Telemetry_ProcessStep(g2s_link);
}

/** @copydoc Telemetry_SetDownlinkMode */
void Telemetry_SetDownlinkMode(TelemetryDownlinkMode_t mode)
{
	uint32_t primask;

	if((mode != TELEM_DOWNLINK_RADIO_ONLY) &&
	   (mode != TELEM_DOWNLINK_UART_ONLY) &&
	   (mode != TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR)){
		return;
	}

	primask = Telemetry_EnterCritical();
	g_telem_downlink_mode = mode;
	Telemetry_ExitCritical(primask);
}

/** @copydoc Telemetry_GetDownlinkMode */
TelemetryDownlinkMode_t Telemetry_GetDownlinkMode(void)
{
	TelemetryDownlinkMode_t mode;
	uint32_t primask = Telemetry_EnterCritical();
	mode = g_telem_downlink_mode;
	Telemetry_ExitCritical(primask);
	return mode;
}

/** @copydoc Telemetry_SendSystemStatusEx */
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

/** @copydoc Telemetry_SendSystemStatus */
bool Telemetry_SendSystemStatus(uint8_t status)
{
	return (Telemetry_SendSystemStatusEx(status) == TELEM_STATUS_OK);
}

/** @copydoc Telemetry_SendADCHealthEx */
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

/** @copydoc Telemetry_SendADCHealth */
bool Telemetry_SendADCHealth(float vdda_voltage, float battery_voltage, float mcu_temp_c)
{
	return (Telemetry_SendADCHealthEx(vdda_voltage, battery_voltage, mcu_temp_c) == TELEM_STATUS_OK);
}

/** @copydoc Telemetry_SendEventEx */
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

/** @copydoc Telemetry_SendEvent */
bool Telemetry_SendEvent(TelemetryEventCode_t event_code, uint32_t event_value)
{
	return (Telemetry_SendEventEx(event_code, event_value) == TELEM_STATUS_OK);
}

/** @copydoc Telemetry_SendHeartbeatEx */
Telemetry_Status_t Telemetry_SendHeartbeatEx(void)
{
    TelemetryHeartbeatPayload_t payload;
    uint32_t primask;

    if(sizeof(payload) > TELEM_PAYLOAD_SIZE){
        return TELEM_STATUS_INVALID_PARAM;
    }

    primask = Telemetry_EnterCritical();
    payload.uptime_ms = HAL_GetTick();
    payload.queue_depth = FrameQueue_Count(&telem_queue);
    payload.frame_pending = frame_pending ? 1U : 0U;
    payload.reserved = 0U;
    Telemetry_ExitCritical(primask);

    return Telemetry_QueuePacketEx(
        TELEM_ID_HEARTBEAT,
        (uint8_t *)&payload,
        sizeof(payload)
    );
}

/** @copydoc Telemetry_SendHeartbeat */
bool Telemetry_SendHeartbeat(void)
{
	return (Telemetry_SendHeartbeatEx() == TELEM_STATUS_OK);
}

/** @copydoc Telemetry_SendCommandAckEx */
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

/** @copydoc Telemetry_SendCommandAck */
bool Telemetry_SendCommandAck(uint8_t command_id, int8_t status_code, uint32_t argument)
{
	return (Telemetry_SendCommandAckEx(command_id, status_code, argument) == TELEM_STATUS_OK);
}

/** @copydoc Telemetry_GetStats */
void Telemetry_GetStats(TelemetryStats_t *stats)
{
    uint32_t primask;

    if (stats == NULL)
    {
        return;
    }

    primask = Telemetry_EnterCritical();
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
	stats->downlink_mode = g_telem_downlink_mode;
	stats->radio_sent_frames = g_telem_radio_sent_frames;
	stats->uart_sent_frames = g_telem_uart_sent_frames;
	Telemetry_ExitCritical(primask);
}

/** @copydoc Telemetry_StatusToString */
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

/** @copydoc Telemetry_DownlinkModeToString */
const char *Telemetry_DownlinkModeToString(TelemetryDownlinkMode_t mode)
{
	switch(mode){
	case TELEM_DOWNLINK_RADIO_ONLY:
		return "RADIO_ONLY";
	case TELEM_DOWNLINK_UART_ONLY:
		return "UART_ONLY";
	case TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR:
	default:
		return "RADIO_WITH_UART_MIRROR";
	}
}
