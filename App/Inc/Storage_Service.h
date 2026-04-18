/**
 * @file Storage_Service.h
 * @brief Task-owned storage queue and persistence service for SD/FatFs operations.
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

/**
 * @brief Initialize the storage service queues and reset service state.
 *
 * This should be called before the scheduler starts or before any producer tries to
 * submit log records or SD status requests.
 */
void StorageService_Init(void);

/**
 * @brief Dedicated worker loop for a storage-owned task context.
 *
 * The project currently calls @ref StorageService_ProcessNext from StorageLogTask
 * directly, but this helper remains available when the service is hosted by a
 * standalone worker loop.
 */
void StorageService_RunTask(void);

/**
 * @brief Service one pending storage request or log record.
 *
 * This is the main task-owned execution boundary for storage work. It first drains
 * explicit command requests such as SD status or smoke tests, then appends queued
 * log records, and finally performs opportunistic flushes when the timeout expires.
 *
 * @param timeout_ms Maximum time to wait for a queued log record.
 * @return true if work was processed, false if the service was idle or not ready.
 */
bool StorageService_ProcessNext(uint32_t timeout_ms);

/**
 * @brief Request an SD-card status probe and wait for the storage task to fill it in.
 *
 * @param result Output structure written by the storage task.
 * @param timeout_ms Maximum time to wait for completion.
 * @return Request status for the command/response exchange.
 */
StorageService_Status_t StorageService_RequestSdStatus(StorageService_SdStatusResult_t *result,
                                                       uint32_t timeout_ms);

/**
 * @brief Request a simple SD/FatFs write-read smoke test through the storage task.
 *
 * @param result Output structure written by the storage task.
 * @param timeout_ms Maximum time to wait for completion.
 * @return Request status for the command/response exchange.
 */
StorageService_Status_t StorageService_RequestSmokeTest(StorageService_SmokeTestResult_t *result,
                                                        uint32_t timeout_ms);

/**
 * @brief Queue one log line for buffered persistence by the storage task.
 *
 * Producers call this instead of touching FatFs directly. The storage task later
 * dequeues the record and persists it according to the service flush/rotation policy.
 *
 * @param line Log text to copy into the bounded storage queue.
 * @param len Number of bytes from @p line to enqueue.
 * @return Queue submission status.
 */
StorageService_Status_t StorageService_EnqueueLogLine(const char *line, uint16_t len);

/**
 * @brief Snapshot current storage-service counters and media state.
 *
 * @param stats Output structure receiving the current statistics.
 */
void StorageService_GetStats(StorageService_Stats_t *stats);

/**
 * @brief Convert storage-service request status codes into strings.
 *
 * @param status Storage service status.
 * @return Constant string representation of the supplied status.
 */
const char *StorageService_StatusToString(StorageService_Status_t status);

/**
 * @brief Convert SD/FatFs operation result codes into strings.
 *
 * @param status Storage operation status.
 * @return Constant string representation of the supplied status.
 */
const char *StorageService_OpStatusToString(StorageService_OpStatus_t status);

#endif /* INC_STORAGE_SERVICE_H_ */
