/*
 * Command_List.h
 *
 *  Created on: 21-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_COMMAND_LIST_H_
#define INC_COMMAND_LIST_H_

#include <stdint.h>

#define COMMAND_MAX_LINE_LENGTH 128U

typedef enum {
    COMMAND_STATUS_OK = 0,
    COMMAND_STATUS_UNKNOWN,
    COMMAND_STATUS_MISSING_ARGUMENT,
    COMMAND_STATUS_INVALID_ARGUMENT,
    COMMAND_STATUS_NOT_READY,
    COMMAND_STATUS_IO_ERROR
} Command_Status_t;

typedef struct {
    uint8_t command_id;
    int8_t ack_status_code;
    uint32_t argument;
    Command_Status_t status;
} Command_Result_t;

/* Forward declaration (avoid circular dependency) */
typedef Command_Status_t (*CommandHandler_t)(int argc, char *argv[], Command_Result_t *result);

typedef struct {
    const char *name;
    CommandHandler_t handler;
} CommandEntry_t;

/* Exposed command table */
extern const CommandEntry_t command_table[];
extern const uint32_t command_count;

Command_Result_t Command_DispatchLine(const char *cmd_line);
const char *Command_StatusToString(Command_Status_t status);

#endif /* INC_COMMAND_LIST_H_ */
