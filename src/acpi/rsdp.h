#ifndef ACPI_RSDP_H
#define ACPI_RSDP_H

#include "../common.h"

// RSDP - root system description "pointer"
// RSDP -> RSDT/XSDT -> FADT -> DSDT -> _S5

typedef struct
{
    u8 signature[8];
    u8 checksum;
    u8 oemid[6];
    u8 revision;
    u32 rsdt_address;

    // Extension:
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
}RSDP;

void setup_rsdp(RSDP*);

#endif // ACPI_RSDP_H
