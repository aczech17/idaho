#ifndef IO_USB_INTERFACE_ENDPOINT_H
#define IO_USB_INTERFACE_ENDPOINT_H

#include "../common.h"

typedef struct
{
	u8 endpoint_address;
	u8 attributes;
	u16 max_packet_size;
	u8 interval;
}USB_Interface_Endpoint;

#endif // IO_USB_INTERFACE_ENDPOINT_H
