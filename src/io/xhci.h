#ifndef IO_XHCI_H
#define IO_XHCI_H

#include "../common.h"
#include <stdbool.h>
#include "../memory/memory.h"

#define XHCI_SIGNATURE 0x0C0330

typedef struct
{
	bool found;

	u8 bus;
	u8 device;
	u8 function;
	u64 mmio_addr;
	u8 max_slots;
	u16 max_interrupters;
	u8 max_ports;
	u32 scratchpad_count;
	Virt_addr dcbaa_virt_addr;
	Virt_addr scratchpad_virt_addr;
	u64 max_frame_number;
	volatile u32* op_regs;
}XHCI_Info;

u32 pci_read_config(u8 bus, u8 device, u8 function, u8 offset);
void pci_write_config_16_bit(u8 bus, u8 device, u8 function, u8 offset, u16 value);
void pci_write_config_32_bit(u8 bus, u8 device, u8 function, u8 offset, u32 value);

XHCI_Info setup_xhci(void);

#endif // IO_XHCI_H
