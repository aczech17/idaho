#ifndef ACPI_RSDT_H
#define ACPI_RSDT_H

#include "../common.h"
#include <stdbool.h>

// Root/Extended system descriptor table
// Entries are u32 (root) or u64 (extended) based on signature
typedef struct
{
    u8 signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    u8 oem_id[6];
    u8 oem_table_id[8];
    u8 oem_revision[4];
    u8 creator_id[4];
    u8 creator_revision[4];

    bool is_extended;
    u8* entries;
}RSDT;

void setup_rsdt(RSDT* rsdt, u64 address);
u64 get_rsdt_entry(RSDT* rsdt, u64 index);

#endif // ACPI_RSDT_H
