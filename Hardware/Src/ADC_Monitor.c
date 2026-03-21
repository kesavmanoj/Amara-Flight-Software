#include "adc_monitor.h"
#include <string.h>
#include "Logger.h"

/* ================= PRIVATE STATE ================= */

static ADC_HandleTypeDef *pAdc = NULL;

/* DMA buffer (written by hardware) */
static volatile uint16_t dma_buffer[ADC_CHANNELS_COUNT];

/* Processing buffer (safe copy) */
uint16_t proc_buffer[ADC_CHANNELS_COUNT];

/* Data ready flag (set in ISR context) */
static volatile bool data_ready = false;

/* ================= INTERNAL HELPERS ================= */

/**
 * @brief Calculate VDDA using VREFINT calibration
 */
static float ADC_CalcVDDA(uint16_t raw_vref)
{
    if (raw_vref == 0)
        return 0.0f;

    uint16_t vref_cal = *VREFINT_CAL_ADDR;

    /* VDDA = (VREF_CAL_VOLTAGE * VREFINT_CAL) / RAW */
    float vdda = ((float)VREFINT_CAL_VREF * (float)vref_cal) / (float)raw_vref;

    return vdda / 1000.0f; // Convert mV → V
}

/**
 * @brief Calculate temperature using factory calibration with VDDA compensation
 */
static float ADC_CalcTemperature(uint16_t raw_temp, float vdda)
{
    uint16_t ts_cal1 = *TEMPSENSOR_CAL1_ADDR;
    uint16_t ts_cal2 = *TEMPSENSOR_CAL2_ADDR;

    float vdda_cal = TEMPSENSOR_CAL_VREFANALOG / 1000.0f; // 3.3V

    /* Scale raw ADC value to calibration voltage */
    float scaled_raw = raw_temp * (vdda / vdda_cal);

    /* Linear interpolation */
    float temperature =
        ((scaled_raw - (float)ts_cal1) *
        (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP)) /
        ((float)ts_cal2 - (float)ts_cal1)
        + TEMPSENSOR_CAL1_TEMP;

    return temperature;
}

/**
 * @brief Convert raw ADC value to voltage
 */
static float ADC_CalcVoltage(uint16_t raw, float vdda)
{
    return ((float)raw / 4095.0f) * vdda;
}

/* ================= PUBLIC API ================= */

ADC_Monitor_Status_t ADC_Monitor_Init(ADC_HandleTypeDef *hadc)
{
    if (hadc == NULL)
        return ADC_MONITOR_ERROR;

    pAdc = hadc;
    data_ready = false;

    return ADC_MONITOR_OK;
}

ADC_Monitor_Status_t ADC_Monitor_Start(void)
{
    if (pAdc == NULL)
        return ADC_MONITOR_ERROR;

    if (HAL_ADC_Start_DMA(pAdc, (uint32_t*)dma_buffer, ADC_CHANNELS_COUNT) != HAL_OK)
        return ADC_MONITOR_ERROR;

    return ADC_MONITOR_OK;
}

ADC_Monitor_Status_t ADC_Monitor_GetData(ADC_HealthData_t *data)
{
    if (data == NULL)
        return ADC_MONITOR_ERROR;

    data_ready = false;

    /* Atomic snapshot (already safe due to buffer copy in ISR) */
    uint16_t raw_vref = proc_buffer[INDEX_VREFINT];
    uint16_t raw_temp = proc_buffer[INDEX_TEMP_SENSOR];
    uint16_t raw_batt = proc_buffer[INDEX_BATTERY];

    /* 1. VDDA calculation */
    float vdda = ADC_CalcVDDA(raw_vref);
    data->vdda_voltage = vdda;

    /* 2. Battery voltage */
    float v_batt = ADC_CalcVoltage(raw_batt, vdda);

    /* Apply voltage divider scaling */
    float scale = (ADC_BATTERY_R1 + ADC_BATTERY_R2) / ADC_BATTERY_R2;
    data->battery_voltage = v_batt * scale;

    /* 3. Temperature (with calibration + VDDA compensation) */
    data->mcu_temp_c = ADC_CalcTemperature(raw_temp, vdda);

    return ADC_MONITOR_OK;
}

/* ================= DMA CALLBACK ================= */
void ADC_Monitor_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc != pAdc)
        return;

    memcpy(proc_buffer, (const void*)dma_buffer, sizeof(dma_buffer));

    data_ready = true;

}

/* Once ADC & DMA is enabled, ADC conversion starts, DMA transfers
 * converted values to buffer, Once buffer is full, Interrupt is called
 * and callback runs, Where the DMA buffer contents are copied to
 * the processing buffer, set data_ready variable to true, GetData function
 * runs and checks if new data is available. If yes, it reads the copied
 * values from the processing buffer, converts raw ADC values into useful
 * physical quantities like VDDA voltage, battery voltage and temperature,
 * and returns them to the application. Meanwhile, in circular mode, DMA
 * continues filling the buffer with new ADC data in the background and
 * the cycle repeats continuously.
 */
