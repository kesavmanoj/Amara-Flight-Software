/**
 * @file Command_Parser.h
 * @brief Console UART byte-stream parser that feeds command lines into dispatch.
 */

#ifndef INC_COMMAND_PARSER_H_
#define INC_COMMAND_PARSER_H_

#include <stdint.h>

/** @brief Reset the command-parser line buffer state. */
void CommandParser_Init(void);

/**
 * @brief Consume available console UART bytes and dispatch complete command lines.
 *
 * This function is normally called from CommTask after UART RX ISR context has queued
 * new bytes into the UART driver ring buffer.
 */
void CommandParser_Process(void);

#endif /* INC_COMMAND_PARSER_H_ */
