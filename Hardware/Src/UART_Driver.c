/*
 * UART_Driver.c
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
 */
#include "UART_Driver.h"

#define UART_TX_DMA_BUFFER_SIZE 128U

typedef struct {
	UART_HandleTypeDef *huart;
	RingBuffer_t tx_buffer;
	uint8_t tx_dma_buffer[UART_TX_DMA_BUFFER_SIZE];
	volatile uint16_t tx_dma_len;
	volatile uint8_t dma_busy;
} UART_ChannelState_t;

static UART_ChannelState_t uart_channels[UART_DRIVER_CHANNEL_COUNT];
static UART_HandleTypeDef *pConsoleUart = NULL;
static RingBuffer_t rx_buffer;
static uint8_t rx_byte;

static uint32_t UART_EnterCritical(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}

static void UART_ExitCritical(uint32_t primask)
{
	if(primask == 0U){
		__enable_irq();
	}
}

static UART_ChannelState_t *UART_GetChannel(UART_Driver_Channel_t channel)
{
	if(channel >= UART_DRIVER_CHANNEL_COUNT){
		return NULL;
	}

	return &uart_channels[channel];
}

static UART_ChannelState_t *UART_FindChannel(UART_HandleTypeDef *huart)
{
	uint32_t i;

	if(huart == NULL){
		return NULL;
	}

	for(i = 0U; i < (uint32_t)UART_DRIVER_CHANNEL_COUNT; i++){
		if(uart_channels[i].huart == huart){
			return &uart_channels[i];
		}
	}

	return NULL;
}

/*
 * DMA flow:
 * 1. Writers enqueue bytes into the selected channel's tx_buffer.
 * 2. UART_StartTxDMA() loads one chunk into that channel's tx_dma_buffer when idle.
 * 3. HAL_UART_Transmit_DMA() sends the chunk asynchronously on that UART.
 * 4. HAL_UART_TxCpltCallback() clears the channel's dma_busy flag and starts the next chunk.
 */
static void UART_StartTxDMA(UART_ChannelState_t *channel)
{
	uint16_t chunk_len = 0U;
	bool start_transfer = false;

	if((channel == NULL) || (channel->huart == NULL)){
		return;
	}

	/*
	 * Protect per-channel ring-buffer state from concurrent access between the
	 * superloop and DMA completion IRQs.
	 */
	uint32_t primask = UART_EnterCritical();

	if(channel->dma_busy != 0U){
		UART_ExitCritical(primask);
		return;
	}

	if(channel->tx_dma_len == 0U){
		channel->tx_dma_len = RingBuffer_PopArray(&channel->tx_buffer, channel->tx_dma_buffer, UART_TX_DMA_BUFFER_SIZE);
	}

	chunk_len = channel->tx_dma_len;
	if(chunk_len > 0U){
		channel->dma_busy = 1U;
		start_transfer = true;
	}
	UART_ExitCritical(primask);

	if(!start_transfer){
		return;
	}

	/*
	 * Keep the loaded DMA chunk in tx_dma_buffer until the transfer start
	 * succeeds. If HAL returns busy/error, the same chunk will be retried on
	 * the next UART_Write() or UART callback without losing ordering.
	 */
	if(HAL_UART_Transmit_DMA(channel->huart, channel->tx_dma_buffer, chunk_len) == HAL_OK){
		primask = UART_EnterCritical();
		channel->tx_dma_len = 0U;
		UART_ExitCritical(primask);
	} else {
		primask = UART_EnterCritical();
		channel->dma_busy = 0U;
		UART_ExitCritical(primask);
	}
}

// API FUNCTIONS

UART_Driver_Status_t UART_Driver_Init(UART_HandleTypeDef *huart){
	return UART_Driver_InitChannel(UART_DRIVER_CHANNEL_CONSOLE, huart);
}

UART_Driver_Status_t UART_Driver_InitChannel(UART_Driver_Channel_t channel, UART_HandleTypeDef *huart){
	UART_ChannelState_t *state = UART_GetChannel(channel);

	if((state == NULL) || (huart == NULL)) return UART_DRIVER_ERROR;

	RingBuffer_Init(&state->tx_buffer);
	state->huart = huart;
	state->tx_dma_len = 0U;
	state->dma_busy = 0U;

	if(channel == UART_DRIVER_CHANNEL_CONSOLE){
		RingBuffer_Init(&rx_buffer);
		pConsoleUart = huart;

		if(HAL_UART_Receive_IT(pConsoleUart, &rx_byte, 1) != HAL_OK) return UART_DRIVER_ERROR;
	}

	return UART_DRIVER_OK;
}

// RX
bool UART_ReadByte(uint8_t *data){
	return RingBuffer_Pop(&rx_buffer, data);
}

uint16_t UART_Available(void){
	return RingBuffer_Available(&rx_buffer);
}

// TX
UART_Driver_Status_t UART_Write(uint8_t *data, uint16_t len){
	return UART_WriteChannel(UART_DRIVER_CHANNEL_CONSOLE, data, len);
}

UART_Driver_Status_t UART_WriteChannel(UART_Driver_Channel_t channel, uint8_t *data, uint16_t len){
	uint32_t primask;
	UART_ChannelState_t *state = UART_GetChannel(channel);

	if((state == NULL) || (state->huart == NULL) || (data == NULL) || (len == 0U)) return UART_DRIVER_ERROR;

	/* Buffer handling stays inside the UART driver so higher layers remain non-blocking. */
	primask = UART_EnterCritical();
	if(!RingBuffer_PushArray(&state->tx_buffer, data, len)){
		UART_ExitCritical(primask);
		return UART_DRIVER_ERROR;
	}
	UART_ExitCritical(primask);

	UART_StartTxDMA(state);

	return UART_DRIVER_OK;
}
UART_Driver_Status_t UART_WriteString(const char *str){
	if(str == NULL) return UART_DRIVER_ERROR;

	return UART_Write((uint8_t *)str, (uint16_t)strlen(str));
}

// IRQ Handler
void UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != pConsoleUart)
        return;

    /* Store byte ONLY */
    RingBuffer_Push(&rx_buffer, rx_byte);

    /* Restart RX */
    if (HAL_UART_Receive_IT(pConsoleUart, &rx_byte, 1) != HAL_OK)
    {
        HAL_UART_AbortReceive(pConsoleUart);
        HAL_UART_Receive_IT(pConsoleUart, &rx_byte, 1);
    }
}

void UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	uint32_t primask;
	UART_ChannelState_t *state = UART_FindChannel(huart);

	if(state == NULL){
		return;
	}

	/*
	 * The DMA engine finished one chunk. Mark it idle, then kick the next
	 * chunk immediately so queued writers can stream continuously.
	 */
	primask = UART_EnterCritical();
	state->dma_busy = 0U;
	UART_ExitCritical(primask);

	UART_StartTxDMA(state);
}

void UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	uint32_t primask;
	UART_ChannelState_t *state = UART_FindChannel(huart);

	if(state == NULL){
		return;
	}

	/*
	 * On UART/DMA error, release the busy flag and retry any remaining queued
	 * data. The currently active chunk is considered failed and is not rebuilt
	 * here because higher layers already operate on queued writes.
	 */
	primask = UART_EnterCritical();
	state->dma_busy = 0U;
	UART_ExitCritical(primask);

	UART_StartTxDMA(state);
}








