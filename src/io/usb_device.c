#include "usb_device.h"
#include "../memory/frame_allocator.h"
#include "../memory/arena_allocator.h"
#include "../memory/page_table.h"


static bool detect_usb_device_connected(XHCI_Info* xhci, u32 event[4], u8* port_id)
{
	const u32 PORT_STATUS_CHANGED = 34;

	u32 event_type = (event[3] >> 10) & 0x3F;
	if (event_type != PORT_STATUS_CHANGED)
		return false;

	u8 _port_id = (event[0] >> 24) & 0xFF;
	if (_port_id == 0 || _port_id > xhci->max_ports)
		return false;

	u32 portsc_index = 256 + (_port_id - 1) * 4;
	u32 portsc = xhci->op_regs[portsc_index];

	bool connected = (portsc & 1U) != 0;
	bool connection_changed = (portsc & (1 << 17U)) != 0;

	if (connection_changed && connected)
	{
		*port_id = _port_id;
		return true;
	}

	return false;
}

static bool reset_port(XHCI_Info* xhci_info, u8 port_id)
{
	u32 portsc_index = 256 + (port_id - 1) * 4;
	volatile u32* portsc = &xhci_info->op_regs[portsc_index];

	const u32 CCS	= 1 << 0;
	const u32 PED	= 1 << 1;
	const u32 PR	= 1 << 4;
	const u32 PRC	= 1 << 21;

	const u32 PORTSC_NEUTRAL_MASK =
		(1U << 0)  |  // CCS
		(1U << 3)  |  // OCA
		(0xFU << 5) | // PLS
		(1U << 9)  |  // PP
		(0xFU << 10) | // Port Speed
		(3U << 14) | // PIC
		(7U << 25) | // WCE, WDE, WOE
		(1U << 30);   // DR

	// clear PRC
	u32 portsc_value = *portsc & PORTSC_NEUTRAL_MASK;
	*portsc = portsc_value | PRC;

	// start reset
	portsc_value = *portsc & PORTSC_NEUTRAL_MASK;
	*portsc = portsc_value | PR;

	while (*portsc & PR) {}
	while ((*portsc & PRC) == 0) {}

	portsc_value = *portsc;
	bool success = (portsc_value & CCS) && (portsc_value & PED);

	portsc_value = *portsc & PORTSC_NEUTRAL_MASK;
	*portsc = portsc_value | (1U << 21);

	return success;
}

static void enable_slot(XHCI_Info* info, u8 slot_type)
{
	u32 enable_slot_command[4] = {0};
	enable_slot_command[3] = 9 << 10;
	enable_slot_command[3] |= (slot_type << 16);

	write_command_trb(info, enable_slot_command);
	doorbell_command_ring(info);
}

static bool detect_enable_completion(u32 event_trb[4], u8* completion_code, u8* slot_id)
{
	const u32 COMMAND_COMPLETION_EVENT = 33;

	u32 event_type = (event_trb[3] >> 10) & 0x3F;
	if (event_type != COMMAND_COMPLETION_EVENT)
		return false;

	*completion_code = (event_trb[2] >> 24) & 0xFF;
	*slot_id = (event_trb[3] >> 24) & 0xFF;

	return true;
}

static void setup_device_context(USB_Device* device)
{
	Virt_addr ctxt_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
	Phys_addr ctxt_phys_addr = allocate_general_frame();
	map_address(ctxt_virt_addr, ctxt_phys_addr, PTE_WRITABLE);

	memset((void*)ctxt_virt_addr, 0, FRAME_SIZE);

	device->device_context_virt_addr = ctxt_virt_addr;
	device->device_context_phys_addr = ctxt_phys_addr;

	// The XHCI controller should also know this address.
	volatile u64* controller_dcbaa = (volatile u64*)(device->xhci_info->dcbaa_virt_addr);
	controller_dcbaa[device->slot_id] = ctxt_phys_addr;
}

bool setup_usb_device(USB_Device* device, XHCI_Info* xhci)
{
	device->xhci_info = xhci;

	u32 event[4];

	// Acquire port ID.
	while (1)
	{
		while (!read_event_trb(xhci, event)) {}
		update_event_ring_dequeue_pointer(xhci);

		if (!detect_usb_device_connected(xhci, event, &device->port_id))
			continue;

		if (reset_port(xhci, device->port_id))
		{
			// vga_printf("Port %X reset success.\n", device->port_id);
			break;
		}
	}

	enable_slot(xhci, 0);

	u8 completion_code;
	while (1)
	{
		while (!read_event_trb(xhci, event)) {}
		update_event_ring_dequeue_pointer(xhci);

		if (detect_enable_completion(event, &completion_code, &device->slot_id))
		{
			if (!completion_code)
				return false;

			break;
		}
	}
	// Port reset complete.

	u32 portsc_index = 256 + (device->port_id - 1) * 4;
	u32 portsc = xhci->op_regs[portsc_index];

	device->port_speed = (portsc >> 10) & 0xFF;

	setup_device_context(device);

	return true;
}
