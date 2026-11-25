#include "command_line.h"
#include "vga/vga.h"

static void clear_command_line(Command_line* cmd)
{
    memset(cmd->data, 0, COMMAND_LINE_MAX_SIZE);
    cmd->command_line_size = 0;
}

void add_letter_to_command_line(Command_line* cmd, char c)
{
    if (cmd->command_line_size < COMMAND_LINE_MAX_SIZE)
    {
        cmd->data[cmd->command_line_size++] = c;
        vga_printf("%c", c);
    }
}

void execute_command_line_command(Command_line* cmd)
{
    const char* help = "CLEAR: clears screen\nELO: prints greeting";

    if (strcmp(cmd->data, "CLEAR") == 0)
        clear_screen();
    else if (strcmp(cmd->data, "ELO") == 0)
        vga_printf("\nELO xD");
    else if (strcmp(cmd->data, "HELP") == 0)
        vga_printf("\n%s", help);
    else
        vga_printf("\nUnknown command: %s", cmd->data);

    clear_command_line(cmd);
    vga_printf("\n%s: ", cmd->prompt);
}
