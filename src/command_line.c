#include "command_line.h"
#include "vga/vga.h"

static void clear_command_line(Command_line* cmd)
{
    memset(cmd->data, 0, sizeof(cmd->data));
    cmd->command_line_size = 0;
}

static void add_letter_to_command_line(Command_line* cmd, char c)
{
    if (cmd->command_line_size < COMMAND_LINE_MAX_SIZE)
    {
        cmd->data[cmd->command_line_size++] = c;
        vga_printf("%c", c);
    }
}

static void execute_command_line_command(Command_line* cmd)
{
    const char* help = "CLEAR: clears screen\nELO: prints greeting";

    if (strcmp(cmd->data, "CLEAR") == 0)
        clear_screen();
    else if (strcmp(cmd->data, "ELO") == 0)
        vga_printf("\nELO xD");
    else if (strcmp(cmd->data, "HELP") == 0)
        vga_printf("\n%s", help);
    else if (strcmp(cmd->data, "QUIT") == 0 || strcmp(cmd->data, "EXIT") == 0)
        vga_printf("\nThere is no escape from here.");
    else
        vga_printf("\nUnknown command: %s", cmd->data);

    clear_command_line(cmd);
    vga_printf("\n%s: ", cmd->prompt);
}

void process_cmd(Command_line* cmd, u8 hid_code)
{
    enum
    {
        NONE =      0x0,
        SPACE =     0x2C,
        ENTER =     0x28,
        BACKSPACE = 0x2A,
        A =         0x04,
        Z =         0x1D,
        ZERO =      0x27,
        ONE =       0x1E,
        NINE =      0x26,
    };

    switch (hid_code)
    {
        case NONE:
            return;
        case SPACE:
            add_letter_to_command_line(cmd, ' ');
            break;
        case ENTER:
            execute_command_line_command(cmd);
            break;
        case BACKSPACE:
        {
            if (cmd->command_line_size > 0)
            {
                --cmd->command_line_size;
                cmd->data[cmd->command_line_size] = 0;
                vga_backspace();
            }
            break;
        }
        default:
        {
            char ascii_code;

            if (hid_code == ZERO)
                ascii_code = '0';
            else if (hid_code >= ONE && hid_code <= NINE)
                ascii_code = '1' + hid_code - ONE;
            else if (hid_code >= A && hid_code <= Z)
                ascii_code = 'A' + (char)hid_code - A;
            else break;

            add_letter_to_command_line(cmd, ascii_code);
            break;
        }
    }
}
