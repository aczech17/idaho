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

static bool detect_command_completion(const u32 event_trb[4], u8* completion_code, u8* slot_id)
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

static void setup_ep0_transfer_ring(USB_Device* device)
{
	Virt_addr ep0_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
	Phys_addr ep0_phys_addr = allocate_general_frame();
	map_address(ep0_virt_addr, ep0_phys_addr, PTE_WRITABLE);

	memset((void*)ep0_virt_addr, 0, FRAME_SIZE);

	device->ep0_transfer_ring_virt_addr = ep0_virt_addr;
	device->ep0_transfer_ring_phys_addr = ep0_phys_addr;

	volatile u32* link_trb = (volatile u32*)(ep0_virt_addr + 255 * 16);
	link_trb[0] = (u32)(ep0_phys_addr);
	link_trb[1] = (u32)(ep0_phys_addr >> 32);
	link_trb[2] = 0;
	link_trb[3] = (6U << 10)	// TRB type = 6 (link TRB)
					| (1U << 1) // Toggle cycle
					| (1U << 0); // Initial cycle bit = 1

	device->ep0_transfer_ring_trb_index = 0;
	device->ep0_transfer_ring_cycle_state = true;
}

static void setup_input_context(USB_Device* device)
{
	Virt_addr ctxt_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
	Phys_addr ctxt_phys_addr = allocate_general_frame();
	map_address(ctxt_virt_addr, ctxt_phys_addr, PTE_WRITABLE);

	memset((void*)ctxt_virt_addr, 0, FRAME_SIZE);

	device->input_context_virt_addr = ctxt_virt_addr;
	device->input_context_phys_addr = ctxt_phys_addr;

	/*
		Input context:
		Offset 0x00 – Input Control Context
		Offset 0x20 – Input Slot Context       (32 B)
		Offset 0x40 – Input Endpoint 0 Context (32 B)

		or times 2 if context size == 64
	*/
	u32* input_slot_context = (u32*)(device->input_context_virt_addr) +
								(device->xhci_info->context_size_64 ? 16 : 8);
	input_slot_context[0] = (1U << 27) // Context entries == 1
							| ((u32)device->port_speed << 20);

	input_slot_context[1] = (u32)(device->port_id) << 16;

	u32* input_endpoint0_context = (u32*)(device->input_context_virt_addr) +
									(device->xhci_info->context_size_64 ? 32 : 16);
	switch (device->port_speed)
	{
		case 1:
		case 2:
			device->ep0_max_packet_size = 8;
			break;
		case 3:
			device->ep0_max_packet_size = 64;
			break;
		case 4:
		case 5:
			device->ep0_max_packet_size = 512;
			break;
		default:
			vga_printf("USB speed = %X\n", device->port_speed);
			panic("Bad USB speed.");
	}

	// input_endpoint0_context[0] remains zero
	input_endpoint0_context[1] =
			 (3U << 1) // CERR = 3
		   | (4U << 3) // EP type = 4
		   | ((u32)device->ep0_max_packet_size << 16);

	setup_ep0_transfer_ring(device);
	input_endpoint0_context[2] = (u32)device->ep0_transfer_ring_phys_addr |
		(device->ep0_transfer_ring_cycle_state ? 1U : 0U);
	input_endpoint0_context[3] = (u32)(device->ep0_transfer_ring_phys_addr >> 32);

	volatile u32* input_control_context = (volatile u32*)device->input_context_virt_addr;
	input_control_context[0] = 0; // Drop context flags
	input_control_context[1] =	(1U << 1)		|	(1U << 0);
								// EP0 context		slot context
}

static void address_device(USB_Device* device)
{
	u32 request_trb[4] = {0};

	request_trb[0] = (u32)(device->input_context_phys_addr);
	request_trb[1] = (u32)(device->input_context_phys_addr >> 32);
	// request_trb[2] remains 0
	request_trb[3] = (11U << 10) // TRB type = 11 (address device command)
		| ((u32)(device->slot_id) << 24);

	// Now let's send it.
	write_command_trb(device->xhci_info, request_trb);
	doorbell_command_ring(device->xhci_info);

	u8 completion_code;
	u8 slot_id_received;
	u32 event_trb[4];

	while (1)
	{
		while (!read_event_trb(device->xhci_info, event_trb)) {} // Wait till something comes.
		update_event_ring_dequeue_pointer(device->xhci_info);	// Something has come.

		// Check if completion respond has arrived.
		if (!detect_command_completion(event_trb, &completion_code, &slot_id_received))
			continue;

		if (slot_id_received != device->slot_id) // It's not for us.
			continue;

		if (completion_code != 1)
			panic("Address device failed.");

		break;
	}

	vga_printf("Address device completed.\n");
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

		if (detect_command_completion(event, &completion_code, &device->slot_id))
		{
			if (!completion_code)
				return false;

			break;
		}
	}
	// Port reset complete.

	u32 portsc_index = 256 + (device->port_id - 1) * 4;
	u32 portsc = xhci->op_regs[portsc_index];

	device->port_speed = (portsc >> 10) & 0xF;

	setup_device_context(device);
	setup_input_context(device);
	address_device(device);


	return true;
}
