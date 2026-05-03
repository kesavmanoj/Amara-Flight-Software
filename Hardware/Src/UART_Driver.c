/**
 * @file UART_Driver.c
 * @brief UART transport implementation with ISR RX and DMA-backed TX queues.
 */
#include "UART_Driver.h"

#define UART_TX_DMA_BUFFER_SIZE 128U

typedef struct {
	UART_HandleTypeDef *huart;

	/**
	 *	This is the transmit queue for the channel, when UART functions are called, 
	 *	the bytes are pushed into this ring buffer first. That means TX is non-blocking.
	 *	the driver later pulls bytes out and sends them via DMA. 				
	 */ 
	RingBuffer_t rx_buffer;

	/**
	 * 	When a byte arrives in interrupt context: the RX callback stores it into this
	 *  ring buffer. 
	 */
	RingBuffer_t tx_buffer; 

	/**
	 * 	This is the temporary DMA staging buffer for transmission. Many bytes accumulate
	 * 	in 'tx_buffer' then when DMA is idle, the driver 'pops' up to 'UART_TX_DMA_BUFFER_SIZE'
	 * 	bytes from the 'tx_buffer' to 'tx_dma_buffer'
	 */
	uint8_t tx_dma_buffer[UART_TX_DMA_BUFFER_SIZE];

	/**
	 * 	This holds the single most recently received byte for interrupt-driven RX. 
	 * 	When it arrives HAL writes it into 'rx_byte' 
	 * 	The RX complete callback runs and the driver pushes that byte into the 'rx_buffer'
	 * 	Then it re-arms reception again into the same rx_byte
	 */
	uint8_t rx_byte;

	/*	This is the length of the currently prepared DMA chunk in tx_dma_buffer					*/
	volatile uint16_t tx_dma_len;

	/* 	This is the channel’s “DMA transmit currently active” flag.								*/
	volatile uint8_t dma_busy;

	/*	This counts how many received bytes were dropped because the RX ring buffer was full.	*/
	volatile uint32_t rx_overflow_count;

} UART_ChannelState_t;

static UART_ChannelState_t uart_channels[UART_DRIVER_CHANNEL_COUNT];

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


/* 
 *	Returns a pointer to the channel in the uart_channel array 
 *	where it is used to initialize the UART driver channel
 */

static UART_ChannelState_t *UART_GetChannel(UART_Driver_Channel_t channel)
{
	if(channel >= UART_DRIVER_CHANNEL_COUNT){
		return NULL;
	}

	return &uart_channels[channel];
}

static UART_ChannelState_t *UART_GetConsoleChannel(void)
{
	return UART_GetChannel(UART_DRIVER_CHANNEL_CONSOLE);
}

/*
 *	Returns the driver channel state bound to the given HAL UART handle.
 */

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
static UART_Driver_Status_t UART_StartTxDMA(UART_ChannelState_t *channel)
{
	uint16_t chunk_len = 0U;
	bool start_transfer = false;

	if((channel == NULL) || (channel->huart == NULL)){
		return UART_DRIVER_INVALID_PARAM;
	}

	/*
	 * Protect per-channel ring-buffer state from concurrent access between the
	 * superloop and DMA completion IRQs.
	 */
	uint32_t primask = UART_EnterCritical();

	if(channel->dma_busy != 0U){
		UART_ExitCritical(primask);
		return UART_DRIVER_OK;
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
		return UART_DRIVER_OK;
	}

	/*
	 * Keep the loaded DMA chunk in tx_dma_buffer until the transfer start
	 * succeeds. If HAL returns busy/error, the same chunk will be retried on
	 * the next UART_Write() or UART callback without losing ordering.
	 */
	HAL_StatusTypeDef hal_status = HAL_UART_Transmit_DMA(channel->huart, channel->tx_dma_buffer, chunk_len);
	if(hal_status == HAL_OK){
		primask = UART_EnterCritical();
		channel->tx_dma_len = 0U;
		UART_ExitCritical(primask);
		return UART_DRIVER_OK;
	} else {
		primask = UART_EnterCritical();
		channel->dma_busy = 0U;
		UART_ExitCritical(primask);
		return (hal_status == HAL_BUSY) ? UART_DRIVER_BUSY : UART_DRIVER_ERROR;
	}
}

// API FUNCTIONS

/** @copydoc UART_Driver_Init */
UART_Driver_Status_t UART_Driver_Init(UART_HandleTypeDef *huart){
	return UART_Driver_InitChannel(UART_DRIVER_CHANNEL_CONSOLE, huart);
}


/*
	Gets the address of a channel in the uart_channel array and then
	initialises the rest of the struct, if the channel is a console channel
	then initialises a ringbuffer and enables its recieve interrupt
*/

/**
 * @copydoc UART_Driver_InitChannel
 *
 * Communication-spine note:
 * - the console channel feeds CommandParser_Process()
 * - the telemetry channel is used by Telemetry_ProcessStep() for UART downlink
 */
UART_Driver_Status_t UART_Driver_InitChannel(UART_Driver_Channel_t channel, UART_HandleTypeDef *huart){
	UART_ChannelState_t *state = UART_GetChannel(channel);

	if((state == NULL) || (huart == NULL)) return UART_DRIVER_INVALID_PARAM;

	RingBuffer_Init(&state->tx_buffer);
	RingBuffer_Init(&state->rx_buffer);
	state->huart = huart;
	state->rx_byte = 0U;
	state->tx_dma_len = 0U;
	state->dma_busy = 0U;
	state->rx_overflow_count = 0U;

	if(channel == UART_DRIVER_CHANNEL_CONSOLE){
		HAL_StatusTypeDef hal_status = HAL_UART_Receive_IT(huart, &state->rx_byte, 1);
		if(hal_status != HAL_OK) return (hal_status == HAL_BUSY) ? UART_DRIVER_BUSY : UART_DRIVER_ERROR;
	}

	return UART_DRIVER_OK;
}

// RX (the public RX API still exposes only the console channel)
/** @copydoc UART_ReadByte */
bool UART_ReadByte(uint8_t *data){
	UART_ChannelState_t *state = UART_GetConsoleChannel();

	if(state == NULL){
		return false;	
	}

	return RingBuffer_Pop(&state->rx_buffer, data);
}

/** @copydoc UART_Available */
uint16_t UART_Available(void){
	UART_ChannelState_t *state = UART_GetConsoleChannel();

	if(state == NULL){
		return 0U;
	}

	return RingBuffer_Available(&state->rx_buffer);
}

/** @copydoc UART_GetRxOverflowCount */
uint32_t UART_GetRxOverflowCount(void){
	UART_ChannelState_t *state = UART_GetConsoleChannel();

	return (state == NULL) ? 0U : state->rx_overflow_count;
}

// TX
/** @copydoc UART_Write */
UART_Driver_Status_t UART_Write(uint8_t *data, uint16_t len){
	return UART_WriteChannel(UART_DRIVER_CHANNEL_CONSOLE, data, len);
}

/**
 * @copydoc UART_WriteChannel
 *
 * This is the primary non-blocking TX queue boundary for both console/log output and
 * the UART telemetry downlink path.
 */
UART_Driver_Status_t UART_WriteChannel(UART_Driver_Channel_t channel, uint8_t *data, uint16_t len){
	uint32_t primask;
	UART_ChannelState_t *state = UART_GetChannel(channel);
	UART_Driver_Status_t start_status;

	if(state == NULL) return UART_DRIVER_INVALID_PARAM;
	if(state->huart == NULL) return UART_DRIVER_NOT_INITIALIZED;
	if((data == NULL) || (len == 0U)) return UART_DRIVER_INVALID_PARAM;

	/* Buffer handling stays inside the UART driver so higher layers remain non-blocking. */
	primask = UART_EnterCritical();
	if(!RingBuffer_PushArray(&state->tx_buffer, data, len)){
		UART_ExitCritical(primask);
		return UART_DRIVER_BUFFER_FULL;
	}
	UART_ExitCritical(primask);

	start_status = UART_StartTxDMA(state);
	if(start_status == UART_DRIVER_ERROR){
		return UART_DRIVER_ERROR;
	}
	if(start_status == UART_DRIVER_BUSY){
		return UART_DRIVER_BUSY;
	}

	return UART_DRIVER_OK;
}

/** @copydoc UART_IsChannelIdle */
bool UART_IsChannelIdle(UART_Driver_Channel_t channel)
{
	UART_ChannelState_t *state = UART_GetChannel(channel);
	bool is_idle;
	uint32_t primask;

	if((state == NULL) || (state->huart == NULL)){
		return true;
	}

	primask = UART_EnterCritical();
	is_idle = (state->dma_busy == 0U) && RingBuffer_IsEmpty(&state->tx_buffer) && (state->tx_dma_len == 0U);
	UART_ExitCritical(primask);
	return is_idle;
}

/** @copydoc UART_IsIdle */
bool UART_IsIdle(void)
{
	return UART_IsChannelIdle(UART_DRIVER_CHANNEL_CONSOLE);
}

/** @copydoc UART_WriteString */
UART_Driver_Status_t UART_WriteString(const char *str){
	if(str == NULL) return UART_DRIVER_INVALID_PARAM;

	return UART_Write((uint8_t *)str, (uint16_t)strlen(str));
}

/** @copydoc UART_Driver_StatusToString */
const char *UART_Driver_StatusToString(UART_Driver_Status_t status)
{
	switch(status){
	case UART_DRIVER_OK:
		return "OK";
	case UART_DRIVER_INVALID_PARAM:
		return "INVALID_PARAM";
	case UART_DRIVER_NOT_INITIALIZED:
		return "NOT_INITIALIZED";
	case UART_DRIVER_BUFFER_FULL:
		return "BUFFER_FULL";
	case UART_DRIVER_BUSY:
		return "BUSY";
	case UART_DRIVER_ERROR:
	default:
		return "ERROR";
	}
}

// IRQ Handler
/**
 * @copydoc UART_RxCpltCallback
 *
 * Runtime ownership note:
 * - ISR context only stores one byte and rearms reception
 * - CommTask later consumes queued bytes in task context through CommandParser_Process()
 */
void UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	UART_ChannelState_t *state = UART_GetConsoleChannel();

	if((state == NULL) || (huart != state->huart)){
		return;
	}

	/* Store byte ONLY */
	if(!RingBuffer_Push(&state->rx_buffer, state->rx_byte)){
		state->rx_overflow_count++;
	}

	/* Restart RX */
	if (HAL_UART_Receive_IT(huart, &state->rx_byte, 1) != HAL_OK)
	{
		HAL_UART_AbortReceive(huart);
		HAL_UART_Receive_IT(huart, &state->rx_byte, 1);
	}
}

/** @copydoc UART_TxCpltCallback */
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

/** @copydoc UART_ErrorCallback */
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


