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
	u64 mmio_addr;
}XHCI_Info;

u32 pci_read_config(u8 bus, u8 device, u8 function, u8 offset);
void pci_write_config(u8 bus, u8 device, u8 function, u8 offset, u16 value);

XHCI_Info setup_xhci(void);

#endif // IO_XHCI_H
