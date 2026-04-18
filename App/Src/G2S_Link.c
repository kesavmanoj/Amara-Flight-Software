/**
 * @file G2S_Link.c
 * @brief Ground-to-space packet transport built on top of the SX1278 radio driver.
 */

#include "G2S_Link.h"
#include <string.h>

_Static_assert(((sizeof(G2S_Packet_t) - sizeof(uint32_t)) % 4U) == 0U,
               "G2S packet CRC region must stay 32-bit aligned");
_Static_assert(sizeof(G2S_CommandAckPayload_t) <= G2S_MAX_PAYLOAD_SIZE,
               "G2S command ACK payload must fit inside the packet payload region");

static G2S_Status_t G2S_Validate(const G2S_Link_Handle_t *link)
{
    if (link == NULL)
    {
        return G2S_STATUS_INVALID_PARAM;
    }

    if ((link->initialized == false) || (link->radio == NULL) || (link->crc == NULL))
    {
        return G2S_STATUS_NOT_INITIALIZED;
    }

    return G2S_STATUS_OK;
}

static G2S_Status_t G2S_ConvertRadioStatus(SX1278_Status_t status)
{
    switch (status)
    {
        case SX1278_STATUS_OK:
            return G2S_STATUS_OK;
        case SX1278_STATUS_NO_PACKET:
            return G2S_STATUS_IDLE;
        case SX1278_STATUS_IDLE:
            return G2S_STATUS_IDLE;
        case SX1278_STATUS_INVALID_PARAM:
            return G2S_STATUS_INVALID_PARAM;
        case SX1278_STATUS_NOT_INITIALIZED:
            return G2S_STATUS_NOT_INITIALIZED;
        case SX1278_STATUS_TIMEOUT:
        case SX1278_STATUS_SPI_ERROR:
        case SX1278_STATUS_VERSION_MISMATCH:
        case SX1278_STATUS_BUFFER_TOO_SMALL:
        default:
            return G2S_STATUS_RADIO_ERROR;
    }
}

static uint32_t G2S_CalculatePacketCrc(G2S_Link_Handle_t *link, const G2S_Packet_t *packet)
{
    uint32_t word_count = (sizeof(G2S_Packet_t) - sizeof(uint32_t)) / 4U;
    return HAL_CRC_Calculate(link->crc, (uint32_t *)packet, word_count);
}

static G2S_Status_t G2S_BuildPacket(G2S_Link_Handle_t *link,
                                    G2S_Packet_t *packet,
                                    G2S_PacketType_t type,
                                    uint8_t destination,
                                    const uint8_t *payload,
                                    uint16_t payload_length,
                                    uint16_t flags)
{
    if ((link == NULL) || (packet == NULL))
    {
        return G2S_STATUS_INVALID_PARAM;
    }

    if ((payload == NULL) && (payload_length > 0U))
    {
        return G2S_STATUS_INVALID_PARAM;
    }

    if (payload_length > G2S_MAX_PAYLOAD_SIZE)
    {
        return G2S_STATUS_INVALID_LENGTH;
    }

    memset(packet, 0, sizeof(*packet));
    packet->sync_word = G2S_SYNC_WORD;
    packet->version = G2S_PROTOCOL_VERSION;
    packet->packet_type = (uint8_t)type;
    packet->source = G2S_LOCAL_NODE_ID;
    packet->destination = destination;
    packet->sequence = link->stats.next_tx_sequence++;
    packet->payload_length = payload_length;
    packet->flags = flags;

    if ((payload != NULL) && (payload_length > 0U))
    {
        memcpy(packet->payload, payload, payload_length);
    }

    packet->crc32 = G2S_CalculatePacketCrc(link, packet);
    return G2S_STATUS_OK;
}

static G2S_Status_t G2S_SendPacket(G2S_Link_Handle_t *link, G2S_Packet_t *packet)
{
    SX1278_Status_t radio_status;

    if ((link == NULL) || (packet == NULL))
    {
        return G2S_STATUS_INVALID_PARAM;
    }

    radio_status = SX1278_Transmit(link->radio, (const uint8_t *)packet, (uint8_t)sizeof(*packet), 1000U);
    if (radio_status != SX1278_STATUS_OK)
    {
        link->stats.last_status = G2S_STATUS_RADIO_ERROR;
        return G2S_STATUS_RADIO_ERROR;
    }

    link->stats.tx_packets++;
    return G2S_STATUS_OK;
}

static G2S_Status_t G2S_SendTypedPacket(G2S_Link_Handle_t *link,
                                        G2S_PacketType_t packet_type,
                                        uint8_t destination,
                                        const uint8_t *payload,
                                        uint16_t payload_length)
{
    G2S_Packet_t packet;
    G2S_Status_t status = G2S_Validate(link);

    if (status != G2S_STATUS_OK)
    {
        return status;
    }

    status = G2S_BuildPacket(link,
                             &packet,
                             packet_type,
                             destination,
                             payload,
                             payload_length,
                             0U);
    if (status != G2S_STATUS_OK)
    {
        link->stats.last_status = status;
        return status;
    }

    status = G2S_SendPacket(link, &packet);
    link->stats.last_status = status;
    return status;
}

static G2S_Status_t G2S_SendCommandAckPacket(G2S_Link_Handle_t *link,
                                             uint8_t destination,
                                             uint16_t request_sequence,
                                             const Command_Result_t *command_result)
{
    G2S_Packet_t packet;
    G2S_CommandAckPayload_t payload;
    G2S_Status_t status;

    if ((link == NULL) || (command_result == NULL))
    {
        return G2S_STATUS_INVALID_PARAM;
    }

    payload.request_sequence = request_sequence;
    payload.command_id = command_result->command_id;
    payload.status_code = command_result->ack_status_code;
    payload.argument = command_result->argument;

    status = G2S_BuildPacket(link,
                             &packet,
                             G2S_PACKET_TYPE_ACK,
                             destination,
                             (const uint8_t *)&payload,
                             (uint16_t)sizeof(payload),
                             0U);
    if (status != G2S_STATUS_OK)
    {
        return status;
    }

    status = G2S_SendPacket(link, &packet);
    if (status == G2S_STATUS_OK)
    {
        link->stats.ack_packets++;
    }

    return status;
}

static G2S_Status_t G2S_VerifyPacket(G2S_Link_Handle_t *link, const G2S_Packet_t *packet)
{
    uint32_t expected_crc;

    if ((link == NULL) || (packet == NULL))
    {
        return G2S_STATUS_INVALID_PARAM;
    }

    if (packet->sync_word != G2S_SYNC_WORD)
    {
        return G2S_STATUS_INVALID_HEADER;
    }

    if (packet->version != G2S_PROTOCOL_VERSION)
    {
        return G2S_STATUS_INVALID_VERSION;
    }

    if (packet->payload_length > G2S_MAX_PAYLOAD_SIZE)
    {
        return G2S_STATUS_INVALID_LENGTH;
    }

    if ((packet->destination != G2S_LOCAL_NODE_ID) && (packet->destination != 0xFFU))
    {
        return G2S_STATUS_INVALID_HEADER;
    }

    expected_crc = G2S_CalculatePacketCrc(link, packet);
    if (expected_crc != packet->crc32)
    {
        link->stats.crc_failures++;
        return G2S_STATUS_CRC_MISMATCH;
    }

    return G2S_STATUS_OK;
}

G2S_Status_t G2S_Link_Init(G2S_Link_Handle_t *link, SX1278_Handle_t *radio, CRC_HandleTypeDef *crc)
{
    if ((link == NULL) || (radio == NULL) || (crc == NULL))
    {
        return G2S_STATUS_INVALID_PARAM;
    }

    if (radio->state.initialized == false)
    {
        return G2S_STATUS_NOT_INITIALIZED;
    }

    memset(link, 0, sizeof(*link));
    link->radio = radio;
    link->crc = crc;
    link->initialized = true;
    link->stats.next_tx_sequence = 1U;
    link->stats.last_status = G2S_STATUS_OK;

    return G2S_STATUS_OK;
}

/**
 * @brief Process one receive/command/ack cycle for the G2S link.
 *
 * This function is the wireless command-ingress step owned by CommTask. It gives the
 * radio link one opportunity to receive a packet, validate framing and CRC, translate
 * a command payload into the command-dispatch layer, and emit the matching ACK/NACK
 * response so uplink control stays synchronized with the rest of the runtime.
 */
G2S_Status_t G2S_Link_Process(G2S_Link_Handle_t *link)
{
    G2S_Packet_t rx_packet;
    uint8_t rx_length = (uint8_t)sizeof(rx_packet);
    G2S_Status_t status = G2S_Validate(link);
    Command_Result_t command_result;
    char command_line[COMMAND_MAX_LINE_LENGTH];

    if (status != G2S_STATUS_OK)
    {
        return status;
    }

    status = G2S_ConvertRadioStatus(SX1278_Receive(link->radio, (uint8_t *)&rx_packet, &rx_length));
    if (status == G2S_STATUS_IDLE)
    {
        link->stats.last_status = G2S_STATUS_IDLE;
        return G2S_STATUS_IDLE;
    }

    if (status != G2S_STATUS_OK)
    {
        link->stats.last_status = status;
        return status;
    }

    if (rx_length != sizeof(rx_packet))
    {
        link->stats.last_status = G2S_STATUS_PACKET_TOO_SHORT;
        return G2S_STATUS_PACKET_TOO_SHORT;
    }

    status = G2S_VerifyPacket(link, &rx_packet);
    if (status != G2S_STATUS_OK)
    {
        link->stats.last_status = status;
        return status;
    }

    link->stats.rx_packets++;
    link->stats.last_rx_sequence = rx_packet.sequence;

    if (rx_packet.packet_type != (uint8_t)G2S_PACKET_TYPE_COMMAND)
    {
        link->stats.unsupported_packets++;
        link->stats.last_status = G2S_STATUS_UNSUPPORTED_TYPE;
        return G2S_STATUS_UNSUPPORTED_TYPE;
    }

    if (rx_packet.payload_length >= COMMAND_MAX_LINE_LENGTH)
    {
        link->stats.command_failures++;
        link->stats.last_status = G2S_STATUS_INVALID_LENGTH;
        return G2S_STATUS_INVALID_LENGTH;
    }

    memcpy(command_line, rx_packet.payload, rx_packet.payload_length);
    command_line[rx_packet.payload_length] = '\0';

    command_result = Command_DispatchLine(command_line);
    link->stats.command_packets++;

    status = G2S_SendCommandAckPacket(link, rx_packet.source, rx_packet.sequence, &command_result);
    if (status != G2S_STATUS_OK)
    {
        link->stats.command_failures++;
        link->stats.last_status = status;
        return status;
    }

    if (command_result.status != COMMAND_STATUS_OK)
    {
        link->stats.command_failures++;
        link->stats.last_status = G2S_STATUS_COMMAND_ERROR;
        return G2S_STATUS_COMMAND_ERROR;
    }

    link->stats.last_status = G2S_STATUS_OK;
    return G2S_STATUS_OK;
}

G2S_Status_t G2S_Link_SendEvent(G2S_Link_Handle_t *link, const uint8_t *payload, uint16_t payload_length)
{
    return G2S_SendTypedPacket(link,
                               G2S_PACKET_TYPE_EVENT,
                               G2S_GROUND_NODE_ID,
                               payload,
                               payload_length);
}

G2S_Status_t G2S_Link_SendTelemetry(G2S_Link_Handle_t *link, const uint8_t *payload, uint16_t payload_length)
{
    return G2S_SendTypedPacket(link,
                               G2S_PACKET_TYPE_TELEMETRY,
                               G2S_GROUND_NODE_ID,
                               payload,
                               payload_length);
}

void G2S_Link_GetStats(G2S_Link_Handle_t *link, G2S_Stats_t *stats)
{
    if ((link == NULL) || (stats == NULL))
    {
        return;
    }

    *stats = link->stats;
}

const char *G2S_StatusToString(G2S_Status_t status)
{
    switch (status)
    {
        case G2S_STATUS_OK:
            return "OK";
        case G2S_STATUS_IDLE:
            return "IDLE";
        case G2S_STATUS_INVALID_PARAM:
            return "INVALID_PARAM";
        case G2S_STATUS_NOT_INITIALIZED:
            return "NOT_INITIALIZED";
        case G2S_STATUS_RADIO_ERROR:
            return "RADIO_ERROR";
        case G2S_STATUS_PACKET_TOO_SHORT:
            return "PACKET_TOO_SHORT";
        case G2S_STATUS_INVALID_HEADER:
            return "INVALID_HEADER";
        case G2S_STATUS_INVALID_VERSION:
            return "INVALID_VERSION";
        case G2S_STATUS_INVALID_LENGTH:
            return "INVALID_LENGTH";
        case G2S_STATUS_CRC_MISMATCH:
            return "CRC_MISMATCH";
        case G2S_STATUS_UNSUPPORTED_TYPE:
            return "UNSUPPORTED_TYPE";
        case G2S_STATUS_COMMAND_ERROR:
            return "COMMAND_ERROR";
        default:
            return "ERROR";
    }
}
