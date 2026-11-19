#include "vga.h"

const char default_vga_color = 0x07; // gray on black

Vga_buffer global_vga_buffer =
{
    .row = 0,
    .col = 0,
    .color = default_vga_color,
};    

static void write_char_vga(char c, size_t row, size_t col, char color)
{
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH)
        return;

    volatile char* dest = VGA_ADDRESS + (row * VGA_WIDTH + col) * 2;
    *dest = c;
    *(dest + 1) = color;
}

static void scroll_down(void)
{
    memcpy(VGA_ADDRESS, VGA_ADDRESS + VGA_WIDTH * 2, VGA_SIZE - VGA_WIDTH * 2);

    // Clear the bottom row.
    for (size_t col = 0; col < VGA_WIDTH; ++col)
    {
        write_char_vga(' ', VGA_HEIGHT - 1, col, global_vga_buffer.color);
    }

    global_vga_buffer.row = VGA_HEIGHT - 1;
    global_vga_buffer.col = 0;
}

void clear_screen(void)
{
    for (char* dest = VGA_ADDRESS; dest < VGA_ADDRESS + VGA_SIZE; dest += 2)
    {
        *dest = ' ';
        *(dest + 1) = global_vga_buffer.color;
    }

    global_vga_buffer.col = 0;
    global_vga_buffer.row = 0;
}

void write_char(const char c)
{
    if (c == '\n')
    {
        ++global_vga_buffer.row;
        global_vga_buffer.col = 0;

        if (global_vga_buffer.row >= VGA_HEIGHT)
        {
            scroll_down();
        }

        return;
    }

    if (c == '\r')
    {
        global_vga_buffer.col = 0;
        return;
    }

    if (global_vga_buffer.col >= VGA_WIDTH)
    {
        ++global_vga_buffer.row;
        global_vga_buffer.col = 0;

        if (global_vga_buffer.row >= VGA_HEIGHT)
        {
            scroll_down();
        }
    }

    write_char_vga(c, global_vga_buffer.row, global_vga_buffer.col, global_vga_buffer.color);
    ++global_vga_buffer.col;
}

void write_string(const char* text)
{
    for (char* src = (char*)text; *src != 0; ++src)
    {
        write_char(*src);
    }
}

void write_hex(u64 number, bool digit_uppercase, bool strip)
{
    if (number == 0 && strip)
    {
        write_string("0x0");
        return;
    }

    write_string("0x");

    // 16 nibbles
    char digits[17] = {0};
    for (int i = 0; i < 16; ++i)
    {
        int shift = (15 - i) * 4;
        u64 nibble = ((number >> shift) & 0xF);
        char nibble_rep = digit_uppercase ? "0123456789ABCDEF"[nibble] : "0123456789abcdef"[nibble];
        digits[i] = nibble_rep;
    }

    if (strip)
    {
        int i;
        for (i = 0; digits[i] == '0'; ++i) {} // skip '0' digits

        write_string(&digits[i]);
        return;
    }

    write_string(digits);
}

void write_dec_unsigned(u64 number)
{
    if (number == 0)
    {
        write_char('0');
    }

    char digits[21] = {0}; // (2^64 - 1) has 20 digits.

    int i = 0;
    while (number > 0)
    {
        digits[i++] = (char)((number % 10) + '0');
        number /= 10;
    }
    
    // reverse digit buffer
    int left = 0;
    int right = i - 1;
    while (left < right)
    {
        // swap left and right
        char tmp = digits[left];
        digits[left] = digits[right];
        digits[right] = tmp;

        ++left;
        --right;
    }

    write_string(digits);
}

void write_dec_signed(i64 number)
{
    // Min i64 is a special case of a negative number, because we can't get an absolute value of min i64.
    const i64 min_i64 = (i64)0xFFFFFFFFFFFFFFFF;
    if (number == min_i64)
    {
        write_string("-9223372036854775808");
        return;
    }

    if (number < 0)
    {
        write_char('-');
        write_dec_unsigned(-number);
    }
    else
        write_dec_unsigned((u64)number);
}
