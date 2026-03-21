/*
 * Command_List.h
 *
 *  Created on: 21-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_COMMAND_LIST_H_
#define INC_COMMAND_LIST_H_

#include <stdint.h>

/* Forward declaration (avoid circular dependency) */
typedef void (*CommandHandler_t)(int argc, char *argv[]);

typedef struct {
    const char *name;
    CommandHandler_t handler;
} CommandEntry_t;

/* Exposed command table */
extern const CommandEntry_t command_table[];
extern const uint32_t command_count;

#endif /* INC_COMMAND_LIST_H_ */
