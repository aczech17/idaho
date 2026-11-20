#ifndef IO_KEYBOARD_DRIVER_H
#define IO_KEYBOARD_DRIVER_H

#include "xhci.h"

typedef struct
{
	XHCI_Info* xhci_info;

	u8 port_id;
	u8 slot_id;
}Keyboard_driver;

bool setup_keyboard_driver(Keyboard_driver*, XHCI_Info*);

#endif // IO_KEYBOARD_DRIVER_H
