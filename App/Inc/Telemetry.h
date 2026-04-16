/**
 * @file Telemetry.h
 * @brief Telemetry framing, queueing, and downlink transport API.
 *
 * This module owns telemetry packet construction, timestamping, CRC generation,
 * queue buffering, and transport processing over configured downlink paths
 * (LoRa/G2S and/or UART mirror/fallback).
 *
 * Concurrency model:
 * - Multiple producers may enqueue telemetry from different RTOS tasks.
 * - One consumer should call @ref Telemetry_ProcessStep (currently CommTask).
 * - Internal critical sections protect queue/state updates.
 */

#ifndef INC_TELEMETRY_H_
#define INC_TELEMETRY_H_


#include "stm32f4xx_hal.h"
#include "G2S_Link.h"
#include <stdbool.h>
#include <stdint.h>


/** @brief Start-of-frame marker for telemetry frames. */
#define TELEM_SYNC_WORD    0x55AA
/** @brief Fixed payload bytes per telemetry frame. */
#define TELEM_PAYLOAD_SIZE 32
/** @brief Frame queue storage slots (usable capacity is TELEM_QUEUE_SIZE - 1). */
#define TELEM_QUEUE_SIZE   9
/** @brief Epoch base year used by telemetry RTC timestamp conversion. */
#define TELEM_TIMESTAMP_EPOCH_YEAR 2000U

/** @brief Telemetry API/process status codes. */
typedef enum {
    /** Operation succeeded. */
    TELEM_STATUS_OK = 0,
    /** No work available (e.g., empty queue in process step). */
    TELEM_STATUS_IDLE,
    /** Invalid argument or packet sizing mismatch. */
    TELEM_STATUS_INVALID_PARAM,
    /** Telemetry module not initialized (CRC handle not bound). */
    TELEM_STATUS_NOT_INITIALIZED,
    /** Unsupported packet id passed to queue API. */
    TELEM_STATUS_INVALID_PACKET_ID,
    /** Frame queue full; enqueue rejected. */
    TELEM_STATUS_QUEUE_FULL,
    /** Retryable transport condition (busy/not ready). */
    TELEM_STATUS_TX_BUSY,
    /** Non-retry transport failure. */
    TELEM_STATUS_TRANSPORT_ERROR
} Telemetry_Status_t;

/** @brief Telemetry packet identifiers serialized in TelemetryFrame_t::packet_id. */
typedef enum {
    TELEM_ID_SYSTEM_STATUS = 0x01,
    TELEM_ID_ADC_HEALTH    = 0x02,
    TELEM_ID_EVENT         = 0x03,
    TELEM_ID_HEARTBEAT     = 0x04,
    TELEM_ID_COMMAND_ACK   = 0x05
} TelemetryPacketID_t;

/** @brief Event codes used by @ref TelemetryEventPayload_t. */
typedef enum {
    TELEM_EVENT_BOOT             = 0x01,
    TELEM_EVENT_ADC_READ_ERROR   = 0x02,
    TELEM_EVENT_QUEUE_OVERFLOW   = 0x03,
    TELEM_EVENT_COMMAND_UNKNOWN  = 0x04,
    TELEM_EVENT_COMMAND_OVERFLOW = 0x05,
    TELEM_EVENT_POWER_STATE      = 0x06,
    TELEM_EVENT_POWER_MODE       = 0x07,
    TELEM_EVENT_POWER_WAKEUP     = 0x08
} TelemetryEventCode_t;

/** @brief Timestamp source selected while building a frame. */
typedef enum {
    TELEM_TIMESTAMP_SOURCE_RTC = 0,
    TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK
} TelemetryTimestampSource_t;

/** @brief Runtime output routing policy for telemetry transport. */
typedef enum {
    /** Only LoRa/G2S path is attempted. */
    TELEM_DOWNLINK_RADIO_ONLY = 0,
    /** Only UART telemetry channel is attempted. */
    TELEM_DOWNLINK_UART_ONLY,
    /** LoRa/G2S is primary; UART is attempted as mirror/fallback visibility path. */
    TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR
} TelemetryDownlinkMode_t;

/** @brief Payload format for @ref TELEM_ID_SYSTEM_STATUS. */
typedef struct __attribute__((packed)) {
    uint8_t status_code;
    uint32_t uptime_ms;
} TelemetrySystemStatusPayload_t;

/** @brief Payload format for @ref TELEM_ID_ADC_HEALTH. */
typedef struct __attribute__((packed)) {
    float vdda_voltage;
    float battery_voltage;
    float mcu_temp_c;
} TelemetryADCHealthPayload_t;

/** @brief Payload format for @ref TELEM_ID_EVENT. */
typedef struct __attribute__((packed)) {
    uint8_t event_code;
    uint32_t event_value;
} TelemetryEventPayload_t;

/** @brief Payload format for @ref TELEM_ID_HEARTBEAT. */
typedef struct __attribute__((packed)) {
    uint32_t uptime_ms;
    uint16_t queue_depth;
    uint8_t frame_pending;
    uint8_t reserved;
} TelemetryHeartbeatPayload_t;

/** @brief Payload format for @ref TELEM_ID_COMMAND_ACK. */
typedef struct __attribute__((packed)) {
    uint8_t command_id;
    int8_t status_code;
    uint16_t reserved;
    uint32_t argument;
} TelemetryCommandAckPayload_t;

/**
 * @brief Serialized telemetry wire frame.
 *
 * Wire layout: sync + timestamp + packet_id + payload_length + fixed payload + crc.
 * The CRC is computed over all preceding bytes in this struct.
 */
typedef struct __attribute__((packed)) {
    uint16_t sync_word;
    uint32_t timestamp;
    uint8_t  packet_id;
    uint8_t  payload_length;
    uint8_t  payload[TELEM_PAYLOAD_SIZE];
    uint32_t crc;
} TelemetryFrame_t;

/**
 * @brief Runtime counters and last-known process state for telemetry diagnostics.
 *
 * Counters are cumulative since @ref Telemetry_Init.
 */
typedef struct {
    uint16_t queue_depth;
    uint16_t queue_capacity;
    uint16_t max_queue_depth;
    uint32_t queued_frames;
    uint32_t sent_frames;
    uint32_t dropped_frames;
    uint32_t tx_busy_retries;
    uint32_t transport_errors;
    uint32_t rtc_fallback_count;
    uint8_t frame_pending;
    Telemetry_Status_t last_enqueue_status;
    Telemetry_Status_t last_process_status;
    TelemetryTimestampSource_t last_timestamp_source;
    TelemetryDownlinkMode_t downlink_mode;
    uint32_t radio_sent_frames;
    uint32_t uart_sent_frames;
} TelemetryStats_t;

/** @brief Initialize telemetry module state and bind CRC peripheral handle. */
void Telemetry_Init(CRC_HandleTypeDef *hcrc);

/**
 * @brief Build and enqueue one telemetry frame.
 *
 * Validation rules:
 * - @p id must be recognized telemetry packet id.
 * - @p len must be <= @ref TELEM_PAYLOAD_SIZE.
 * - @p payload may be NULL only when @p len is 0.
 *
 * On success, one frame is added to the internal queue. Actual transport is
 * performed later by @ref Telemetry_ProcessStep.
 *
 * @param id Telemetry packet identifier.
 * @param payload Payload bytes (may be NULL when len is 0).
 * @param len Payload length in bytes.
 * @return Detailed status from @ref Telemetry_Status_t.
 */
Telemetry_Status_t Telemetry_QueuePacketEx(TelemetryPacketID_t id, const uint8_t *payload, uint16_t len);

/** @brief Boolean wrapper around @ref Telemetry_QueuePacketEx. */
bool Telemetry_QueuePacket(TelemetryPacketID_t id, const uint8_t *payload, uint16_t len);

/** @brief Process one telemetry transport cycle (wrapper around @ref Telemetry_ProcessStep). */
void Telemetry_Process(G2S_Link_Handle_t *g2s_link);

/**
 * @brief Attempt to transmit one queued telemetry frame over configured downlink path(s).
 *
 * Behavior highlights:
 * - Consumes at most one queued frame per call.
 * - Retries are supported: a frame may remain pending when transport is busy.
 * - In mirror mode, success is accepted when either radio or UART path succeeds.
 * - In radio-only / uart-only mode, the selected path must succeed.
 *
 * @param g2s_link G2S link handle used when radio path is enabled.
 * @return Process/transport status.
 */
Telemetry_Status_t Telemetry_ProcessStep(G2S_Link_Handle_t *g2s_link);

/** @brief Set telemetry downlink routing mode. Invalid mode values are ignored. */
void Telemetry_SetDownlinkMode(TelemetryDownlinkMode_t mode);

/** @brief Get currently configured telemetry downlink mode. */
TelemetryDownlinkMode_t Telemetry_GetDownlinkMode(void);

/** @brief Enqueue a system-status telemetry packet. */
Telemetry_Status_t Telemetry_SendSystemStatusEx(uint8_t status);
/** @brief Enqueue an ADC-health telemetry packet. */
Telemetry_Status_t Telemetry_SendADCHealthEx(float vdda_voltage, float battery_voltage, float mcu_temp_c);
/** @brief Enqueue an event telemetry packet. */
Telemetry_Status_t Telemetry_SendEventEx(TelemetryEventCode_t event_code, uint32_t event_value);
/** @brief Enqueue a heartbeat telemetry packet. */
Telemetry_Status_t Telemetry_SendHeartbeatEx(void);
/** @brief Enqueue a command-ack telemetry packet. */
Telemetry_Status_t Telemetry_SendCommandAckEx(uint8_t command_id, int8_t status_code, uint32_t argument);


/** @brief Boolean wrapper around @ref Telemetry_SendSystemStatusEx. */
bool Telemetry_SendSystemStatus(uint8_t status);
/** @brief Boolean wrapper around @ref Telemetry_SendADCHealthEx. */
bool Telemetry_SendADCHealth(float vdda_voltage, float battery_voltage, float mcu_temp_c);
/** @brief Boolean wrapper around @ref Telemetry_SendEventEx. */
bool Telemetry_SendEvent(TelemetryEventCode_t event_code, uint32_t event_value);
/** @brief Boolean wrapper around @ref Telemetry_SendHeartbeatEx. */
bool Telemetry_SendHeartbeat(void);
/** @brief Boolean wrapper around @ref Telemetry_SendCommandAckEx. */
bool Telemetry_SendCommandAck(uint8_t command_id, int8_t status_code, uint32_t argument);
/** @brief Snapshot telemetry statistics into caller-provided struct atomically. */
void Telemetry_GetStats(TelemetryStats_t *stats);
/** @brief Convert @ref Telemetry_Status_t to a printable string. */
const char *Telemetry_StatusToString(Telemetry_Status_t status);
/** @brief Convert @ref TelemetryDownlinkMode_t to a printable string. */
const char *Telemetry_DownlinkModeToString(TelemetryDownlinkMode_t mode);


#endif /* INC_TELEMETRY_H_ */
