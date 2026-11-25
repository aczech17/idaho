#ifndef COMMAND_LINE_H
#define COMMAND_LINE_H

#include "common.h"

#define COMMAND_LINE_MAX_SIZE 50

typedef struct
{
    char data[COMMAND_LINE_MAX_SIZE + 1];
    u64 command_line_size;
    const char* prompt;
}Command_line;

void process_cmd(Command_line* cmd, u8 hid_code);

#endif // COMMAND_LINE_H
