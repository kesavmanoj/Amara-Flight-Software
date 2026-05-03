/**
 * @file Storage_Service.c
 * @brief Queue-backed SD/FatFs persistence service owned by StorageLogTask.
 */

#include "Storage_Service.h"
#include "fatfs.h"
#include "bsp_driver_sd.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define STORAGE_SERVICE_COMMAND_QUEUE_LENGTH  4U
#define STORAGE_SERVICE_LOG_QUEUE_LENGTH     16U
#define STORAGE_SERVICE_RESPONSE_FLAG   0x00000001U
#define STORAGE_SERVICE_LOG_FLUSH_INTERVAL_MS 1000U
#define STORAGE_SERVICE_LOG_FLUSH_BYTES       512U
#define STORAGE_SERVICE_LOG_MAX_RECORD_LEN    192U
#define STORAGE_SERVICE_LOG_FILE_COUNT          4U
#define STORAGE_SERVICE_LOG_FILE_MAX_BYTES   8192U

typedef enum {
    STORAGE_REQUEST_SD_STATUS = 0,
    STORAGE_REQUEST_SMOKE_TEST
} StorageService_RequestType_t;

typedef struct {
    StorageService_RequestType_t type;
    osThreadId_t requester;
    void *response;
} StorageService_Request_t;

typedef struct {
    uint16_t length;
    char line[STORAGE_SERVICE_LOG_MAX_RECORD_LEN];
} StorageService_LogRecord_t;

typedef struct {
    bool mounted;
    bool log_file_open;
    uint8_t active_log_index;
    uint32_t active_log_size;
    uint32_t bytes_since_flush;
    uint32_t pending_record_count;
    uint32_t last_flush_ms;
    FIL log_file;
    StorageService_Stats_t stats;
} StorageService_Context_t;

static osMessageQueueId_t g_storage_command_queue = NULL;
static osMessageQueueId_t g_storage_log_queue = NULL;
static StorageService_Context_t g_storage_context;

static void StorageService_ResetLogState(void)
{
    g_storage_context.log_file_open = false;
    g_storage_context.active_log_index = 0U;
    g_storage_context.active_log_size = 0U;
    g_storage_context.bytes_since_flush = 0U;
    g_storage_context.pending_record_count = 0U;
}

static void StorageService_CloseMount(void)
{
    if (g_storage_context.log_file_open)
    {
        (void)f_sync(&g_storage_context.log_file);
        (void)f_close(&g_storage_context.log_file);
        g_storage_context.log_file_open = false;
    }

    if (g_storage_context.mounted)
    {
        (void)f_mount(NULL, SDPath, 1U);
        g_storage_context.mounted = false;
    }

    g_storage_context.stats.mounted = 0U;
    g_storage_context.stats.log_file_open = 0U;
    g_storage_context.stats.active_log_file_size = 0U;
    StorageService_ResetLogState();
}

static bool StorageService_EnsureMounted(void)
{
    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
        StorageService_CloseMount();
        return false;
    }

    if (g_storage_context.mounted)
    {
        return true;
    }

    if (BSP_SD_Init() != MSD_OK)
    {
        g_storage_context.stats.service_error_count++;
        return false;
    }

    if (f_mount(&SDFatFS, SDPath, 1U) != FR_OK)
    {
        g_storage_context.stats.service_error_count++;
        return false;
    }

    g_storage_context.mounted = true;
    g_storage_context.stats.mounted = 1U;
    g_storage_context.stats.mount_count++;
    g_storage_context.last_flush_ms = HAL_GetTick();
    return true;
}

static void StorageService_BuildLogFileName(uint8_t index, char *path, size_t path_len)
{
    (void)snprintf(path, path_len, "flight_log_%u.txt", (unsigned int)index);
}

static bool StorageService_OpenExistingLogFile(uint8_t index, uint32_t existing_size)
{
    char path[24];

    StorageService_BuildLogFileName(index, path, sizeof(path));

    if (f_open(&g_storage_context.log_file, path, FA_OPEN_ALWAYS | FA_WRITE | FA_READ) != FR_OK)
    {
        g_storage_context.stats.service_error_count++;
        return false;
    }

    if (f_lseek(&g_storage_context.log_file, existing_size) != FR_OK)
    {
        (void)f_close(&g_storage_context.log_file);
        g_storage_context.stats.service_error_count++;
        return false;
    }

    g_storage_context.log_file_open = true;
    g_storage_context.active_log_index = index;
    g_storage_context.active_log_size = existing_size;
    g_storage_context.bytes_since_flush = 0U;
    g_storage_context.pending_record_count = 0U;
    g_storage_context.last_flush_ms = HAL_GetTick();
    g_storage_context.stats.log_file_open = 1U;
    g_storage_context.stats.active_log_index = index;
    g_storage_context.stats.active_log_file_size = existing_size;
    return true;
}

static bool StorageService_OpenOrCreateLogFile(void)
{
    uint8_t index;
    FILINFO file_info;
    char path[24];

    if (!StorageService_EnsureMounted())
    {
        return false;
    }

    if (g_storage_context.log_file_open)
    {
        return true;
    }

    for (index = 0U; index < STORAGE_SERVICE_LOG_FILE_COUNT; index++)
    {
        StorageService_BuildLogFileName(index, path, sizeof(path));
        if (f_stat(path, &file_info) == FR_OK)
        {
            if (file_info.fsize < STORAGE_SERVICE_LOG_FILE_MAX_BYTES)
            {
                return StorageService_OpenExistingLogFile(index, file_info.fsize);
            }
        }
        else
        {
            return StorageService_OpenExistingLogFile(index, 0U);
        }
    }

    StorageService_BuildLogFileName(0U, path, sizeof(path));
    if (f_open(&g_storage_context.log_file, path, FA_CREATE_ALWAYS | FA_WRITE | FA_READ) != FR_OK)
    {
        g_storage_context.stats.service_error_count++;
        return false;
    }

    g_storage_context.log_file_open = true;
    g_storage_context.active_log_index = 0U;
    g_storage_context.active_log_size = 0U;
    g_storage_context.bytes_since_flush = 0U;
    g_storage_context.pending_record_count = 0U;
    g_storage_context.last_flush_ms = HAL_GetTick();
    g_storage_context.stats.log_file_open = 1U;
    g_storage_context.stats.active_log_index = 0U;
    g_storage_context.stats.active_log_file_size = 0U;
    return true;
}

/**
 * @brief Flush the active log file according to size/time policy or a forced request.
 *
 * This helper is the buffered-persistence boundary. It decides when queued log bytes
 * must cross the FatFs sync boundary and closes the mount on sync failures so later
 * requests see a clean remount path.
 *
 * @param force When true, flush immediately regardless of thresholds.
 * @return true if the file is already clean or the flush succeeded.
 */
static bool StorageService_FlushLogFile(bool force)
{
    uint32_t now = HAL_GetTick();

    if (!g_storage_context.log_file_open)
    {
        return true;
    }

    if (!force)
    {
        if ((g_storage_context.bytes_since_flush < STORAGE_SERVICE_LOG_FLUSH_BYTES) &&
            ((now - g_storage_context.last_flush_ms) < STORAGE_SERVICE_LOG_FLUSH_INTERVAL_MS))
        {
            return true;
        }
    }

    if (f_sync(&g_storage_context.log_file) != FR_OK)
    {
        g_storage_context.stats.service_error_count++;
        StorageService_CloseMount();
        return false;
    }

    g_storage_context.bytes_since_flush = 0U;
    g_storage_context.pending_record_count = 0U;
    g_storage_context.last_flush_ms = now;
    g_storage_context.stats.flush_count++;
    return true;
}

/**
 * @brief Rotate to the next bounded log file when the active file reaches capacity.
 *
 * Rotation is performed inside the storage task so producers never need to reason
 * about file ownership or FatFs state.
 *
 * @return true on success, false if the rotation path failed.
 */
static bool StorageService_RotateLogFile(void)
{
    uint8_t next_index;
    char path[24];

    if (!StorageService_FlushLogFile(true))
    {
        return false;
    }

    if (g_storage_context.log_file_open)
    {
        (void)f_close(&g_storage_context.log_file);
        g_storage_context.log_file_open = false;
    }

    next_index = (uint8_t)((g_storage_context.active_log_index + 1U) % STORAGE_SERVICE_LOG_FILE_COUNT);
    StorageService_BuildLogFileName(next_index, path, sizeof(path));

    if (f_open(&g_storage_context.log_file, path, FA_CREATE_ALWAYS | FA_WRITE | FA_READ) != FR_OK)
    {
        g_storage_context.stats.service_error_count++;
        StorageService_CloseMount();
        return false;
    }

    g_storage_context.log_file_open = true;
    g_storage_context.active_log_index = next_index;
    g_storage_context.active_log_size = 0U;
    g_storage_context.bytes_since_flush = 0U;
    g_storage_context.pending_record_count = 0U;
    g_storage_context.last_flush_ms = HAL_GetTick();
    g_storage_context.stats.rotation_count++;
    g_storage_context.stats.log_file_open = 1U;
    g_storage_context.stats.active_log_index = next_index;
    g_storage_context.stats.active_log_file_size = 0U;
    return true;
}

static void StorageService_MaybeRotateLogFile(void)
{
    if (g_storage_context.active_log_size >= STORAGE_SERVICE_LOG_FILE_MAX_BYTES)
    {
        (void)StorageService_RotateLogFile();
    }
}

/**
 * @brief Append one queued log record to persistent storage.
 *
 * This function is the log-persistence execution step owned by StorageLogTask. It
 * opens or creates the active file on demand, writes the copied record, updates the
 * buffered-flush counters, and triggers file rotation when the current log reaches
 * its configured size limit.
 *
 * @param record Bounded queue record copied from the producer side.
 */
static void StorageService_AppendLogRecord(const StorageService_LogRecord_t *record)
{
    UINT bytes_written = 0U;

    if ((record == NULL) || (record->length == 0U))
    {
        return;
    }

    if (!StorageService_OpenOrCreateLogFile())
    {
        g_storage_context.stats.dropped_log_records++;
        return;
    }

    if (f_write(&g_storage_context.log_file, record->line, record->length, &bytes_written) != FR_OK)
    {
        g_storage_context.stats.service_error_count++;
        g_storage_context.stats.dropped_log_records++;
        StorageService_CloseMount();
        return;
    }

    if (bytes_written != record->length)
    {
        g_storage_context.stats.service_error_count++;
        g_storage_context.stats.dropped_log_records++;
        StorageService_CloseMount();
        return;
    }

    g_storage_context.active_log_size += bytes_written;
    g_storage_context.bytes_since_flush += bytes_written;
    g_storage_context.pending_record_count++;
    g_storage_context.stats.written_log_records++;
    g_storage_context.stats.active_log_file_size = g_storage_context.active_log_size;

    StorageService_MaybeRotateLogFile();
}

static void StorageService_HandleSdStatus(StorageService_SdStatusResult_t *result)
{
    HAL_SD_CardInfoTypeDef card_info;

    memset(result, 0, sizeof(*result));
    result->detected = BSP_SD_IsDetected();
    if (result->detected != SD_PRESENT)
    {
        result->op_status = STORAGE_OP_STATUS_NOT_DETECTED;
        StorageService_CloseMount();
        return;
    }

    result->init_status = BSP_SD_Init();
    if (result->init_status != MSD_OK)
    {
        result->op_status = STORAGE_OP_STATUS_INIT_ERROR;
        return;
    }

    result->card_state = BSP_SD_GetCardState();
    BSP_SD_GetCardInfo(&card_info);
    result->block_count = card_info.LogBlockNbr;
    result->block_size = card_info.LogBlockSize;
    result->op_status = STORAGE_OP_STATUS_OK;

    (void)StorageService_EnsureMounted();
}

static void StorageService_HandleSmokeTest(StorageService_SmokeTestResult_t *result)
{
    FIL file;
    UINT bytes_written = 0U;
    UINT bytes_read = 0U;
    char write_buffer[96];

    memset(result, 0, sizeof(*result));
    result->detected = BSP_SD_IsDetected();
    if (result->detected != SD_PRESENT)
    {
        result->op_status = STORAGE_OP_STATUS_NOT_DETECTED;
        StorageService_CloseMount();
        return;
    }

    result->init_status = BSP_SD_Init();
    if (result->init_status != MSD_OK)
    {
        result->op_status = STORAGE_OP_STATUS_INIT_ERROR;
        return;
    }

    if (!StorageService_EnsureMounted())
    {
        result->op_status = STORAGE_OP_STATUS_MOUNT_ERROR;
        result->fatfs_result = FR_NOT_READY;
        return;
    }

    (void)StorageService_FlushLogFile(true);

    result->fatfs_result = f_open(&file, "phase4_smoke.txt", FA_OPEN_ALWAYS | FA_READ | FA_WRITE);
    if (result->fatfs_result != FR_OK)
    {
        result->op_status = STORAGE_OP_STATUS_OPEN_ERROR;
        return;
    }

    (void)snprintf(write_buffer, sizeof(write_buffer), "tick=%lu\r\n", (unsigned long)HAL_GetTick());
    result->fatfs_result = f_lseek(&file, f_size(&file));
    if (result->fatfs_result == FR_OK)
    {
        result->fatfs_result = f_write(&file, write_buffer, (UINT)strlen(write_buffer), &bytes_written);
    }
    if (result->fatfs_result == FR_OK)
    {
        result->fatfs_result = f_sync(&file);
    }
    if (result->fatfs_result == FR_OK)
    {
        result->fatfs_result = f_lseek(&file, 0U);
    }
    if (result->fatfs_result == FR_OK)
    {
        memset(result->sample, 0, sizeof(result->sample));
        result->fatfs_result = f_read(&file, result->sample, sizeof(result->sample) - 1U, &bytes_read);
    }

    (void)f_close(&file);

    result->bytes_written = (uint32_t)bytes_written;
    result->bytes_read = (uint32_t)bytes_read;
    result->op_status = (result->fatfs_result == FR_OK) ? STORAGE_OP_STATUS_OK : STORAGE_OP_STATUS_IO_ERROR;
}

/** @copydoc StorageService_Init */
void StorageService_Init(void)
{
    if (g_storage_command_queue == NULL)
    {
        g_storage_command_queue = osMessageQueueNew(STORAGE_SERVICE_COMMAND_QUEUE_LENGTH,
                                                    sizeof(StorageService_Request_t),
                                                    NULL);
    }

    if (g_storage_log_queue == NULL)
    {
        g_storage_log_queue = osMessageQueueNew(STORAGE_SERVICE_LOG_QUEUE_LENGTH,
                                                sizeof(StorageService_LogRecord_t),
                                                NULL);
    }

    memset(&g_storage_context, 0, sizeof(g_storage_context));
}

/** @copydoc StorageService_RunTask */
void StorageService_RunTask(void)
{
    for (;;)
    {
        (void)StorageService_ProcessNext(osWaitForever);
    }
}

/**
 * @copydoc StorageService_ProcessNext
 *
 * Runtime ownership note:
 * - command-driven SD requests are serviced first
 * - log persistence is serviced second
 * - flush policy is evaluated even on idle timeouts so buffered data still reaches
 *   storage without requiring constant log traffic
 */
bool StorageService_ProcessNext(uint32_t timeout_ms)
{
    StorageService_Request_t request;
    StorageService_LogRecord_t log_record;

    if ((g_storage_command_queue == NULL) || (g_storage_log_queue == NULL))
    {
        return false;
    }

    if (osMessageQueueGet(g_storage_command_queue, &request, NULL, 0U) == osOK)
    {
        if (request.response != NULL)
        {
            if (request.type == STORAGE_REQUEST_SD_STATUS)
            {
                StorageService_HandleSdStatus((StorageService_SdStatusResult_t *)request.response);
            }
            else
            {
                StorageService_HandleSmokeTest((StorageService_SmokeTestResult_t *)request.response);
            }
        }

        if (request.requester != NULL)
        {
            (void)osThreadFlagsSet(request.requester, STORAGE_SERVICE_RESPONSE_FLAG);
        }

        return true;
    }

    if (osMessageQueueGet(g_storage_log_queue, &log_record, NULL, timeout_ms) == osOK)
    {
        StorageService_AppendLogRecord(&log_record);
        (void)StorageService_FlushLogFile(false);
        return true;
    }

    (void)StorageService_FlushLogFile(false);
    return false;
}

static StorageService_Status_t StorageService_SubmitRequest(StorageService_RequestType_t type,
                                                            void *response,
                                                            uint32_t timeout_ms)
{
    StorageService_Request_t request;
    uint32_t flags;

    if ((g_storage_command_queue == NULL) || (response == NULL))
    {
        return STORAGE_SERVICE_STATUS_NOT_READY;
    }

    request.type = type;
    request.requester = osThreadGetId();
    request.response = response;
    if (request.requester == NULL)
    {
        return STORAGE_SERVICE_STATUS_INVALID_PARAM;
    }

    (void)osThreadFlagsClear(STORAGE_SERVICE_RESPONSE_FLAG);

    if (osMessageQueuePut(g_storage_command_queue, &request, 0U, 0U) != osOK)
    {
        return STORAGE_SERVICE_STATUS_QUEUE_FULL;
    }

    flags = osThreadFlagsWait(STORAGE_SERVICE_RESPONSE_FLAG, osFlagsWaitAny, timeout_ms);
    if ((flags & STORAGE_SERVICE_RESPONSE_FLAG) == 0U)
    {
        return STORAGE_SERVICE_STATUS_TIMEOUT;
    }

    return STORAGE_SERVICE_STATUS_OK;
}

/** @copydoc StorageService_RequestSdStatus */
StorageService_Status_t StorageService_RequestSdStatus(StorageService_SdStatusResult_t *result,
                                                       uint32_t timeout_ms)
{
    return StorageService_SubmitRequest(STORAGE_REQUEST_SD_STATUS, result, timeout_ms);
}

/** @copydoc StorageService_RequestSmokeTest */
StorageService_Status_t StorageService_RequestSmokeTest(StorageService_SmokeTestResult_t *result,
                                                        uint32_t timeout_ms)
{
    return StorageService_SubmitRequest(STORAGE_REQUEST_SMOKE_TEST, result, timeout_ms);
}

/** @copydoc StorageService_EnqueueLogLine */
StorageService_Status_t StorageService_EnqueueLogLine(const char *line, uint16_t len)
{
    StorageService_LogRecord_t record;

    if ((g_storage_log_queue == NULL) || (line == NULL) || (len == 0U))
    {
        return STORAGE_SERVICE_STATUS_NOT_READY;
    }

    if (len > (STORAGE_SERVICE_LOG_MAX_RECORD_LEN - 1U))
    {
        len = (uint16_t)(STORAGE_SERVICE_LOG_MAX_RECORD_LEN - 1U);
    }

    memset(&record, 0, sizeof(record));
    record.length = len;
    memcpy(record.line, line, len);
    record.line[len] = '\0';

    if (osMessageQueuePut(g_storage_log_queue, &record, 0U, 0U) != osOK)
    {
        g_storage_context.stats.dropped_log_records++;
        return STORAGE_SERVICE_STATUS_QUEUE_FULL;
    }

    g_storage_context.stats.queued_log_records++;
    return STORAGE_SERVICE_STATUS_OK;
}

/** @copydoc StorageService_GetStats */
void StorageService_GetStats(StorageService_Stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    *stats = g_storage_context.stats;
    stats->mounted = g_storage_context.mounted ? 1U : 0U;
    stats->log_file_open = g_storage_context.log_file_open ? 1U : 0U;
    stats->active_log_index = g_storage_context.active_log_index;
    stats->active_log_file_size = g_storage_context.active_log_size;
}

/** @copydoc StorageService_StatusToString */
const char *StorageService_StatusToString(StorageService_Status_t status)
{
    switch (status)
    {
        case STORAGE_SERVICE_STATUS_OK:
            return "OK";
        case STORAGE_SERVICE_STATUS_NOT_READY:
            return "NOT_READY";
        case STORAGE_SERVICE_STATUS_QUEUE_FULL:
            return "QUEUE_FULL";
        case STORAGE_SERVICE_STATUS_TIMEOUT:
            return "TIMEOUT";
        case STORAGE_SERVICE_STATUS_INVALID_PARAM:
        default:
            return "INVALID_PARAM";
    }
}

/** @copydoc StorageService_OpStatusToString */
const char *StorageService_OpStatusToString(StorageService_OpStatus_t status)
{
    switch (status)
    {
        case STORAGE_OP_STATUS_OK:
            return "OK";
        case STORAGE_OP_STATUS_NOT_DETECTED:
            return "NOT_DETECTED";
        case STORAGE_OP_STATUS_INIT_ERROR:
            return "INIT_ERROR";
        case STORAGE_OP_STATUS_MOUNT_ERROR:
            return "MOUNT_ERROR";
        case STORAGE_OP_STATUS_OPEN_ERROR:
            return "OPEN_ERROR";
        case STORAGE_OP_STATUS_IO_ERROR:
        default:
            return "IO_ERROR";
    }
}
