#include "memory/phys_memory_map.h"
#include "memory/frame_allocator.h"
#include "memory/page_table.h"
#include "interrupt/idt.h"
#include "vga/vga.h"
#include "io/xhci.h"

Page_table* global_page_table;
static Idt_entry idt[256];
Frame_allocator global_frame_allocator;

static void print_memory_map(const Phys_memory_map* memory_regions, const char* name)
{
    vga_printf("%Z%s:%z\n", 0x09 ,name);
    u64 total_memory_available = 0;
    for (u32 i = 0; i < memory_regions->region_count; ++i)
    {
        total_memory_available += (memory_regions->end_addr[i] - memory_regions->start_addr[i] + 1);
        vga_printf("start = %X, end = %X\n", memory_regions->start_addr[i], memory_regions->end_addr[i]);
    }
    // u64 total_frames_available = total_memory_available / FRAME_SIZE;

    vga_printf("%s: %X bytes\n", name, total_memory_available);
    // vga_printf("Total frames in %s: %X\n%z\n", name, total_frames_available);
}

bool detect_usb_device_connected(XHCI_Info* xhci, u32 event[4], u8* port_id)
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

bool reset_port(XHCI_Info* xhci_info, u8 port_id)
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

void enable_slot(XHCI_Info* info, u8 slot_type)
{
	u32 enable_slot_command[4] = {0};
	enable_slot_command[3] = 9 << 10;
	enable_slot_command[3] |= (slot_type << 16);

	write_command_trb(info, enable_slot_command);
	doorbell_command_ring(info);
}

bool detect_enable_completion(u32 event_trb[4], u8* slot_id)
{
	const u32 COMMAND_COMPLETION_EVENT = 33;

	u32 event_type = (event_trb[3] >> 10) & 0x3F;
	if (event_type != COMMAND_COMPLETION_EVENT)
		return false;

	u8 completion_code = (event_trb[2] >> 24) & 0xFF;
	if (!completion_code)
		return false;

	*slot_id = (event_trb[3] >> 24) & 0xFF;

	return true;
}


void kernel_main(const Kernel_mem_info* kernel_mem_info)
{
    const Phys_addr LOW_MEM_SIZE = 1 << 20;
    const u64 KERNEL_MEM_SIZE = 1 << 30; // The government will feed itself.

    disable_bios_cursor();
    clear_screen();
    vga_printf("%ZIdaho OS. Witamy!\n%z\n", 0x1F);


    Phys_memory_map phys_memory_regions;
    determine_available_phys_addresses(&phys_memory_regions, kernel_mem_info, LOW_MEM_SIZE);

    Phys_memory_map kernel_regions;
    register_kernel_regions(&kernel_regions, kernel_mem_info);

    print_memory_map(&phys_memory_regions, "available memory");
    print_memory_map(&kernel_regions, "kernel memory");


    setup_frame_allocator(&phys_memory_regions, &kernel_regions, KERNEL_MEM_SIZE);

    global_page_table = (Page_table*)(kernel_mem_info->page_table_address);

    setup_idt(idt);
    vga_printf("\nIDT set up.%z\n");

    XHCI_Info xhci_info = setup_xhci();
	if (!xhci_info.found)
	{
		panic("Could not find XHCI config.");
	}


    vga_printf("XHCI set up.\n\n");

	vga_printf("Waiting for a USB device... ");

	u32 event[4];
	while (1)
	{
		while (!read_event_trb(&xhci_info, event)) {}
		update_event_ring_dequeue_pointer(&xhci_info);

		u8 port_id;

		if (!detect_usb_device_connected(&xhci_info, event, &port_id))
			continue;

		if (reset_port(&xhci_info, port_id))
		{
			vga_printf("Port %X reset success.\n", port_id);
			break;
		}
	}

	enable_slot(&xhci_info, 0);
	u8 slot_id;

	while (1)
	{
		while (!read_event_trb(&xhci_info, event)) {}
		update_event_ring_dequeue_pointer(&xhci_info);

		if (detect_enable_completion(event, &slot_id))
			break;
	}

	vga_printf("Got slot ID = %X\n", slot_id);



    for (;;);
}
