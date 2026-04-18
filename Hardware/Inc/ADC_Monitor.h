/**
 * @file ADC_Monitor.h
 * @brief ADC DMA health-monitor interface for battery, VDDA, and MCU temperature data.
 */

#ifndef INC_ADC_MONITOR_H_
#define INC_ADC_MONITOR_H_


#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ================= CONFIG ================= */

/* Must match ADC scan order in CubeMX */
#define ADC_CHANNELS_COUNT   3

#define INDEX_VREFINT        0
#define INDEX_TEMP_SENSOR    1
#define INDEX_BATTERY        2

/* Voltage divider (adjust based on hardware) */
#define ADC_BATTERY_R1       10000.0f
#define ADC_BATTERY_R2       10000.0f

/* ================= CALIBRATION CONSTANTS ================= */

/* Internal voltage reference */
#define VREFINT_CAL_ADDR            ((uint16_t*) (0x1FFF7A2AU))
//#define VREFINT_CAL_VREF            (3300UL)   /* mV */

/* Temperature sensor */
#define TEMPSENSOR_CAL1_ADDR        ((uint16_t*) (0x1FFF7A2CU)) /* 30°C */
#define TEMPSENSOR_CAL2_ADDR        ((uint16_t*) (0x1FFF7A2EU)) /* 110°C */

//#define TEMPSENSOR_CAL1_TEMP        (30.0f)
//#define TEMPSENSOR_CAL2_TEMP        (110.0f)
//#define TEMPSENSOR_CAL_VREFANALOG   (3300UL) /* mV */

/* ================= TYPES ================= */

typedef enum {
    ADC_MONITOR_OK = 0,
    ADC_MONITOR_ERROR,
    ADC_MONITOR_NOT_READY
} ADC_Monitor_Status_t;

typedef struct {
    float vdda_voltage;
    float battery_voltage;
    float mcu_temp_c;
} ADC_HealthData_t;

/* ================= API ================= */

/**
 * @brief Bind the ADC monitor to the ADC peripheral used for background DMA sampling.
 *
 * This one-time setup call stores the ADC handle that later DMA callbacks and control
 * functions use. It does not start conversions by itself.
 *
 * @param hadc ADC handle configured for the project health-monitor scan group.
 * @return ADC_MONITOR_OK on success, otherwise ADC_MONITOR_ERROR.
 */
ADC_Monitor_Status_t ADC_Monitor_Init(ADC_HandleTypeDef *hadc);

/**
 * @brief Start continuous ADC sampling through DMA.
 *
 * Once started, the ADC peripheral fills the internal DMA buffer in the background
 * until @ref ADC_Monitor_Stop is called. Completed conversions are copied into the
 * processing buffer by @ref ADC_Monitor_ConvCpltCallback.
 *
 * @return ADC_MONITOR_OK on success, otherwise ADC_MONITOR_ERROR.
 */
ADC_Monitor_Status_t ADC_Monitor_Start(void);

/**
 * @brief Stop background ADC DMA sampling.
 *
 * This is used during low-power preparation and other runtime transitions where
 * the ADC should no longer be updating shared sample state.
 *
 * @return ADC_MONITOR_OK on success, otherwise ADC_MONITOR_ERROR.
 */
ADC_Monitor_Status_t ADC_Monitor_Stop(void);

/**
 * @brief Read the most recent processed health sample.
 *
 * The function converts the last copied ADC values into VDDA, battery voltage, and
 * MCU temperature. It returns @ref ADC_MONITOR_NOT_READY until DMA completion has
 * produced at least one fresh processing-buffer snapshot.
 *
 * @param data Output structure that receives the converted sample.
 * @return ADC_MONITOR_OK when a fresh sample is returned.
 */
ADC_Monitor_Status_t ADC_Monitor_GetData(ADC_HealthData_t *data);

/**
 * @brief Convert ADC monitor status codes into log-friendly strings.
 *
 * @param status ADC monitor status code.
 * @return Constant string representation of the supplied status.
 */
const char *ADC_Monitor_StatusToString(ADC_Monitor_Status_t status);

/**
 * @brief DMA completion hook that snapshots raw ADC values for foreground readers.
 *
 * This callback is called from HAL ADC conversion-complete ISR context. It copies the
 * hardware-owned DMA buffer into a stable processing buffer and marks the sample as
 * ready so @ref ADC_Monitor_GetData can safely consume it later from task context.
 *
 * @param hadc ADC instance that completed the DMA transfer.
 */
void ADC_Monitor_ConvCpltCallback(ADC_HandleTypeDef *hadc);


#endif /* INC_ADC_MONITOR_H_ */
