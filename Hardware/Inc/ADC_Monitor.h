/*
 * ADC_Monitor.h
 *
 *  Created on: 19-Mar-2026
 *      Author: KESAV
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

ADC_Monitor_Status_t ADC_Monitor_Init(ADC_HandleTypeDef *hadc);
ADC_Monitor_Status_t ADC_Monitor_Start(void);
ADC_Monitor_Status_t ADC_Monitor_GetData(ADC_HealthData_t *data);

/* DMA callback hook */
void ADC_Monitor_ConvCpltCallback(ADC_HandleTypeDef *hadc);


#endif /* INC_ADC_MONITOR_H_ */
