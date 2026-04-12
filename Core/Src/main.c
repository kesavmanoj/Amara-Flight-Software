/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "adc.h"
#include "crc.h"
#include "dma.h"
#include "fatfs.h"
#include "i2c.h"
#include "iwdg.h"
#include "rtc.h"
#include "sdio.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <string.h>
#include "Telemetry.h"
#include "ADC_Monitor.h"
#include "Logger.h"
#include "SPI_Bus.h"
#include "Ring_Buffer.h"
#include "UART_Driver.h"
#include "Command_Parser.h"
#include "I2C_Bus.h"
#include "OLED_Display.h"
#include "SX1278.h"
#include "G2S_Link.h"
#include "IPMS.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static volatile uint32_t g_telem_tx_complete_count = 0;
static volatile uint32_t g_telem_error_count = 0;
static I2C_Bus_Handle_t g_i2c1_bus;
static OLED_HandleTypeDef g_oled;
static SX1278_Handle_t g_radio;
static G2S_Link_Handle_t g_g2s_link;
static IPMS_Config_t g_ipms_config;
static ADC_HealthData_t g_latest_adc_data;
static bool g_latest_adc_valid = false;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static const char *Main_TelemetryTimestampSourceToString(TelemetryTimestampSource_t source)
{
  switch (source)
  {
    case TELEM_TIMESTAMP_SOURCE_RTC:
      return "RTC";
    case TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK:
    default:
      return "UPTIME_FALLBACK";
  }
}

static uint32_t Main_PackIpmsEventValue(const IPMS_Event_t *event)
{
  if (event == NULL)
  {
    return 0U;
  }

  switch (event->type)
  {
    case IPMS_EVENT_STATE_TRANSITION:
      return ((uint32_t)event->old_state << 24)
          | ((uint32_t)event->new_state << 16)
          | ((uint32_t)event->reason << 8)
          | (uint32_t)event->simulation_mode;

    case IPMS_EVENT_WAKEUP:
      return ((uint32_t)event->wake_source << 24)
          | ((uint32_t)event->new_state << 16)
          | ((uint32_t)event->policy_mode << 8)
          | (uint32_t)event->reason;

    case IPMS_EVENT_SIMULATION_MODE_CHANGE:
      return ((uint32_t)event->simulation_mode << 24)
          | ((uint32_t)event->old_state << 16)
          | ((uint32_t)event->policy_mode << 8)
          | (uint32_t)event->reason;

    case IPMS_EVENT_POLICY_MODE_CHANGE:
    default:
      return ((uint32_t)event->policy_mode << 24)
          | ((uint32_t)event->old_state << 16)
          | ((uint32_t)event->simulation_mode << 8)
          | (uint32_t)event->reason;
  }
}

static void Main_ReportIpmsEvents(void)
{
  IPMS_Event_t event;
  Telemetry_Status_t telem_status;

  while (IPMS_PopEvent(&event))
  {
    switch (event.type)
    {
      case IPMS_EVENT_STATE_TRANSITION:
        if ((event.new_state == IPMS_POWER_STATE_LOW_POWER_WARNING)
            || (event.new_state == IPMS_POWER_STATE_SLEEP_CANDIDATE)
            || (event.new_state == IPMS_POWER_STATE_STOP_CANDIDATE))
        {
          Logger_Warn("IPMS state: %s -> %s reason=%s measured=%.3fV effective=%.3fV",
                      IPMS_PowerStateToString(event.old_state),
                      IPMS_PowerStateToString(event.new_state),
                      IPMS_ReasonToString(event.reason),
                      event.measured_battery_voltage,
                      event.effective_battery_voltage);
        }
        else
        {
          Logger_Info("IPMS state: %s -> %s reason=%s measured=%.3fV effective=%.3fV",
                      IPMS_PowerStateToString(event.old_state),
                      IPMS_PowerStateToString(event.new_state),
                      IPMS_ReasonToString(event.reason),
                      event.measured_battery_voltage,
                      event.effective_battery_voltage);
        }

        telem_status = Telemetry_SendEventEx(TELEM_EVENT_POWER_STATE, Main_PackIpmsEventValue(&event));
        Logger_Info("IPMS telemetry event: type=POWER_STATE result=%s", Telemetry_StatusToString(telem_status));
        break;

      case IPMS_EVENT_WAKEUP:
        Logger_Info("IPMS wakeup: source=%s next_state=%s reason=%s",
                    IPMS_WakeSourceToString(event.wake_source),
                    IPMS_PowerStateToString(event.new_state),
                    IPMS_ReasonToString(event.reason));
        telem_status = Telemetry_SendEventEx(TELEM_EVENT_POWER_WAKEUP, Main_PackIpmsEventValue(&event));
        Logger_Info("IPMS telemetry event: type=POWER_WAKEUP result=%s", Telemetry_StatusToString(telem_status));
        break;

      case IPMS_EVENT_SIMULATION_MODE_CHANGE:
      case IPMS_EVENT_POLICY_MODE_CHANGE:
      default:
        Logger_Info("IPMS mode event: type=%s sim=%s policy=%s reason=%s",
                    IPMS_EventTypeToString(event.type),
                    IPMS_SimulationModeToString(event.simulation_mode),
                    IPMS_PolicyModeToString(event.policy_mode),
                    IPMS_ReasonToString(event.reason));
        telem_status = Telemetry_SendEventEx(TELEM_EVENT_POWER_MODE, Main_PackIpmsEventValue(&event));
        Logger_Info("IPMS telemetry event: type=POWER_MODE result=%s", Telemetry_StatusToString(telem_status));
        break;
    }
  }
}

static void Main_PrepareForLowPower(void)
{
  (void)ADC_Monitor_Stop();
  (void)SX1278_SetMode(&g_radio, SX1278_MODE_SLEEP);
  g_latest_adc_valid = false;
}

static void Main_RestoreAfterSleep(void)
{
  (void)ADC_Monitor_Init(&hadc1);
  (void)ADC_Monitor_Start();
  (void)SX1278_StartReceiveContinuous(&g_radio);
}

static void Main_RestoreAfterStop(void)
{
  UART_Driver_Status_t uart_status;
  UART_Driver_Status_t telem_uart_status;
  ADC_Monitor_Status_t adc_init_status;
  ADC_Monitor_Status_t adc_start_status;
  I2C_Status_t i2c_status;
  OLED_Status_t oled_status = OLED_STATUS_INVALID_PARAM;
  SX1278_Status_t radio_status;

  SystemClock_Config();
  HAL_ResumeTick();

  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_ADC1_Init();
  MX_CRC_Init();

  uart_status = UART_Driver_Init(&huart2);
  telem_uart_status = UART_Driver_InitChannel(UART_DRIVER_CHANNEL_TELEMETRY, &huart1);
  adc_init_status = ADC_Monitor_Init(&hadc1);
  adc_start_status = ADC_Monitor_Start();
  i2c_status = I2C_Bus_Init(&g_i2c1_bus, &hi2c1);
  if (i2c_status == I2C_OK)
  {
    oled_status = OLED_Init(&g_oled, &g_i2c1_bus, OLED_I2C_ADDR_0x3C);
  }

  radio_status = SX1278_Init(&g_radio, &hspi1, LORA_CS_GPIO_Port, LORA_CS_Pin, NULL, 0U, false);
  g_latest_adc_valid = false;

  Logger_Info("IPMS stop restore: UART=%s TELEM_UART=%s ADC_INIT=%s ADC_START=%s I2C=%s OLED=%d RADIO=%s",
              UART_Driver_StatusToString(uart_status),
              UART_Driver_StatusToString(telem_uart_status),
              ADC_Monitor_StatusToString(adc_init_status),
              ADC_Monitor_StatusToString(adc_start_status),
              I2C_Bus_StatusToString(i2c_status),
              oled_status,
              SX1278_StatusToString(radio_status));
}

static void Main_ExecuteIpmsAction(const IPMS_ActionRequest_t *request)
{
  IPMS_Status_t rtc_status;

  if ((request == NULL) || (request->type == IPMS_ACTION_NONE))
  {
    return;
  }

  Logger_Warn("IPMS action: %s duration=%lu ms",
              IPMS_ActionTypeToString(request->type),
              (unsigned long)request->duration_ms);

  rtc_status = IPMS_ArmRtcWakeup(&hrtc, request->duration_ms);
  if (rtc_status != IPMS_STATUS_OK)
  {
    Logger_Warn("IPMS wakeup arm failed: %s", IPMS_StatusToString(rtc_status));
    return;
  }

  Main_PrepareForLowPower();
  HAL_IWDG_Refresh(&hiwdg);
  __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
  HAL_SuspendTick();

  if (request->type == IPMS_ACTION_ENTER_SLEEP)
  {
    HAL_PWR_EnterSLEEPMode(PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    HAL_ResumeTick();
    Main_RestoreAfterSleep();
  }
  else
  {
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    Main_RestoreAfterStop();
  }

  IPMS_DisarmRtcWakeup(&hrtc);
  IPMS_RecordWakeup(IPMS_WAKE_SOURCE_UNKNOWN, HAL_GetTick());
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SDIO_SD_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  MX_CRC_Init();
  MX_IWDG_Init();
  MX_RTC_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */

  UART_Driver_Status_t uart_status = UART_Driver_Init(&huart2);
  UART_Driver_Status_t telem_uart_status = UART_Driver_InitChannel(UART_DRIVER_CHANNEL_TELEMETRY, &huart1);
  CommandParser_Init();
  ADC_Monitor_Status_t adc_init_status = ADC_Monitor_Init(&hadc1);
  ADC_Monitor_Status_t adc_start_status = ADC_Monitor_Start();
  Telemetry_Init(&hcrc);
  Telemetry_Status_t boot_telem_status = Telemetry_SendSystemStatusEx(0x01U);
  Telemetry_Status_t boot_event_status = Telemetry_SendEventEx(TELEM_EVENT_BOOT, HAL_GetTick());
  IPMS_GetDefaultConfig(&g_ipms_config);
  IPMS_Status_t ipms_status = IPMS_Init(&g_ipms_config);
  SX1278_Status_t radio_status = SX1278_Init(&g_radio, &hspi1, LORA_CS_GPIO_Port, LORA_CS_Pin, NULL, 0U, false);
  G2S_Status_t g2s_status = G2S_Link_Init(&g_g2s_link, &g_radio, &hcrc);

  I2C_Status_t i2c_bus_status = I2C_Bus_Init(&g_i2c1_bus, &hi2c1);
  OLED_Status_t oled_status = OLED_STATUS_INVALID_PARAM;

  if (i2c_bus_status == I2C_OK)
  {
    oled_status = OLED_Init(&g_oled, &g_i2c1_bus, OLED_I2C_ADDR_0x3C);
    if (oled_status == OLED_STATUS_OK){

      OLED_Clear(&g_oled);
      OLED_SetCursor(&g_oled, 0U, 0U);
      OLED_WriteString(&g_oled, "CubeSat FC", OLED_COLOR_WHITE);
      OLED_SetCursor(&g_oled, 0U, 16U);
      OLED_WriteString(&g_oled, "OLED OK", OLED_COLOR_WHITE);
      OLED_UpdateScreen(&g_oled);

    }
  }

  Logger_Info("Initialization Complete");
  Logger_Info("CLI/Logger UART=USART2 @115200, Telemetry UART=USART1 @57600");
  Logger_Info("Startup status: UART=%s TELEM_UART=%s ADC_INIT=%s ADC_START=%s TELEM_BOOT=%s BOOT_EVT=%s IPMS=%s RADIO=%s G2S=%s I2C=%s OLED=%d",
		  UART_Driver_StatusToString(uart_status),
		  UART_Driver_StatusToString(telem_uart_status),
		  ADC_Monitor_StatusToString(adc_init_status),
		  ADC_Monitor_StatusToString(adc_start_status),
		  Telemetry_StatusToString(boot_telem_status),
		  Telemetry_StatusToString(boot_event_status),
          IPMS_StatusToString(ipms_status),
		  SX1278_StatusToString(radio_status),
		  G2S_StatusToString(g2s_status),
		  I2C_Bus_StatusToString(i2c_bus_status),
		  oled_status);
  Logger_Info("IPMS defaults: warn<=%.2f sleep<=%.2f stop<=%.2f policy=%s sim=%s",
              g_ipms_config.warn_enter_v,
              g_ipms_config.sleep_enter_v,
              g_ipms_config.stop_enter_v,
              IPMS_PolicyModeToString(IPMS_POLICY_MONITOR_ONLY),
              IPMS_SimulationModeToString(IPMS_SIMULATION_AUTO));


  /* USER CODE END 2 */

  /* Init scheduler */
//  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
//  MX_FREERTOS_Init();
//
//  /* Start scheduler */
//  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  static uint32_t last_heartbeat_ms = 0;
	  static uint32_t last_telem_queue_ms = 0;
	  static uint32_t last_telem_report_ms = 0;
	  static uint32_t last_adc_report_ms = 0;
	  static uint32_t last_radio_report_ms = 0;
      static uint32_t last_power_sample_ms = 0;
	  static uint8_t telemetry_status_counter = 0;
	  uint32_t now = HAL_GetTick();
      IPMS_ActionRequest_t power_action;
	  
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	CommandParser_Process();
	Telemetry_Process();
	(void)G2S_Link_Process(&g_g2s_link);
	HAL_IWDG_Refresh(&hiwdg);
    Main_ReportIpmsEvents();

	if((now - last_heartbeat_ms) >= 500U){
		last_heartbeat_ms = now;
		HAL_GPIO_TogglePin(LD2_HEARTBEAT_GPIO_Port, LD2_HEARTBEAT_Pin);
	}

    if((now - last_power_sample_ms) >= 1000U){
        ADC_HealthData_t adc_sample;
        ADC_Monitor_Status_t adc_status = ADC_Monitor_GetData(&adc_sample);

        if(adc_status == ADC_MONITOR_OK){
            g_latest_adc_data = adc_sample;
            g_latest_adc_valid = true;
            (void)IPMS_ProcessBatterySample(adc_sample.battery_voltage, now);
        }

        last_power_sample_ms = now;
    }

	if((now - last_telem_queue_ms) >= 2000U){
		uint8_t status_code = (uint8_t)(0x10U | (telemetry_status_counter & 0x0FU));
		Telemetry_Status_t queue_status = Telemetry_SendSystemStatusEx(status_code);
		Logger_Info("Telemetry queue attempt: result=%s status=0x%02X",
				Telemetry_StatusToString(queue_status),
				status_code);
		telemetry_status_counter++;
		last_telem_queue_ms = now;
	}

	if((now - last_telem_report_ms) >= 3000U){
		TelemetryStats_t telem_stats;
		Logger_Stats_t logger_stats;
		Telemetry_Status_t heartbeat_status = Telemetry_SendHeartbeatEx();
		Telemetry_GetStats(&telem_stats);
		Logger_GetStats(&logger_stats);
		Logger_Info("Telemetry UART DMA counters: tx_complete=%lu tx_error=%lu",
				(unsigned long)g_telem_tx_complete_count,
				(unsigned long)g_telem_error_count);
		Logger_Info("Telemetry heartbeat queue: result=%s", Telemetry_StatusToString(heartbeat_status));
		Logger_Info("Telemetry stats: depth=%u/%u peak=%u queued=%lu sent=%lu dropped=%lu tx_busy=%lu tx_err=%lu rtc_fallback=%lu pending=%u last_enq=%s last_proc=%s ts_src=%s",
				(unsigned int)telem_stats.queue_depth,
				(unsigned int)telem_stats.queue_capacity,
				(unsigned int)telem_stats.max_queue_depth,
				(unsigned long)telem_stats.queued_frames,
				(unsigned long)telem_stats.sent_frames,
				(unsigned long)telem_stats.dropped_frames,
				(unsigned long)telem_stats.tx_busy_retries,
				(unsigned long)telem_stats.transport_errors,
				(unsigned long)telem_stats.rtc_fallback_count,
				(unsigned int)telem_stats.frame_pending,
				Telemetry_StatusToString(telem_stats.last_enqueue_status),
				Telemetry_StatusToString(telem_stats.last_process_status),
				Main_TelemetryTimestampSourceToString(telem_stats.last_timestamp_source));
		Logger_Info("Logger stats: attempted=%lu dropped=%lu last_uart=%s",
				(unsigned long)logger_stats.messages_attempted,
				(unsigned long)logger_stats.messages_dropped,
				UART_Driver_StatusToString(logger_stats.last_uart_status));
		last_telem_report_ms = now;
	}

	if((now - last_adc_report_ms) >= 5000U){
		if(g_latest_adc_valid){
			Telemetry_Status_t adc_telem_status = Telemetry_SendADCHealthEx(
					g_latest_adc_data.vdda_voltage,
					g_latest_adc_data.battery_voltage,
					g_latest_adc_data.mcu_temp_c);

			Logger_Info("ADC health: VDDA=%.3fV TEMP=%.2fC BATT=%.3fV",
					g_latest_adc_data.vdda_voltage,
					g_latest_adc_data.mcu_temp_c,
					g_latest_adc_data.battery_voltage);
			Logger_Info("ADC telemetry queue: result=%s", Telemetry_StatusToString(adc_telem_status));
		} else {
			Telemetry_Status_t adc_error_event = Telemetry_SendEventEx(TELEM_EVENT_ADC_READ_ERROR, (uint32_t)ADC_MONITOR_NOT_READY);
			Logger_Warn("ADC health unavailable: status=%s(%d)",
                        ADC_Monitor_StatusToString(ADC_MONITOR_NOT_READY),
                        ADC_MONITOR_NOT_READY);
			Logger_Warn("ADC error event queue: result=%s", Telemetry_StatusToString(adc_error_event));
		}

		last_adc_report_ms = now;
	}

	if((now - last_radio_report_ms) >= 4000U){
		G2S_Stats_t g2s_stats;
		uint8_t version = 0U;
		SX1278_Status_t version_status = SX1278_ReadVersion(&g_radio, &version);

		G2S_Link_GetStats(&g_g2s_link, &g2s_stats);
		Logger_Info("Radio status: sx1278=%s version_status=%s version=0x%02X mode=%d freq=%luHz",
				SX1278_StatusToString(radio_status),
				SX1278_StatusToString(version_status),
				version,
				(int)g_radio.state.mode,
				(unsigned long)g_radio.state.frequency_hz);
		Logger_Info("G2S stats: rx=%lu tx=%lu crc_fail=%lu cmd=%lu ack=%lu unsupported=%lu cmd_fail=%lu last=%s next_seq=%u last_rx_seq=%u",
				(unsigned long)g2s_stats.rx_packets,
				(unsigned long)g2s_stats.tx_packets,
				(unsigned long)g2s_stats.crc_failures,
				(unsigned long)g2s_stats.command_packets,
				(unsigned long)g2s_stats.ack_packets,
				(unsigned long)g2s_stats.unsupported_packets,
				(unsigned long)g2s_stats.command_failures,
				G2S_StatusToString(g2s_stats.last_status),
				(unsigned int)g2s_stats.next_tx_sequence,
				(unsigned int)g2s_stats.last_rx_sequence);
		last_radio_report_ms = now;
	}

    if(IPMS_GetPendingAction(&power_action)){
        Main_ExecuteIpmsAction(&power_action);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE
                              |RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  RCC_OscInitStruct.PLL.PLLR = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    UART_RxCpltCallback(huart);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    ADC_Monitor_ConvCpltCallback(hadc);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1)
    {
        g_telem_tx_complete_count++;
    }

    UART_TxCpltCallback(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1)
    {
        g_telem_error_count++;
    }

    UART_ErrorCallback(huart);
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc_handle)
{
    (void)hrtc_handle;
    IPMS_OnRtcWakeup();
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == B1_USER_BUTTON_Pin)
    {
        IPMS_OnButtonWakeup();
    }
}

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
