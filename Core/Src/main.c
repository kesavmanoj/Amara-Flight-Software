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
  Logger_Info("Startup status: UART=%s TELEM_UART=%s ADC_INIT=%s ADC_START=%s TELEM_BOOT=%s BOOT_EVT=%s I2C=%s OLED=%d",
		  UART_Driver_StatusToString(uart_status),
		  UART_Driver_StatusToString(telem_uart_status),
		  ADC_Monitor_StatusToString(adc_init_status),
		  ADC_Monitor_StatusToString(adc_start_status),
		  Telemetry_StatusToString(boot_telem_status),
		  Telemetry_StatusToString(boot_event_status),
		  I2C_Bus_StatusToString(i2c_bus_status),
		  oled_status);


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
	  static uint8_t telemetry_status_counter = 0;
	  uint32_t now = HAL_GetTick();
	  
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	CommandParser_Process();
	Telemetry_Process();
	HAL_IWDG_Refresh(&hiwdg);

	if((now - last_heartbeat_ms) >= 500U){
		last_heartbeat_ms = now;
		HAL_GPIO_TogglePin(LD2_HEARTBEAT_GPIO_Port, LD2_HEARTBEAT_Pin);
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
		ADC_HealthData_t adc_data;
		ADC_Monitor_Status_t adc_status = ADC_Monitor_GetData(&adc_data);

		if(adc_status == ADC_MONITOR_OK){
			Telemetry_Status_t adc_telem_status = Telemetry_SendADCHealthEx(
					adc_data.vdda_voltage,
					adc_data.battery_voltage,
					adc_data.mcu_temp_c);

			Logger_Info("ADC health: VDDA=%.3fV TEMP=%.2fC BATT=%.3fV",
					adc_data.vdda_voltage,
					adc_data.mcu_temp_c,
					adc_data.battery_voltage);
			Logger_Info("ADC telemetry queue: result=%s", Telemetry_StatusToString(adc_telem_status));
		} else {
			Telemetry_Status_t adc_error_event = Telemetry_SendEventEx(TELEM_EVENT_ADC_READ_ERROR, (uint32_t)adc_status);
			Logger_Warn("ADC health read failed: status=%s(%d)", ADC_Monitor_StatusToString(adc_status), adc_status);
			Logger_Warn("ADC error event queue: result=%s", Telemetry_StatusToString(adc_error_event));
		}

		last_adc_report_ms = now;
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
