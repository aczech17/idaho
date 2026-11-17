#ifndef IO_XHCI_H
#define IO_XHCI_H

#include "../common.h"
#include <stdbool.h>
#define XHCI_SIGNATURE 0x0C0330

typedef struct
{
	u8 bus;
	u8 device;
	u8 function;
	bool found;
}XHCI_Info;

u32 pci_read_config(u8 bus, u8 device, u8 function, u8 offset);
void pci_write_config(u8 bus, u8 device, u8 function, u8 offset, u16 value);

XHCI_Info find_xhci_info(void);
u64 get_mmio_xhci_address(const XHCI_Info*);
void enable_xhci_memory_space(const XHCI_Info*);

#endif // IO_XHCI_H
