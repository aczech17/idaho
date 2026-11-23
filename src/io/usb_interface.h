#ifndef IO_USB_INTERFACE_H
#define IO_USB_INTERFACE_H

#include "../common.h"
#include "usb_interface_endpoint.h"
#include <stdbool.h>

typedef struct
{
	u8 number;
	u8 alternate_setting;
	u8 endpoint_count;
	u8 class;
	u8 subclass;
	u8 protocol;
	u8 name_index;

	USB_Interface_Endpoint* endpoints;
}USB_Interface;

bool allocate_interface_endpoints(USB_Interface*);

#endif // IO_USB_INTERFACE_H
