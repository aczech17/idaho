#ifndef MEMORY_COMMON_H
#define MEMORY_COMMON_H

#include "../common.h"

#define FRAME_SIZE (1 << 12)        // 4 KiB

typedef u64 Phys_addr;
typedef u64 Virt_addr;
typedef Phys_addr Frame_address;

#define INVALID_ADDR ((u64)(0))

void set_cr3(u64 pt_tree_addr);
void load_idtr(u64 idtr_addr);
void invalidate_tlb_entry(u64 virtual_address);

#endif // MEMORY_COMMON_H
