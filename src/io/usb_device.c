#include "usb_device.h"
#include "../memory/frame_allocator.h"
#include "../memory/arena_allocator.h"
#include "../memory/page_table.h"

#define DEVICE_DESCRIPTOR 1
#define CONFIG_DESCRIPTOR 2
#define STRING_DESCRIPTOR 3
#define INTERFACE_DESCRIPTOR 4
#define ENDPOINT_DESCRIPTOR 5

#define TRANSFER_EVENT ((u32)(32))
#define COMMAND_COMPLETION_EVENT ((u32)(33))

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

static void decode_event_trb(const u32 event_trb[4], u8* completion_code, u8* slot_id, u8* event_type, u8* endpoint_id)
{
	*event_type = (event_trb[3] >> 10) & 0x3F;
	*completion_code = (event_trb[2] >> 24) & 0xFF;
	*slot_id = (event_trb[3] >> 24) & 0xFF;

	if (endpoint_id)
		*endpoint_id = (event_trb[3] >> 16) & 0x1F;
}

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
	u8 event_type;

	while (1)
	{
		while (!read_event_trb(device->usb_controller, event)) {}

		update_event_ring_dequeue_pointer(device->usb_controller);
		decode_event_trb(event, &completion_code, &device->slot_id, &event_type, NULL);

		if (event_type != COMMAND_COMPLETION_EVENT)
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
	u8 event_type;
	u32 event_trb[4];

	while (1)
	{
		while (!read_event_trb(device->usb_controller, event_trb)) {} // Wait till something comes.

		update_event_ring_dequeue_pointer(device->usb_controller);	// Something has come.
		decode_event_trb(event_trb, &completion_code, &slot_id_received, &event_type, NULL);

		if (event_type != COMMAND_COMPLETION_EVENT || slot_id_received != device->slot_id) // It's not for us.
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

static bool parse_device_descriptor(USB_Device* device)
{
	const u8* descriptor = (u8*)(device->received_descriptor_virt_addr);

	if (descriptor[0] != 18 || descriptor[1] != 1)
		return false;

	device->device_class = descriptor[4];
	device->device_subclass = descriptor[5];
	device->device_protocol = descriptor[6];

	device->ep0_max_packet_size = descriptor[7] == 9
		? 512
		: descriptor[7];

	device->vendor_id = (u16)descriptor[8]
					  | ((u16)descriptor[9] << 8);

	device->product_id = (u16)descriptor[10]
					   | ((u16)descriptor[11] << 8);

	device->num_configurations = descriptor[17];

	return true;
}

static bool parse_config_descriptor(USB_Device* device)
{
	const u8* descriptor = (const u8*)device->received_descriptor_virt_addr;

	if (descriptor[0] != 9 || descriptor[1] != 2)
		return false;

	u16 total_length = (u16)descriptor[2]
					 | ((u16)descriptor[3] << 8);

	if (total_length < 9)
		return false;

	device->config_total_length = total_length;
	device->interfaces_count = descriptor[4];
	device->config_value = descriptor[5];

	return true;
}

static bool allocate_interfaces(USB_Device* device)
{
	u64 mem_needed = device->interfaces_count * sizeof(USB_Interface);
	u64 frames_to_allocate = (mem_needed + FRAME_SIZE - 1) / FRAME_SIZE;

	Virt_addr virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, frames_to_allocate);
	Phys_addr phys_addr = allocate_contiguous_general_frames(frames_to_allocate, MAX_FRAME_NUM);

	if (virt_addr == INVALID_ADDR || phys_addr == INVALID_ADDR)
		return false;

	map_address(virt_addr, phys_addr, PTE_WRITABLE);

	memset((void*)virt_addr, 0, frames_to_allocate * FRAME_SIZE); // Just to be sure.

	device->interfaces = (USB_Interface*)virt_addr;

	return true;
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

	get_descriptor(device, DEVICE_DESCRIPTOR, 18); // get device descriptor
	if (!parse_device_descriptor(device))
		panic("Invalid USB device descriptor.");

	get_descriptor(device, CONFIG_DESCRIPTOR, 9);// get configuration descriptor
	if (!parse_config_descriptor(device))
		panic("Invalid USB config descriptor.");

	get_descriptor(device, CONFIG_DESCRIPTOR, device->config_total_length);
	device->config_descriptor = device->received_descriptor_virt_addr;

	if (!allocate_interfaces(device))
		return false;

	// USB_Controller → USB_Device → USB_Interface → USB_Interface_Endpoint
	// Parse all this shit.


	volatile u8* descriptor = (volatile u8*)(device->config_descriptor + 9);
	for (u8 intf_num = 0; intf_num < device->interfaces_count; )
	{
		// Parse 9 bytes.
		USB_Interface* interface = &device->interfaces[intf_num];

		u8 descriptor_size = descriptor[0];
		u8 descriptor_type = descriptor[1];


		if (descriptor_size != 9 || descriptor_type != INTERFACE_DESCRIPTOR)
		{
			descriptor += descriptor_size;
			continue;
		}

		u8 alternate_setting = descriptor[3];
		if (alternate_setting != 0)
		{
			descriptor += descriptor_size;
			continue;
		}

		interface->number = descriptor[2];
		interface->alternate_setting = alternate_setting;
		interface->endpoint_count = descriptor[4];
		interface->class = descriptor[5];
		interface->subclass = descriptor[6];
		interface->protocol = descriptor[7];
		interface->name_index = descriptor[8];

		descriptor += 9;

		if (!allocate_interface_endpoints(interface))
			panic("Could not allocate USB interface.");

		for (u8 ep_num = 0; ep_num < interface->endpoint_count; )
		{
			USB_Interface_Endpoint* endpoint = &interface->endpoints[ep_num];

			descriptor_size = descriptor[0];
			descriptor_type = descriptor[1];

			if (descriptor_size != 7 || descriptor_type != ENDPOINT_DESCRIPTOR)
			{
				descriptor += descriptor_size;
				continue;
			}

			endpoint->endpoint_address = descriptor[2];
			endpoint->attributes = descriptor[3];
			endpoint->max_packet_size = (u16)descriptor[4] | ((u16)descriptor[5] << 8);
			endpoint->interval = descriptor[6];

			descriptor += descriptor_size;
			++ep_num;
		}

		++intf_num;
	}


	return true;
}

static bool send_to_ep0_and_acknowledge(USB_Device* device, u32** trbs, u8 trbs_count, u8 event_type)
{
	const u8 EP0_ID = 1;

	for (u8 i = 0; i < trbs_count; ++i)
		write_ep0_trb(device, trbs[i]);

	doorbell_ring(device->usb_controller, device->slot_id, EP0_ID);

	// Wait for completion.
	u32 event_trb[4];
	u8 received_slot_id, completion_code, received_event_type, received_endpoint_id;
	while (1)
	{
		while (!read_event_trb(device->usb_controller, event_trb)) {}
		update_event_ring_dequeue_pointer(device->usb_controller);

		decode_event_trb(event_trb, &completion_code, &received_slot_id, &received_event_type, &received_endpoint_id);

		if (received_event_type != event_type || received_slot_id != device->slot_id || received_endpoint_id != EP0_ID)
			continue;

		if (completion_code != 1)
			return false;

		return true;
	}
}

// bool set_input_context(USB_Device* device, USB_Interface_Endpoint* ep, Input_context* ctxt)
// {
// 	u8 ep_number = ep->endpoint_address & 0x0F;
// 	u8 direction = (ep->endpoint_address & 0x80) != 0;
// 	u8 dci = 2 * ep_number + direction;
//
//
// 	ctxt->input_control_context[0] = 0; // drop context flags
// 	ctxt->input_control_context[1] = 1 | (1 << dci); // add context flags
//
// 	// copy slot context
// 	u8* device_slot_ctxt_address = (u8*)device->device_context_virt_addr;
// 	u8* dest = (u8*)ctxt->slot_context;
// 	memcpy(dest, device_slot_ctxt_address, 32);
//
// 	u8 current_context_entries = (ctxt->slot_context[0] >> 27) & 0x1F;
// 	if (dci > current_context_entries)
// 	{
// 		ctxt->slot_context[0] &= ~(0x1F << 27);
// 		ctxt->slot_context[0] |= (u32)dci << 27;
// 	}
//
//
// 	u32* endpoint_context = ctxt->endpoint_contexts[dci - 1];
//
// 	endpoint_context[0] =
// 		(3 << 1) |                         // CErr = 3
// 		(7 << 3) |                         // Endpoint Type = Interrupt IN
// 		((u32)ep->interval << 16);          // Interval
//
// 	endpoint_context[1] =
// 		((u32)ep->max_packet_size << 16);  // Max Packet Size
//
// 	u64 dequeue_pointer = ep->transfer_ring_phys_addr
// 		| (ep->transfer_ring_cycle_state ? 1ULL : 0ULL);
//
// 	endpoint_context[2] = (u32)dequeue_pointer;
// 	endpoint_context[3] = (u32)(dequeue_pointer >> 32);
//
// 	endpoint_context[4] = 0; // Average TRB Length and Max ESIT Payload
// 	endpoint_context[5] = 0;
// 	endpoint_context[6] = 0;
// 	endpoint_context[7] = 0;
//
//
//
// }

bool set_input_context(USB_Device* device, USB_Interface_Endpoint* ep, Input_context* ctxt)
{
    u8 endpoint_number = ep->endpoint_address & 0x0F;
    u8 dci = 2 * endpoint_number + 1;

    // Decode wMaxPacketSize.
    u16 w_max_packet_size = ep->max_packet_size;
    u16 max_packet_size = w_max_packet_size & 0x07FF;
    u8 max_burst_size = 0;


    // Calculate the endpoint interval and maximum ESIT payload.
    u8 interval;
    u32 max_esit_payload;

    switch (device->port_speed)
    {
        case 1: // Full-Speed
        case 2: // Low-Speed
        {
            u32 interval_microframes = (u32)ep->interval * 8;
            interval = 0;

            while (interval_microframes > 1)
            {
                interval_microframes >>= 1;
                interval++;
            }

            if (interval < 3)
            {
                interval = 3;
            }
            else if (interval > 10)
            {
                interval = 10;
            }

            max_esit_payload = max_packet_size;
            break;
        }

        case 3: // High-Speed
        {
            max_burst_size = (w_max_packet_size >> 11) & 0x03;

            if (max_burst_size > 2)
            {
                return false;
            }

            interval = ep->interval - 1;

            max_esit_payload =
                (u32)max_packet_size * (max_burst_size + 1);

            break;
        }

        default:
        {
            // SuperSpeed requires Endpoint Companion Descriptor data.
            return false;
        }
    }

    // Copy the current Slot Context from the Device Context.
    memcpy(ctxt->slot_context,(void*)device->device_context_virt_addr,32);

    // Update Context Entries in Slot Context DWORD 0.
    u8 current_context_entries = (ctxt->slot_context[0] >> 27) & 0x1F;

    if (dci > current_context_entries)
    {
        ctxt->slot_context[0] &= ~(0x1Fu << 27);
        ctxt->slot_context[0] |= (u32)dci << 27;
    }

    // Input Control Context.
    memset(ctxt->input_control_context, 0, 32);

    ctxt->input_control_context[0] = 0; // Drop Context Flags
    ctxt->input_control_context[1] =
        (1u << 0) | (1u << dci); // Add Slot Context and endpoint



    u32* endpoint_context = ctxt->endpoint_contexts[dci - 1];

    memset(endpoint_context, 0, 32);

    // DWORD 0:
    // Interval: bits 23:16
    // Max ESIT Payload Hi: bits 31:24
    endpoint_context[0] =
        ((u32)interval << 16) |
        (((max_esit_payload >> 16) & 0xFF) << 24);

    // DWORD 1:
    // CErr: bits 2:1
    // Endpoint Type: bits 5:3 (7 = Interrupt IN)
    // Max Burst Size: bits 15:8
    // Max Packet Size: bits 31:16
    endpoint_context[1] =
        (3u << 1) |
        (7u << 3) |
        ((u32)max_burst_size << 8) |
        ((u32)max_packet_size << 16);

    // DWORD 2 and DWORD 3:
    // Transfer Ring Dequeue Pointer and DCS.
    u64 dequeue_pointer =
        (u64)ep->transfer_ring_phys_addr |
        (ep->transfer_ring_cycle_state ? 1ULL : 0ULL);

    endpoint_context[2] = (u32)dequeue_pointer;
    endpoint_context[3] = (u32)(dequeue_pointer >> 32);

    // DWORD 4:
    // Average TRB Length: bits 15:0
    // Max ESIT Payload Low: bits 31:16
    endpoint_context[4] =
        (max_esit_payload & 0xFFFF) |
        ((max_esit_payload & 0xFFFF) << 16);

    // DWORD 5-7 remain zero.


	u32 trb[4];
	trb[0] = (u32)device->input_context_phys_addr;
	trb[1] = (u32)((u64)device->input_context_phys_addr >> 32);
	trb[2] = 0;

	trb[3] =
		(12u << 10) |          // TRB Type: Configure Endpoint Command
		((u32)device->slot_id << 24);  // Slot ID

	write_command_trb(device->usb_controller, trb);
	doorbell_command_ring(device->usb_controller);


	u32 event_trb[4];
	u8 completion_code, received_slot_id, received_event_type;
	while (1)
	{
		while (!read_event_trb(device->usb_controller, event_trb)) {}
		update_event_ring_dequeue_pointer(device->usb_controller);


		decode_event_trb(event_trb, &completion_code, &received_slot_id, &received_event_type, NULL);
		if (received_event_type != COMMAND_COMPLETION_EVENT || received_slot_id != device->slot_id)
			continue;

		if (completion_code != 1)
			return false;

		break;
	}

	return true;
}

bool set_configuration(USB_Device* device)
{
	const u8 SET_CONFIG = 0x09;

	u32 setup_trb[4] = {0};

	setup_trb[0] = ((u32)(SET_CONFIG << 8)) | ((u32)device->config_value << 16);
	setup_trb[2] = 8;			// Transfer length = 8
	setup_trb[3] = (1U << 6)	// IDT = 1
	             | (1U << 4)	// chain = 1
				 | (2U << 10);	// TRB type = 2 (setup stage)



	u32 status_trb[4] = {0};
	status_trb[3] = (1U << 16)	// direction = IN
		| (1U << 5)				// IOC = 1
		| (4U << 10);			// TRB type = 4 (status stage)


	u32* trbs[] = {setup_trb, status_trb};

	return send_to_ep0_and_acknowledge(device, trbs, 2, TRANSFER_EVENT);
}

bool set_protocol(USB_Device* device, u8 interface_number)
{
	const u8 SET_PROTOCOL = 0x0B;

	u32 setup_trb[4] = {0};

	setup_trb[0] = 0x21U
				 | ((u32)SET_PROTOCOL << 8);
	setup_trb[1] = interface_number;	// wIndex = interface number
	setup_trb[2] = 8;					// Transfer length = 8
	setup_trb[3] = (1U << 6)			// IDT = 1
				 | (1U << 4)			// Chain = 1
				 | (2U << 10);			// TRB type = 2 (Setup Stage)

	u32 status_trb[4] = {0};

	status_trb[3] = (1U << 16)			// Direction = IN
				  | (1U << 5)			// IOC = 1
				  | (4U << 10);			// TRB type = 4 (Status Stage)

	u32* trbs[] = {setup_trb, status_trb};

	return send_to_ep0_and_acknowledge(device, trbs, 2, TRANSFER_EVENT);
}

