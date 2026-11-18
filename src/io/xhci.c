#include "xhci.h"
#include "../memory/memory.h"
#include "../memory/page_table.h"

static XHCI_Info find_xhci_info(void)
{
	XHCI_Info xhci_info = {0};

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

static void take_xhci_ownership(const u32 mmio_addr)
{
	u32* regs = (u32*)mmio_addr;

	u32 hccparams1 = regs[4];
	u32 xecp = ((hccparams1 >> 16) & 0xFFFF);

	u32 xecp_offset = xecp * 4;

	while (xecp_offset !=0 )
	{
		volatile u32* header_pointer = (u32*)(mmio_addr + xecp_offset);
		u32 header = *header_pointer;

		u8 capability_id = header & 0xFF;
		if (capability_id == 1) // USB legacy support found
		{
			*header_pointer |= (1U << 24);

			// Wait until the BIOS releases the controller.
			while (((*header_pointer) & (1U << 16)) != 0) {}

			vga_printf("ownership taken\n");

			break;
		}

		u8 next_offset_pointer = (header >> 8) & 0xFF;
		xecp_offset = next_offset_pointer * 4;
	}
}

static void stop(const u32 mmio_addr)
{
	volatile u32* regs = (u32*)(mmio_addr);
	u32 operational_offset = regs[0] & 0xFF;

	volatile u32* operational_regs = (volatile u32*)(mmio_addr + operational_offset);
	volatile u32* usbcmd = &operational_regs[0];
	volatile u32* usbsts = &operational_regs[1];

	*usbcmd &= ~1U;

	while (((*usbsts) & 1U) == 0) {}

	vga_printf("controller stopped\n");
}

static void restart(const u32 mmio_addr)
{
	volatile u32* regs = (u32*)(mmio_addr);
	u32 operational_offset = regs[0] & 0xFF;

	volatile u32* operational_regs = (volatile u32*)(mmio_addr + operational_offset);
	volatile u32* usbcmd = &operational_regs[0];
	volatile u32* usbsts = &operational_regs[1];

	*usbcmd |= (1U << 1);

	while (((*usbsts) & (1U << 1)) != 0) {} // host controller reset (HCRST)
	while (((*usbsts) & (1U << 11)) != 0) {} // controller not ready (CNR)

	vga_printf("controller restarted\n");
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

	u32* xhci_regs = (u32*)(mmio_addr);

	u32 hcsparams1 = xhci_regs[1];

	xhci_info.max_slots = hcsparams1 & 0xFF;
	xhci_info.max_interrupters = (hcsparams1 >> 8) & 0x7FF;
	xhci_info.max_ports = (hcsparams1 >> 24) & 0xFF;

	u32 hcsparams2 = xhci_regs[2];
	xhci_info.scratchpad_count = (((hcsparams2 >> 21) & 0x1F) << 5) | ((hcsparams2 >> 27) & 0x1F);

	take_xhci_ownership(mmio_addr);
	stop(mmio_addr);
	restart(mmio_addr);

	return xhci_info;
}
