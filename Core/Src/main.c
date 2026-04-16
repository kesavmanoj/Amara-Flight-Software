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
#include "Runtime_Resources.h"
#include "Runtime_State.h"
#include "System_Runtime.h"
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

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
void RTOS_NotifyCommTaskRxFromISR(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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
  I2C_Bus_Handle_t *i2c_bus;
  OLED_HandleTypeDef *oled;
  SX1278_Handle_t *radio;
  G2S_Link_Handle_t *g2s_link;
  IPMS_Config_t *ipms_config;
  SystemRuntimeContext_t *runtime_context;
  SystemRuntimeHooks_t *runtime_hooks;

  RuntimeResources_Init();
  i2c_bus = RuntimeResources_GetI2CBus();
  oled = RuntimeResources_GetOled();
  radio = RuntimeResources_GetRadio();
  g2s_link = RuntimeResources_GetG2SLink();
  ipms_config = RuntimeResources_GetIpmsConfig();
  runtime_context = RuntimeResources_GetSystemRuntimeContext();
  runtime_hooks = RuntimeResources_GetSystemRuntimeHooks();

  *runtime_hooks = (SystemRuntimeHooks_t){
      .SystemClock_Config = SystemClock_Config,
      .MX_DMA_Init = MX_DMA_Init,
      .MX_USART1_UART_Init = MX_USART1_UART_Init,
      .MX_USART2_UART_Init = MX_USART2_UART_Init,
      .MX_I2C1_Init = MX_I2C1_Init,
      .MX_SPI1_Init = MX_SPI1_Init,
      .MX_ADC1_Init = MX_ADC1_Init,
      .MX_CRC_Init = MX_CRC_Init
  };
  RuntimeState_Init();

  UART_Driver_Status_t uart_status = UART_Driver_Init(&huart2);
  UART_Driver_Status_t telem_uart_status = UART_Driver_InitChannel(UART_DRIVER_CHANNEL_TELEMETRY, &huart1);
  CommandParser_Init();
  ADC_Monitor_Status_t adc_init_status = ADC_Monitor_Init(&hadc1);
  ADC_Monitor_Status_t adc_start_status = ADC_Monitor_Start();
  Telemetry_Init(&hcrc);
  Telemetry_SetDownlinkMode(TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR);
  Telemetry_Status_t boot_telem_status = Telemetry_SendSystemStatusEx(0x01U);
  Telemetry_Status_t boot_event_status = Telemetry_SendEventEx(TELEM_EVENT_BOOT, HAL_GetTick());
  IPMS_GetDefaultConfig(ipms_config);
  IPMS_Status_t ipms_status = IPMS_Init(ipms_config);
  SX1278_Status_t radio_status = SX1278_Init(radio, &hspi1, LORA_CS_GPIO_Port, LORA_CS_Pin, NULL, 0U, false);
  G2S_Status_t g2s_status = G2S_Link_Init(g2s_link, radio, &hcrc);

  I2C_Status_t i2c_bus_status = I2C_Bus_Init(i2c_bus, &hi2c1);
  OLED_Status_t oled_status = OLED_STATUS_INVALID_PARAM;

  runtime_context->hrtc = &hrtc;
  runtime_context->hiwdg = &hiwdg;
  runtime_context->hadc = &hadc1;
  runtime_context->hi2c = &hi2c1;
  runtime_context->hspi = &hspi1;
  runtime_context->console_uart = &huart2;
  runtime_context->telemetry_uart = &huart1;
  runtime_context->i2c_bus = i2c_bus;
  runtime_context->oled = oled;
  runtime_context->radio = radio;

  if (i2c_bus_status == I2C_OK)
  {
    oled_status = OLED_Init(oled, i2c_bus, OLED_I2C_ADDR_0x3C);
    if (oled_status == OLED_STATUS_OK){

      OLED_Clear(oled);
      OLED_SetCursor(oled, 0U, 0U);
      OLED_WriteString(oled, "CubeSat FC", OLED_COLOR_WHITE);
      OLED_SetCursor(oled, 0U, 16U);
      OLED_WriteString(oled, "OLED OK", OLED_COLOR_WHITE);
      OLED_UpdateScreen(oled);

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
              ipms_config->warn_enter_v,
              ipms_config->sleep_enter_v,
              ipms_config->stop_enter_v,
              IPMS_PolicyModeToString(IPMS_POLICY_MONITOR_ONLY),
              IPMS_SimulationModeToString(IPMS_SIMULATION_AUTO));
  Logger_Info("Telemetry downlink mode: %s", Telemetry_DownlinkModeToString(Telemetry_GetDownlinkMode()));


  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Runtime is owned by FreeRTOS tasks after osKernelStart(). */
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
    RTOS_NotifyCommTaskRxFromISR();
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    ADC_Monitor_ConvCpltCallback(hadc);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1)
    {
        RuntimeState_RecordTelemetryTxComplete();
    }

    UART_TxCpltCallback(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1)
    {
        RuntimeState_RecordTelemetryError();
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
