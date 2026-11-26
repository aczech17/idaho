#include "rsdt.h"

void setup_rsdt(RSDT* rsdt, u64 address)
{
    u8* addr = (u8*)address;

    memcpy(rsdt->signature, addr, 4);
    memcpy(&rsdt->length, addr + 4, 4);
    memcpy(&rsdt->revision, addr + 8, 1);
    memcpy(&rsdt->checksum, addr + 9, 1);

    // TODO
}
