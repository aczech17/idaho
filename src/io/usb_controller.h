#ifndef IO_USB_CONTROLLER_H
#define IO_USB_CONTROLLER_H

#include "../common.h"
#include <stdbool.h>
#include "../memory/memory.h"

#define XHCI_SIGNATURE 0x0C0330

typedef enum
{
	USB_PORT_DISCONNECTED,
	USB_PORT_CONNECTED,
	USB_PORT_INITIALIZING,
	USB_PORT_INITIALIZED,
}USB_Port_state;

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
	Phys_addr dcbaa_phys_addr;
	bool context_size_64;

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

	USB_Port_state* usb_port_states;
}USB_Controller;

u32 pci_read_config(u8 bus, u8 device, u8 function, u8 offset);
void pci_write_config_16_bit(u8 bus, u8 device, u8 function, u8 offset, u16 value);
void pci_write_config_32_bit(u8 bus, u8 device, u8 function, u8 offset, u32 value);

USB_Controller setup_usb_controller(void);

bool read_event_trb(USB_Controller*, u32 event_trb[4]);
void update_event_ring_dequeue_pointer(USB_Controller* controller);

bool write_command_trb(USB_Controller*, u32 command_trb[4]);
void doorbell_ring(USB_Controller* controller, u32 slot_id, u32 value);
void doorbell_command_ring(USB_Controller*);

#endif // IO_USB_CONTROLLER_H
