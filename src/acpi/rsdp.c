#include "rsdp.h"
#include <stdbool.h>

static u8 checksum(u8* addr)
{
    u8 cks = 0;
    for (u16 i = 0; i < 20; ++i)
        cks += addr[i];

    return cks;
}


static bool is_correct_rsp_addr(u8* address)
{
    const char* RSDP_SIG = "RSD PTR ";
    const u32 SIG_LEN = 8;

    return (memcmp(address, RSDP_SIG, SIG_LEN) == 0) && (checksum(address) == 0);
}

static u8* get_rsdp_addr(void)
{
    u16 ebda_base_addr = *(volatile u16*)(0x40E);
    u32 ebda_addr = ((u32)ebda_base_addr * 16);

    if (ebda_addr != 0)
        for (u16 offset = 0; offset < 1024; offset += 16)
        {
            u8* address = (u8*)(uintptr_t)(ebda_addr + offset);
            if (is_correct_rsp_addr(address))
                return address;
        }

    for (u32 addr = 0xE0000; addr < 0xFFFFF; addr += 16)
    {
        u8* address = (u8*)(uintptr_t)(addr);
        if (is_correct_rsp_addr(address))
            return address;
    }

    return 0;
}

void setup_rsdp(RSDP* rsdp)
{
    memset(rsdp, 0, sizeof(RSDP));

    u8* rsdp_addr = get_rsdp_addr();
    if (rsdp_addr == 0)
        panic("Could not find RSDP address.");

    memcpy(rsdp->signature, rsdp_addr, 8);
    memcpy(&rsdp->checksum, rsdp_addr + 8, 1);
    memcpy(rsdp->oemid, rsdp_addr + 9, 6);
    memcpy(&rsdp->revision, rsdp_addr + 15, 1);
    memcpy(&rsdp->rsdt_address, rsdp_addr + 16, 4);

    if (rsdp->revision < 2)
        return;

    memcpy(&rsdp->length, rsdp_addr + 20, 4);
    if (rsdp->length < 36)
        panic("Invalid RSDP length.");

    memcpy(&rsdp->xsdt_address, rsdp_addr + 24, 8);
    memcpy(&rsdp->extended_checksum, rsdp_addr + 32, 1);
    memcpy(&rsdp->reserved, rsdp_addr + 33, 3);

    u8 extended_checksum = 0;
    for (u32 i = 0; i < rsdp->length; ++i)
        extended_checksum += rsdp_addr[i];

    if (extended_checksum != 0)
        panic("Bad RSDP. Extended checksum not valid.");
}
