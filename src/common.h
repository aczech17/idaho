#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC


void disable_bios_cursor(void);
void invalidate_tlb_entry(u64 virtual_address);



size_t string_len(const char* text);

void* memcpy(void* dst, const void* src, size_t count);
void* memset(void* dest, int value, size_t count);
void vga_printf(const char* format, ...);
void panic(const char* msg);

#endif // COMMON_H
