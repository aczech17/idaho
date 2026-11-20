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
	bool supports_64bit_dma;
	u64 max_frame_number;
	volatile u32* op_regs;
	volatile u32* runtime_regs;

	Virt_addr additional_page_virt_addr;
	Phys_addr additional_page_phys_addr;
	u64 additional_page_offset;

	Virt_addr event_ring_virt_addr;
	Phys_addr event_ring_phys_addr;
	u64 event_ring_trb_index;
	bool event_ring_cycle_state;

	Virt_addr command_ring_virt_addr;
	Phys_addr command_ring_phys_addr;
	u64 command_ring_trb_index;
	bool command_ring_cycle_state;
}XHCI_Info;

u32 pci_read_config(u8 bus, u8 device, u8 function, u8 offset);
void pci_write_config_16_bit(u8 bus, u8 device, u8 function, u8 offset, u16 value);
void pci_write_config_32_bit(u8 bus, u8 device, u8 function, u8 offset, u32 value);

XHCI_Info setup_xhci(void);

bool read_event_trb(XHCI_Info*, u32 event_trb[4]);
void update_event_ring_dequeue_pointer(XHCI_Info* info);

bool write_command_trb(XHCI_Info*, u32 command_trb[4]);
void doorbell_command_ring(XHCI_Info*);

#endif // IO_XHCI_H
