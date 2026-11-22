#include "usb_device.h"
#include "../memory/frame_allocator.h"
#include "../memory/arena_allocator.h"
#include "../memory/page_table.h"



// static bool detect_usb_device_connected(USB_Controller* xhci, u32 event[4], u8* port_id)
// {
// 	const u32 PORT_STATUS_CHANGED = 34;
//
// 	u32 event_type = (event[3] >> 10) & 0x3F;
// 	if (event_type != PORT_STATUS_CHANGED)
// 		return false;
//
// 	u8 _port_id = (event[0] >> 24) & 0xFF;
// 	if (_port_id == 0 || _port_id > xhci->max_ports)
// 		return false;
//
// 	u32 portsc_index = 256 + (_port_id - 1) * 4;
// 	u32 portsc = xhci->op_regs[portsc_index];
//
// 	bool connected = (portsc & 1U) != 0;
// 	bool connection_changed = (portsc & (1 << 17U)) != 0;
//
// 	if (connection_changed && connected)
// 	{
// 		*port_id = _port_id;
// 		return true;
// 	}
//
// 	return false;
// }

static bool reset_port(USB_Controller* usb_controller, u8 port_id)
{
	u32 portsc_index = 256 + (port_id - 1) * 4;
	volatile u32* portsc = &usb_controller->op_regs[portsc_index];

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

// static bool acquire_port(USB_Device* device)
// {
// 	u32 event[4];
// 	while (1)
// 	{
// 		while (!read_event_trb(device->usb_controller, event)) {}
// 		update_event_ring_dequeue_pointer(device->usb_controller);
//
// 		if (!detect_usb_device_connected(device->usb_controller, event, &device->port_id))
// 			continue;
//
// 		bool reset_status = reset_port(device->usb_controller, device->port_id);
// 		return reset_status;
// 	}
// }

static bool acquire_slot(USB_Device* device)
{
	// Request a slot from the USB controller.
	u8 slot_type = 0;

	u32 enable_slot_command[4] = {0};
	enable_slot_command[3] = 9 << 10;
	enable_slot_command[3] |= (slot_type << 16);

	write_command_trb(device->usb_controller, enable_slot_command);
	doorbell_command_ring(device->usb_controller);


	// Now wait for the slot.
	u32 event[4];
	u8 completion_code;
	while (1)
	{
		while (!read_event_trb(device->usb_controller, event)) {}
		update_event_ring_dequeue_pointer(device->usb_controller);

		if (!detect_command_completion(event, &completion_code, &device->slot_id))
			continue;

		return completion_code == 1;
	}
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
	volatile u64* controller_dcbaa = (volatile u64*)(device->usb_controller->dcbaa_virt_addr);
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
								(device->usb_controller->context_size_64 ? 16 : 8);
	input_slot_context[0] = (1U << 27) // Context entries == 1
							| ((u32)device->port_speed << 20);

	input_slot_context[1] = (u32)(device->port_id) << 16;

	u32* input_endpoint0_context = (u32*)(device->input_context_virt_addr) +
									(device->usb_controller->context_size_64 ? 32 : 16);
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
	write_command_trb(device->usb_controller, request_trb);
	doorbell_command_ring(device->usb_controller);

	u8 completion_code;
	u8 slot_id_received;
	u32 event_trb[4];

	while (1)
	{
		while (!read_event_trb(device->usb_controller, event_trb)) {} // Wait till something comes.
		update_event_ring_dequeue_pointer(device->usb_controller);	// Something has come.

		// Check if completion respond has arrived.
		if (!detect_command_completion(event_trb, &completion_code, &slot_id_received))
			continue;

		if (slot_id_received != device->slot_id) // It's not for us.
			continue;

		if (completion_code != 1)
			panic("Address device failed.");

		break;
	}
}

static bool allocate_mem_for_descriptor(USB_Device* device)
{
	const u64 max_config_descriptor_size = 65535;
	const u64 frames_to_allocate = (max_config_descriptor_size + FRAME_SIZE - 1) / FRAME_SIZE;

	Phys_addr phys_addr = allocate_contiguous_general_frames(frames_to_allocate, MAX_FRAME_NUM);
	Virt_addr virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, frames_to_allocate);
	if (phys_addr == INVALID_ADDR || virt_addr == INVALID_ADDR)
		return false;

	for (u64 frame = 0; frame < frames_to_allocate; ++frame)
	{
		Virt_addr virt = virt_addr + frame * FRAME_SIZE;
		Phys_addr phys = phys_addr + frame * FRAME_SIZE;

		if (!map_address(virt, phys, PTE_WRITABLE))
			return false;
	}


	device->received_descriptor_virt_addr = virt_addr;
	device->received_descriptor_phys_addr = phys_addr;

	return true;
}

static void write_ep0_trb(USB_Device* device, const u32 trb[4])
{
	volatile u32* ring = (volatile u32*)(device->ep0_transfer_ring_virt_addr);
	u16 index = device->ep0_transfer_ring_trb_index;
	bool cycle_state = device->ep0_transfer_ring_cycle_state;

	volatile u32* entry = &ring[index * 4];
	for (u32 i = 0; i < 3; ++i)
		entry[i] = trb[i];

	entry[3] = cycle_state ? (trb[3] | 1) : (trb[3] & ~1);

	++index;

	if (index == 255)
	{
		volatile u32* link_trb = &ring[255 * 4];
		link_trb[3] = cycle_state ? (link_trb[3] | 1) : (link_trb[3] & ~1);

		device->ep0_transfer_ring_trb_index = 0;
		device->ep0_transfer_ring_cycle_state = !cycle_state;
	}
	else
	{
		device->ep0_transfer_ring_trb_index = index;
	}
}

static void get_descriptor(USB_Device* device, u8 descriptor_type, u32 descriptor_size)
{
	u32 setup_trb[4] = {0};

	setup_trb[0] = 0x80U		// (device to host)
		| (6U << 8)				// bRequest = 6 (get descriptor)
		| ((u32)descriptor_type << 24);

	setup_trb[1] = descriptor_size << 16;
	setup_trb[2] = 8;	// TRB transfer length = 8 (setup packet)

	setup_trb[3] = (3U << 16) // TRT = 3 (IN)
		| (2U << 10)			// TRB type = 2 (setup stage)
		| (1U << 6);		// IDT = 1 (immediate data)


	u32 data_trb[4] = {0};

	data_trb[0] = (u32)(device->received_descriptor_phys_addr);
	data_trb[1] = (u32)(device->received_descriptor_phys_addr >> 32);

	data_trb[2] = descriptor_size;

	data_trb[3] = (3U << 10)	// TRB type = 3 (data stage)
				| (1U << 16);	// DIR = 1 (IN)

	u32 status_trb[4] = {0};
	status_trb[3] = (4U << 10)	// TRB type = 4 (status stage)
	              | (1U << 5);	// IOC = 1 (interrupt on completion)
								// DIR = 0 (OUT)

	write_ep0_trb(device, setup_trb);
	write_ep0_trb(device, data_trb);
	write_ep0_trb(device, status_trb);

	doorbell_ring(device->usb_controller, device->slot_id, 1);

	u32 received_trb[4];
	while (1)
	{
		while (!read_event_trb(device->usb_controller, received_trb));
		update_event_ring_dequeue_pointer(device->usb_controller);

		u8 trb_type = (received_trb[3] >> 10) & 0x3F;
		u8 endpoint_id = (received_trb[3] >> 16) & 0x1F;
		u8 slot_id = (received_trb[3] >> 24) & 0xFF;


		bool it_is_for_us = (trb_type == 32) && (endpoint_id == 1) && (slot_id == device->slot_id);
		if (!it_is_for_us)
			continue;

		u8 completion_code = (received_trb[2] >> 24) & 0xFF;
		if (completion_code != 1)
			panic("USB device get descriptor failed.");

		break;
	}
}

bool setup_usb_device(USB_Device* device, USB_Controller* usb_controller, u8 port_id)
{
	device->usb_controller = usb_controller;
	device->port_id = port_id;

	if (!reset_port(usb_controller, port_id))
		return false;

	if (!acquire_slot(device))
		return false;

	u32 portsc_index = 256 + (device->port_id - 1) * 4;
	u32 portsc = usb_controller->op_regs[portsc_index];
	device->port_speed = (portsc >> 10) & 0xF;

	setup_device_context(device);
	setup_input_context(device);
	address_device(device);

	if (!allocate_mem_for_descriptor(device))
		return false;

	get_descriptor(device, 1, 18); // get device descriptor



	return true;
}
