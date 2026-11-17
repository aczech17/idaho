#include "xhci.h"
#include "../memory/common.h"
#include "../memory/page_table.h"

static XHCI_Info find_xhci_info(void)
{
	XHCI_Info xhci_info = {.bus = 0, .device = 0, .function = 0, .found = false, .mmio_addr = 0};

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
					xhci_info.bus = bus;
					xhci_info.device = device;
					xhci_info.function = function;
					xhci_info.found = true;

					return xhci_info;
				}
			}

	return xhci_info;
}

static void enable_xhci_memory_space(const XHCI_Info* info)
{
	u32 command_status = pci_read_config(info->bus, info->device, info->function, 0x04);

	u16 command = command_status & 0xFFFF;
	command |= 1;

	pci_write_config(info->bus, info->device, info->function, 0x04, command);
}

static u64 get_mmio_xhci_address(const XHCI_Info* info)
{
	u32 bar0 = pci_read_config(info->bus, info->device, info->function, 0x10);
	u32 bar1 = pci_read_config(info->bus, info->device, info->function, 0x14);

	u32 bar0_addr = bar0 & 0xFFFFFFF0;

	u64 mmio_addr = ((u64)bar1 << 32) | (u64)bar0_addr;
	return mmio_addr;
}

XHCI_Info setup_xhci(void)
{
	XHCI_Info xhci_info = find_xhci_info();
	if (!xhci_info.found)
	{
		panic("Could not find XHCI info.");
	}
	enable_xhci_memory_space(&xhci_info);

	u64 mmio_addr = get_mmio_xhci_address(&xhci_info);
	xhci_info.mmio_addr = mmio_addr;

	Virt_addr mmio_page_addr = mmio_addr & ~0xFFFULL;
	map_address(mmio_page_addr, (Phys_addr)mmio_page_addr, PTE_WRITABLE | PTE_CACHE_DISABLE);

	return xhci_info;
}
