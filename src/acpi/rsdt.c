#include "rsdt.h"

void setup_rsdt(RSDT* rsdt, u64 address)
{
    u8* addr = (u8*)address;

    memcpy(rsdt->signature, addr, 4);
    memcpy(&rsdt->length, addr + 4, 4);

    // vga_printf("Raw signature: %x\n", addr);
    // vga_printf("Raw length:    %x\n", *(u32*)(addr + 4));

    if (rsdt->length < 36)
        panic("Bad RSDT/XSDT length.");

    memcpy(&rsdt->revision, addr + 8, 1);
    memcpy(&rsdt->checksum, addr + 9, 1);
    memcpy(&rsdt->oem_id, addr + 10, 6);
    memcpy(&rsdt->oem_table_id, addr + 16, 8);
    memcpy(&rsdt->oem_revision, addr + 24, 4);
    memcpy(&rsdt->creator_id, addr + 28, 4);
    memcpy(&rsdt->creator_revision, addr + 32, 4);

    bool is_extended = false; // We don't want compiler warnings.
    if (memcmp(rsdt->signature, "RSDT", 4) == 0)
        is_extended = false;
    else if (memcmp(rsdt->signature, "XSDT", 4) == 0)
        is_extended = true;
    else
        panic("Bad RSDT/XSDT signature.");

    if ((!is_extended && (rsdt->length - 36) % 4 != 0) ||
        (is_extended && (rsdt->length - 36) % 8 != 0))
            panic("Bad RSDT/XSDT entry count");

    rsdt->is_extended = is_extended;

    u8 calculated_checksum = 0;
    for (u32 i = 0; i < rsdt->length; ++i)
        calculated_checksum += addr[i];

    if (calculated_checksum != 0)
        panic("Bad RSDT/XSDT checksum.");

    rsdt->entries = addr + 36;
}

/**
 Formally it always returns u64, but it might actually be u32.
**/
u64 get_rsdt_entry(RSDT* rsdt, u64 index)
{
    if (rsdt->is_extended)
    {
        u64 value;
        memcpy(&value, rsdt->entries + 8 * index, 8);
        return value;
    }

    u32 value;
    memcpy(&value, rsdt->entries + 4 * index, 4);
    return (u64)value;
}
