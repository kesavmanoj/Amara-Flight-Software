/**
 * @file G2S_Link.h
 * @brief Ground-to-space packet protocol and LoRa radio-link API.
 *
 * This module sits above the SX1278 radio driver and below the transport-agnostic
 * command and telemetry layers. It owns packet framing, CRC validation, command
 * packet decode, ACK generation, and telemetry/event downlink over the radio path.
 */

#ifndef INC_G2S_LINK_H_
#define INC_G2S_LINK_H_

#include "Command_List.h"
#include "SX1278.h"
#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define G2S_SYNC_WORD                0xA55AU
#define G2S_PROTOCOL_VERSION         0x01U
#define G2S_MAX_PAYLOAD_SIZE         48U
#define G2S_LOCAL_NODE_ID            0x10U
#define G2S_GROUND_NODE_ID           0x01U

typedef enum {
    G2S_PACKET_TYPE_COMMAND = 0x01,
    G2S_PACKET_TYPE_ACK     = 0x02,
    G2S_PACKET_TYPE_EVENT   = 0x03,
    G2S_PACKET_TYPE_TELEMETRY = 0x04
} G2S_PacketType_t;

typedef enum {
    G2S_STATUS_OK = 0,
    G2S_STATUS_IDLE,
    G2S_STATUS_INVALID_PARAM,
    G2S_STATUS_NOT_INITIALIZED,
    G2S_STATUS_RADIO_ERROR,
    G2S_STATUS_PACKET_TOO_SHORT,
    G2S_STATUS_INVALID_HEADER,
    G2S_STATUS_INVALID_VERSION,
    G2S_STATUS_INVALID_LENGTH,
    G2S_STATUS_CRC_MISMATCH,
    G2S_STATUS_UNSUPPORTED_TYPE,
    G2S_STATUS_COMMAND_ERROR
} G2S_Status_t;

typedef struct __attribute__((packed)) {
    uint16_t request_sequence;
    uint8_t command_id;
    int8_t status_code;
    uint32_t argument;
} G2S_CommandAckPayload_t;

typedef struct __attribute__((packed)) {
    uint16_t sync_word;
    uint8_t version;
    uint8_t packet_type;
    uint8_t source;
    uint8_t destination;
    uint16_t sequence;
    uint16_t payload_length;
    uint16_t flags;
    uint8_t payload[G2S_MAX_PAYLOAD_SIZE];
    uint32_t crc32;
} G2S_Packet_t;

typedef struct {
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t crc_failures;
    uint32_t command_packets;
    uint32_t ack_packets;
    uint32_t unsupported_packets;
    uint32_t command_failures;
    G2S_Status_t last_status;
    uint16_t last_rx_sequence;
    uint16_t next_tx_sequence;
} G2S_Stats_t;

typedef struct {
    SX1278_Handle_t *radio;
    CRC_HandleTypeDef *crc;
    G2S_Stats_t stats;
    bool initialized;
} G2S_Link_Handle_t;

/** @brief Initialize the G2S link with its radio and CRC resources. */
G2S_Status_t G2S_Link_Init(G2S_Link_Handle_t *link, SX1278_Handle_t *radio, CRC_HandleTypeDef *crc);
/** @brief Process one receive/dispatch/ack cycle for the radio command path. */
G2S_Status_t G2S_Link_Process(G2S_Link_Handle_t *link);
/** @brief Send one event packet over the G2S radio downlink. */
G2S_Status_t G2S_Link_SendEvent(G2S_Link_Handle_t *link, const uint8_t *payload, uint16_t payload_length);
/** @brief Send one telemetry packet over the G2S radio downlink. */
G2S_Status_t G2S_Link_SendTelemetry(G2S_Link_Handle_t *link, const uint8_t *payload, uint16_t payload_length);
/** @brief Snapshot current G2S counters into caller-provided storage. */
void G2S_Link_GetStats(G2S_Link_Handle_t *link, G2S_Stats_t *stats);
/** @brief Convert G2S status codes into printable strings. */
const char *G2S_StatusToString(G2S_Status_t status);

#endif /* INC_G2S_LINK_H_ */
