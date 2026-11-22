#include "usb_controller.h"
#include "../memory/page_table.h"
#include "../memory/arena_allocator.h"

static USB_Controller find_usb_controller(void)
{
	USB_Controller usb_controller = {0};

	for (u16 bus = 0; bus < 256; ++bus)
		for (u8 device = 0; device < 32; ++device)
			for (u8 function = 0; function < 8; ++function)
			{
				u32 ident = pci_read_config(bus, device, function, 0);

				u16 vendor_id = ident & 0xFFFF;
				if (vendor_id == 0xFFFF)
					continue; // nonexistent

				u32 class_info = pci_read_config(bus, device, function, 0x08);
				u32 class_code = class_info >> 8;

				if (class_code == XHCI_SIGNATURE)
				{
					usb_controller.bus = bus;
					usb_controller.device = device;
					usb_controller.function = function;
					usb_controller.found = true;

					return usb_controller;
				}
			}

	return usb_controller;
}

static u64 get_mmio_xhci_size(const USB_Controller* controller)
{
	u32 bar0 = pci_read_config(controller->bus, controller->device, controller->function, 0x10);
	u32 bar1 = pci_read_config(controller->bus, controller->device, controller->function, 0x14);

	u32 command_status = pci_read_config(controller->bus, controller->device, controller->function, 0x04);
	u16 command = command_status & 0xFFFF;

	// Reset memory space enable for bar measure.
	pci_write_config_16_bit(controller->bus, controller->device, controller->function, 0x04, command & ~(1 << 1));

	// Write ones to BAR
	pci_write_config_32_bit(controller->bus, controller->device, controller->function, 0x10, 0xFFFFFFFF);
	pci_write_config_32_bit(controller->bus, controller->device, controller->function, 0x14, 0xFFFFFFFF);

	u32 bar0_mask = pci_read_config(controller->bus, controller->device, controller->function, 0x10);
	u32 bar1_mask = pci_read_config(controller->bus, controller->device, controller->function, 0x14);

	// Restore original BAR
	pci_write_config_32_bit(controller->bus, controller->device, controller->function, 0x10, bar0);
	pci_write_config_32_bit(controller->bus, controller->device, controller->function, 0x14, bar1);

	// Restore command register
	pci_write_config_16_bit(controller->bus, controller->device, controller->function, 0x04, command);

	u64 address_mask = (((u64)bar1_mask) << 32) | (bar0_mask & 0xFFFFFFF0U);
	u64 size = ~address_mask + 1;

	return size;
}

static void enable_xhci_memory_space(const USB_Controller* controller)
{
	u32 command_status = pci_read_config(controller->bus, controller->device, controller->function, 0x04);

	u16 command = command_status & 0xFFFF;
	command |= (1 << 1); // Set memory space enable bit
	command |= (1 << 2);  // Set bus master enable bit

	pci_write_config_16_bit(controller->bus, controller->device, controller->function, 0x04, command);
}

static u64 get_mmio_xhci_address(const USB_Controller* controller)
{
	u32 bar0 = pci_read_config(controller->bus, controller->device, controller->function, 0x10);
	u32 bar1 = pci_read_config(controller->bus, controller->device, controller->function, 0x14);

	u32 bar0_addr = bar0 & 0xFFFFFFF0;

	u64 mmio_addr = ((u64)bar1 << 32) | (u64)bar0_addr;
	return mmio_addr;
}

static void setup_ports_states(USB_Controller* controller)
{
	u64 port_states_size = controller->max_ports * sizeof(USB_Port_state);

	// Port states address must be aligned to sizeof port states.
	u64 alignment = _Alignof(USB_Port_state);
	u64 padding = (alignment - controller->additional_page_offset % alignment) % alignment;
	controller->additional_page_offset += padding;

	controller->usb_port_states =
		(USB_Port_state*)(controller->additional_page_virt_addr + controller->additional_page_offset);

	controller->additional_page_offset += port_states_size;

	for (u8 port = 0; port < controller->max_ports; ++port)
		controller->usb_port_states[port] = USB_PORT_DISCONNECTED;
}

static void take_ownership(const USB_Controller* controller)
{
	volatile u32* regs = (u32*)(controller->mmio_addr);

	u32 hccparams1 = regs[4];
	u32 xecp = ((hccparams1 >> 16) & 0xFFFF);

	u32 xecp_offset = xecp * 4;

	if (xecp == 0)
		return;

	for (;;)
	{
		volatile u32* header_pointer = (u32*)(controller->mmio_addr + xecp_offset);
		u32 header = *header_pointer;

		u8 capability_id = header & 0xFF;
		if (capability_id == 1) // USB legacy support found
		{
			((volatile u8*)header_pointer)[3] |= 1U;

			// Wait until the BIOS releases the controller.
			while (((*header_pointer) & (1U << 16)) != 0) {}
			break;
		}

		u8 next_offset_pointer = (header >> 8) & 0xFF;
		if (next_offset_pointer == 0)
			break;

		xecp_offset += next_offset_pointer * 4;
	}
}

static void stop(const USB_Controller* controller)
{
	volatile u32* regs = (u32*)(controller->mmio_addr);
	u32 operational_offset = regs[0] & 0xFF;

	volatile u32* operational_regs = (volatile u32*)(controller->mmio_addr + operational_offset);
	volatile u32* usbcmd = &operational_regs[0];
	volatile u32* usbsts = &operational_regs[1];

	*usbcmd &= ~1U;

	while (((*usbsts) & 1U) == 0) {}
}

static void restart(const USB_Controller* controller)
{
	volatile u32* regs = (u32*)(controller->mmio_addr);
	u32 operational_offset = regs[0] & 0xFF;

	volatile u32* operational_regs = (volatile u32*)(controller->mmio_addr + operational_offset);
	volatile u32* usbcmd = &operational_regs[0];
	volatile u32* usbsts = &operational_regs[1];

	*usbcmd |= (1U << 1);

	while (((*usbcmd) & (1U << 1)) != 0) {} // host controller reset (HCRST)
	while (((*usbsts) & (1U << 11)) != 0) {} // controller not ready (CNR)
}

static void allocate_additional_page(USB_Controller* controller)
{
	Phys_addr additional_frame =
		allocate_contiguous_general_frames(1, controller->max_frame_number);

	if (additional_frame == INVALID_ADDR)
		panic("Could not allocate additional memory for XHCI.");

	Virt_addr additional_page = arena_allocate_pages(&xhci_driver_page_allocator, 1);
	map_address(additional_page, additional_frame, PTE_WRITABLE);
	memset((void*)additional_page, 0, FRAME_SIZE);

	controller->additional_page_virt_addr = additional_page;
	controller->additional_page_phys_addr = additional_frame;
	controller->additional_page_offset = 0;
}

static void set_up_dcbaa(USB_Controller* controller)
{
	u64 dcbaa_size = (controller->max_slots + 1) * 8;

	// Allocate memory for DCBAA. DCBAA has maximum of 256 entries, that makes 256 * 8 = 2048 bytes.
	// 2048 < 4096, so one page is enough.
	Phys_addr dcbaa_phys_addr = allocate_contiguous_general_frames(1, controller->max_frame_number);
	if (dcbaa_phys_addr == INVALID_ADDR)
		panic("Could not allocate mem for DCBAA.");

	Virt_addr dcbaa_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
	map_address(dcbaa_virt_addr, dcbaa_phys_addr, PTE_WRITABLE);
	memset((void*)dcbaa_virt_addr, 0, dcbaa_size);

	controller->dcbaa_virt_addr = dcbaa_virt_addr;
	controller->dcbaa_phys_addr = dcbaa_phys_addr;

	volatile u64* dcbaap = (volatile u64*)&controller->op_regs[12];

	volatile u32* xhci_regs = (u32*)(controller->mmio_addr);
	bool supports_64bit_dma = (xhci_regs[4] & 1) != 0;

	if (supports_64bit_dma)
		*dcbaap = dcbaa_phys_addr;
	else
		*(volatile u32*)(dcbaap) = (u32)(dcbaa_phys_addr);
}

static void set_up_scratchpad(USB_Controller* controller)
{
	if (controller->scratchpad_count == 0)
		return;

	u32 page_size = controller->op_regs[2];

	if (!(page_size & 1))
		panic("xHCI does not support 4 KiB pages.");

	u64 scratchpad_array_size = controller->scratchpad_count * 8;
	u64 scratchpad_array_page_count = (scratchpad_array_size + FRAME_SIZE - 1) / FRAME_SIZE;

	Phys_addr scratchpad_start_frame_phys_addr =
		allocate_contiguous_general_frames(scratchpad_array_page_count, controller->max_frame_number);
	Virt_addr scratchpad_start_page_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, scratchpad_array_page_count);


	u64 page_addr = scratchpad_start_page_virt_addr;
	u64 frame_addr = scratchpad_start_frame_phys_addr;
	for (u64 i = 0; i < scratchpad_array_page_count; ++i)
	{
		map_address(page_addr, frame_addr, PTE_WRITABLE);

		page_addr += FRAME_SIZE;
		frame_addr += FRAME_SIZE;
	}

	memset((void*)scratchpad_start_page_virt_addr, 0, scratchpad_array_size);

	*(u64*)(controller->dcbaa_virt_addr) = scratchpad_start_frame_phys_addr;
	controller->scratchpad_virt_addr = scratchpad_start_page_virt_addr;

	u32 n = controller->scratchpad_count;
	for (u32 i = 0; i < n; ++i)
	{
		Phys_addr phys_addr =
			allocate_contiguous_general_frames(1, controller->max_frame_number);

		Virt_addr virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
		map_address(virt_addr, phys_addr, PTE_WRITABLE);
		memset((void*)virt_addr, 0, FRAME_SIZE);

		u64* scratchpad_addresses = (u64*)(controller->scratchpad_virt_addr);
		scratchpad_addresses[i] = phys_addr;
	}
}

static void set_up_command_ring(USB_Controller* controller)
{
	// 256 TRBs, 16 B each. 256 * 16 = 4096.
	Phys_addr command_ring_phys_addr = allocate_contiguous_general_frames(1, controller->max_frame_number);
	Virt_addr command_ring_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
	map_address(command_ring_virt_addr, command_ring_phys_addr, PTE_WRITABLE);
	memset((void*)command_ring_virt_addr, 0, FRAME_SIZE);

	controller->command_ring_virt_addr = command_ring_virt_addr;
	controller->command_ring_phys_addr = command_ring_phys_addr;
	controller->command_ring_trb_index = 0;
	controller->command_ring_cycle_state = true;

	Virt_addr link_trb_address = command_ring_virt_addr + 255 * 16;

	u32 link_trb[4];
	*(u64*)&link_trb[0] = command_ring_phys_addr;
	link_trb[2] = 0;

	// link_trb[3] = 1;
	// link_trb[3] |= 6 << 10;
	// link_trb[3] |= (1 << 16);

	link_trb[3] = (6 << 10) | (1 << 1);

	memcpy((char*)link_trb_address, (char*)link_trb, 16);

	volatile u64* crcr = (volatile u64*)&controller->op_regs[6];

	if (controller->supports_64bit_dma)
		*crcr = command_ring_phys_addr | 1; // set RCS
	else
		*(volatile u32*)crcr = (u32)command_ring_phys_addr | 1;
}

static void set_up_event_ring(USB_Controller* controller)
{
	volatile u32* xhci_regs = (volatile u32*)(controller->mmio_addr);
	u32 rtsoff = xhci_regs[6] & ~0x1FU;
	controller->runtime_regs = (volatile u32*)(controller->mmio_addr + rtsoff);


	volatile u32* erstz = &controller->runtime_regs[10];
	volatile u64* erstba = (volatile u64*)&controller->runtime_regs[12];
	volatile u64* erdp = (volatile u64*)&controller->runtime_regs[14]; // event ring deque pointer

	Phys_addr event_ring_phys_addr =
		allocate_contiguous_general_frames(1, controller->max_frame_number);
	Virt_addr event_ring_virt_addr = arena_allocate_pages(&xhci_driver_page_allocator, 1);
	map_address(event_ring_virt_addr, event_ring_phys_addr, PTE_WRITABLE);
	memset((void*)event_ring_virt_addr, 0, FRAME_SIZE);

	controller->event_ring_virt_addr = event_ring_virt_addr;
	controller->event_ring_phys_addr = event_ring_phys_addr;

	u8 erst[16] = {0};
	*(u64*)&erst[0] = event_ring_phys_addr;
	*(u16*)&erst[8] = 256;

	// Copy ERST to additional XHCI page.
	controller->additional_page_offset = (controller->additional_page_offset + 63) & ~63ULL;
	Virt_addr new_erst_address = controller->additional_page_virt_addr + controller->additional_page_offset;
	Phys_addr phys_erst_address = controller->additional_page_phys_addr + controller->additional_page_offset;
	memcpy((void*)new_erst_address, erst, sizeof(erst));
	controller->additional_page_offset += sizeof(erst);

	*erstz = 1;

	if (controller->supports_64bit_dma)
	{
		*erdp = event_ring_phys_addr;
		*erstba = phys_erst_address;
	}
	else
	{
		controller->runtime_regs[14] = (u32)event_ring_phys_addr;
		controller->runtime_regs[12] = (u32)phys_erst_address;
	}

	controller->event_ring_trb_index = 0;
	controller->event_ring_cycle_state = true;
}

static void scan_ports(USB_Controller* controller)
{
	for (u8 port = 0; port < controller->max_ports; ++port)
	{
		u64 portsc_index = 256 + port * 4;
		u32 portsc = controller->op_regs[portsc_index];

		bool current_connect_status = (portsc & 1U) != 0;
		if (current_connect_status == true)
			controller->usb_port_states[port] = USB_PORT_CONNECTED;
	}
}

USB_Controller setup_usb_controller(void)
{
	USB_Controller usb_controller = find_usb_controller();
	if (!usb_controller.found)
	{
		panic("Could not find XHCI info.");
	}

	u64 mmio_size = get_mmio_xhci_size(&usb_controller);
	enable_xhci_memory_space(&usb_controller);

	u64 mmio_addr = get_mmio_xhci_address(&usb_controller);
	usb_controller.mmio_addr = mmio_addr;

	u64 mmio_page_addr = mmio_addr & ~(FRAME_SIZE - 1ULL);
	u64 mmio_offset = mmio_addr - mmio_page_addr;

	u64 mmio_page_count =
		(mmio_offset + mmio_size + FRAME_SIZE - 1) / FRAME_SIZE;

	for (u64 i = 0; i < mmio_page_count; ++i)
	{
		u64 page_addr = mmio_page_addr + i * FRAME_SIZE;

		map_address(page_addr, (Phys_addr)page_addr,
					PTE_WRITABLE | PTE_CACHE_DISABLE);
	}

	volatile u32* xhci_regs = (u32*)(mmio_addr);

	u32 cap_length = xhci_regs[0] & 0xFF;
	u32 hcsparams1 = xhci_regs[1];
	u32 hcsparams2 = xhci_regs[2];
	u32 hccparams1 = xhci_regs[4];

	volatile u32* op_regs = (volatile u32*)(mmio_addr + cap_length);
	usb_controller.op_regs = op_regs;

	usb_controller.context_size_64 = (hccparams1  & (1U << 2)) != 0;

	volatile u32* usbcmd = &op_regs[0];
	volatile u32* usbsts = &op_regs[1];
	volatile u32* config = &op_regs[14];

	usb_controller.max_slots = hcsparams1 & 0xFF;


	usb_controller.max_interrupters = (hcsparams1 >> 8) & 0x7FF;
	usb_controller.max_ports = (hcsparams1 >> 24) & 0xFF;
	
	
	usb_controller.scratchpad_count = (((hcsparams2 >> 21) & 0x1F) << 5) | ((hcsparams2 >> 27) & 0x1F);

	take_ownership(&usb_controller);
	stop(&usb_controller);
	restart(&usb_controller);

	// set max slots shared with kernel to max value possible
	u32 config_val = *config;
	config_val = (config_val & ~0xFF) | usb_controller.max_slots;
	*config = config_val;

	usb_controller.supports_64bit_dma = (hccparams1 & 1) != 0;
	usb_controller.max_frame_number = usb_controller.supports_64bit_dma ? FRAME_BITMAP_SIZE * 8 - 1 : 0xFFFFFFFFULL / FRAME_SIZE;

	allocate_additional_page(&usb_controller);

	setup_ports_states(&usb_controller);
	set_up_dcbaa(&usb_controller);
	set_up_scratchpad(&usb_controller);
	set_up_command_ring(&usb_controller);
	set_up_event_ring(&usb_controller);

	*usbcmd |= 1; // enable controller
	while (*usbsts & 1U) {} // Wait until the HCHalted = 0, that means the controller is indeed enabled.

	scan_ports(&usb_controller);

	return usb_controller;
}

bool read_event_trb(USB_Controller* controller, u32 event_trb[4])
{
	Virt_addr current_entry_addr = controller->event_ring_virt_addr + controller->event_ring_trb_index * 16;
	volatile u32* current_entry = (volatile u32*)(current_entry_addr);
	bool cycle_bit = (current_entry[3] & 1) != 0;

	if (cycle_bit != controller->event_ring_cycle_state)
		return false;


	for (u64 i = 0; i < 4; ++i)
		event_trb[i] = current_entry[i];

	++controller->event_ring_trb_index;
	if (controller->event_ring_trb_index == 256)
	{
		controller->event_ring_trb_index = 0;
		controller->event_ring_cycle_state = !controller->event_ring_cycle_state;
	}

	return true;
}

void update_event_ring_dequeue_pointer(USB_Controller* controller)
{
	Phys_addr dequeue_pointer = controller->event_ring_phys_addr + controller->event_ring_trb_index * 16;

	volatile u64* erdp = (volatile u64*)&controller->runtime_regs[14];
	*erdp = dequeue_pointer | (1 << 3);
}

bool write_command_trb(USB_Controller* controller, u32 command_trb[4])
{
	Virt_addr current_entry_addr = controller->command_ring_virt_addr + controller->command_ring_trb_index * 16;
	volatile u32* current_entry = (volatile u32*)(current_entry_addr);

	for (int i = 0; i < 3; ++i)
		current_entry[i] = command_trb[i];

	u32 last_dword = controller->command_ring_cycle_state ? (command_trb[3] | 1) : (command_trb[3] & ~1);

	__atomic_thread_fence(__ATOMIC_RELEASE);
	current_entry[3] = last_dword;

	++controller->command_ring_trb_index;
	if (controller->command_ring_trb_index == 255)
	{
		Virt_addr link_trb_addr = controller->command_ring_virt_addr + 255 * 16;
		volatile u32* link_trb = (volatile u32*)(link_trb_addr);

		u32 link_control =
			controller->command_ring_cycle_state ? (link_trb[3] | 1) : (link_trb[3] & ~1);

		__atomic_thread_fence(__ATOMIC_RELEASE);
		link_trb[3] = link_control;

		if (link_control & (1 << 1))
			controller->command_ring_cycle_state = !controller->command_ring_cycle_state;

		controller->command_ring_trb_index = 0;
	}

	return true;
}

void doorbell_command_ring(USB_Controller* controller)
{
	volatile u32* dboff_reg = (volatile u32*)(controller->mmio_addr + 0x14);
	u32 dboff_offset = *dboff_reg & ~3;
	volatile u32* doorbell = (volatile u32*)(controller->mmio_addr + dboff_offset);

	__atomic_thread_fence(__ATOMIC_RELEASE);
	doorbell[0] = 0;
}
