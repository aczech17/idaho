#include "rsdp.h"

static u8* get_rsdp_addr(void)
{
    const char* RSDP_SIG = "RSD PTR ";
    const u32 SIG_LEN = 8;

    u16 ebda_base_addr = *(volatile u16*)(0x40E);
    u32 ebda_addr = ((u32)ebda_base_addr * 16);

    if (ebda_addr != 0)
        for (u16 offset = 0; offset < 1024; offset += 16)
        {
            u32 address = ebda_addr + offset;
            char* potential_rsdp_sig = (char*)(uintptr_t)(address);

            if (memcmp(potential_rsdp_sig, RSDP_SIG, SIG_LEN) == 0)
                return (u8*)(uintptr_t)address;
        }

    for (u32 addr = 0xE0000; addr < 0xFFFFF; addr += 16)
    {
        char* potential_rsdp_sig = (char*)(uintptr_t)(addr);

        if (memcmp(potential_rsdp_sig, RSDP_SIG, SIG_LEN) == 0)
            return (u8*)(uintptr_t)addr;
    }

    return 0;
}

void setup_rsdp(RSDP* rsdp)
{
    memset(rsdp, 0, sizeof(RSDP));

    u8* rsdp_addr = get_rsdp_addr();
    if (rsdp_addr == 0)
        panic("Could not find RSDP address.");


    u8 checksum = 0;
    for (u16 i = 0; i < 20; ++i)
        checksum += rsdp_addr[i];

    if (checksum != 0)
        panic("Bad RSDP. Checksum not valid.");


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
