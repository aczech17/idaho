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
    // vga_printf("\nPage table at %X\n", global_page_table);

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
		bool received_any = false;

		while (read_event_trb(&xhci_info, event))
		{
			vga_printf("%ZA device has been connected.\n%z", 0x1F);
			received_any = true;
		}

		if (received_any)
			update_event_ring_dequeue_pointer(&xhci_info);
	}

    for (;;);
}
