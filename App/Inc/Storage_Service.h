/*
 * Storage_Service.h
 *
 *  Created on: 14-Apr-2026
 *      Author: Codex
 */

#ifndef INC_STORAGE_SERVICE_H_
#define INC_STORAGE_SERVICE_H_

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"
#include "ff.h"

typedef enum {
    STORAGE_SERVICE_STATUS_OK = 0,
    STORAGE_SERVICE_STATUS_NOT_READY,
    STORAGE_SERVICE_STATUS_QUEUE_FULL,
    STORAGE_SERVICE_STATUS_TIMEOUT,
    STORAGE_SERVICE_STATUS_INVALID_PARAM
} StorageService_Status_t;

typedef enum {
    STORAGE_OP_STATUS_OK = 0,
    STORAGE_OP_STATUS_NOT_DETECTED,
    STORAGE_OP_STATUS_INIT_ERROR,
    STORAGE_OP_STATUS_MOUNT_ERROR,
    STORAGE_OP_STATUS_OPEN_ERROR,
    STORAGE_OP_STATUS_IO_ERROR
} StorageService_OpStatus_t;

typedef struct {
    StorageService_OpStatus_t op_status;
    uint8_t detected;
    uint8_t init_status;
    uint8_t card_state;
    uint32_t block_count;
    uint32_t block_size;
} StorageService_SdStatusResult_t;

typedef struct {
    StorageService_OpStatus_t op_status;
    uint8_t detected;
    uint8_t init_status;
    FRESULT fatfs_result;
    uint32_t bytes_written;
    uint32_t bytes_read;
    char sample[32];
} StorageService_SmokeTestResult_t;

typedef struct {
    uint32_t queued_log_records;
    uint32_t written_log_records;
    uint32_t dropped_log_records;
    uint32_t flush_count;
    uint32_t rotation_count;
    uint32_t service_error_count;
    uint32_t mount_count;
    uint32_t active_log_file_size;
    uint8_t mounted;
    uint8_t log_file_open;
    uint8_t active_log_index;
} StorageService_Stats_t;

void StorageService_Init(void);
void StorageService_RunTask(void);
bool StorageService_ProcessNext(uint32_t timeout_ms);

StorageService_Status_t StorageService_RequestSdStatus(StorageService_SdStatusResult_t *result,
                                                       uint32_t timeout_ms);
StorageService_Status_t StorageService_RequestSmokeTest(StorageService_SmokeTestResult_t *result,
                                                        uint32_t timeout_ms);
StorageService_Status_t StorageService_EnqueueLogLine(const char *line, uint16_t len);
void StorageService_GetStats(StorageService_Stats_t *stats);

const char *StorageService_StatusToString(StorageService_Status_t status);
const char *StorageService_OpStatusToString(StorageService_OpStatus_t status);

#endif /* INC_STORAGE_SERVICE_H_ */
