#include "memory/phys_memory_map.h"
#include "memory/frame_allocator.h"
#include "memory/page_table.h"
#include "interrupt/idt.h"
#include "io/usb_device.h"
#include "vga/vga.h"
#include "io/usb_controller.h"
#include "io/usb_device_manager.h"

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

    setup_idt(idt);
    vga_printf("\nIDT set up.%z\n");

    USB_Controller usb_controller = setup_usb_controller();
	if (!usb_controller.found)
	{
		panic("Could not configure USB controller.");
	}
    vga_printf("USB controller set up.\n\n");

	USB_Device_Manager dvm;
	setup_usb_device_manager(&dvm, &usb_controller);

	//
	// USB_Device keyboard_driver;
	// setup_usb_device(&keyboard_driver, &xhci_info);
	//
	// vga_printf("USB setup completed.\n");
	// vga_printf("port: %X, slot: %X\n", keyboard_driver.port_id, keyboard_driver.slot_id);

    for (;;);
}
