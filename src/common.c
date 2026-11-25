#include "common.h"
#include "vga/vga.h"
#include <stdarg.h>

size_t strlen(const char* str)
{
    size_t len = 0;
    for (; *str != 0; ++str)
        ++len;
    return len;
}

int strcmp(const char* str1, const char* str2)
{
    while (*str1 && *str1 == *str2)
    {
        ++str1;
        ++str2;
    }

    return (unsigned char)*str1 - (unsigned char)*str2;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    u8* b1 = (u8*)s1;
    u8* b2 = (u8*)s2;

    for (size_t i = 0; i < n; ++i)
    {
        if (*b1 < *b2)
            return -1;
        if (*b1 > *b2)
            return 1;

        ++b1;
        ++b2;
    }

    return 0;
}

void* memcpy(void* dst, const void* src, size_t count)
{
    u8* d = (u8*)dst;
    u8* s = (u8*)src;

    for (size_t i = 0; i < count; ++i)
        d[i] = s[i];

    return dst;
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
    vga_printf("\n%ZPANIC: %s\n", 0x04, msg);
    for (;;);
}
