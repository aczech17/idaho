#include "memory/phys_memory_map.h"
#include "memory/frame_allocator.h"
#include "memory/page_table.h"
#include "interrupt/idt.h"
#include "vga.h"
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
    vga_printf("%ZModern OS. Witamy!\n%z\n", 0x1F);





    Phys_memory_map phys_memory_regions;
    determine_available_phys_addresses(&phys_memory_regions, kernel_mem_info, LOW_MEM_SIZE);

    Phys_memory_map kernel_regions;
    register_kernel_regions(&kernel_regions, kernel_mem_info);

    print_memory_map(&phys_memory_regions, "available memory");
    print_memory_map(&kernel_regions, "kernel memory");


    setup_frame_allocator(&global_frame_allocator, &phys_memory_regions, &kernel_regions, KERNEL_MEM_SIZE);

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

    /*
	    u8 bus;
		u8 device;
		u8 function;
		u64 mmio_addr;
		u8 max_slots;
		u16 max_interrupters;
		u8 max_ports;
		u32 scratchpad_count
     */

    vga_printf("XHCI info:\n");
	vga_printf("bus = %X\n", xhci_info.bus);
	vga_printf("device = %X\n", xhci_info.device);
	vga_printf("function = %X\n", xhci_info.function);
	vga_printf("mmio_addr = %X\n", xhci_info.mmio_addr);
	vga_printf("max_slots = %X\n", xhci_info.max_slots);
	vga_printf("max_interruputers = %X\n", xhci_info.max_interrupters);
	vga_printf("max_ports = %X\n", xhci_info.max_ports);
	vga_printf("scratchpad_count = %X\n", xhci_info.scratchpad_count);

    // vga_printf("\nTrying to provoke page fault.\n");
    // volatile u8* addr = (u8*)(1 << 31);
    // *addr = 0x37;
    //
    // vga_printf("%Z\nval=%X\nOK%z\n", 0x2F, *addr);


    for (;;);
}
