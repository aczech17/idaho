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

void add_letter_to_command_line(Command_line*, char);
void execute_command_line_command(Command_line*);

#endif // COMMAND_LINE_H
