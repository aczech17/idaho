#include "common.h"
#include "vga.h"
#include <stdarg.h>

size_t string_len(const char* text)
{
    size_t len = 0;
    for (; *text != 0; ++text)
        ++len;
    return len;
}

void memory_copy(char* dst, const char* src, size_t count)
{
    while (count--)
        *dst++ = *src++;
}

void* memset(void* dest, int value, size_t count)
{
    u8* dst = dest;

    for (size_t i = 0; i < count; ++i)
        dst[i] = (u8)value;

    return dest;
}

void vga_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    for (; *format != 0; ++format)
    {
        if (*format != '%')
        {
            char c = *format;
            write_char(c);
            continue;
        }

        // Current *format is %, so increment
        ++format;
        // now *format is the actual specificator.

        switch (*format)
        {
            case 'i':
            case 'd':
            {
                i64 number = va_arg(args, i64);
                write_dec_signed(number);
                break;
            }
                    
            case 'u':
            {
                u64 number = va_arg(args, u64);
                write_dec_unsigned(number);
                break;
            }

            case 'x':
            {
                u64 number = va_arg(args, u64);
                write_hex(number, false, true);
                break;
            }

            case 'X':
            {
                u64 number = va_arg(args, u64);
                write_hex(number, true, true);
                break;
            }

            case 's':
            {
                char* str = va_arg(args, char*);
                write_string(str);
                break;
            }

            case 'c':
                write_char((char)va_arg(args, int));
                break;

            case 'Z': // color set
                global_vga_buffer.color = (char)va_arg(args, int);
                break;
            case 'z': // color reset
                global_vga_buffer.color = default_vga_color;
                break;
                
            default:
                write_char('%');
                --format;
                break;
        }
    }

    va_end(args);
}

void panic(const char* msg)
{
    vga_printf("\nPANIC: %s\n", msg);
    for (;;);
}
