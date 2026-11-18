#ifndef VGA_H
#define VGA_H

#include <stdbool.h>
#include <stddef.h>
#include "../memory/memory.h"

#define VGA_ADDRESS ((char*)(0xB8000))
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_SIZE (VGA_WIDTH * VGA_HEIGHT * 2)

typedef struct
{
    size_t row, col;
    char color;
}Vga_buffer;

extern Vga_buffer global_vga_buffer;
extern const char default_vga_color;

void clear_screen(void);
void write_char(const char c);
void write_string(const char* text);
void write_hex(u64 number, bool digit_uppercase, bool strip);
void write_dec_unsigned(u64 number);
void write_dec_signed(i64 number);

#endif // VGA_H
