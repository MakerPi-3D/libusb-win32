/* LIBUSB-WIN32, Generic Windows USB Driver
 * Copyright (C) 2002-2004 Stephan Meyer, <ste_meyer@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "libusb_driver.h"



NTSTATUS release_interface(libusb_device_extension *device_extension,
			   int interface)
{
  debug_printf(LIBUSB_DEBUG_MSG, "release_interface(): interface %d",
	       interface);

  if(interface >= LIBUSB_MAX_NUMBER_OF_INTERFACES)
    {
      debug_printf(LIBUSB_DEBUG_ERR, "release_interface(): invalid interface "
		   "%d", interface);
      return STATUS_INVALID_PARAMETER;
    }

  if(!device_extension->interface_info[interface].valid)
    {
      debug_printf(LIBUSB_DEBUG_ERR, "release_interface(): invalid interface "
		   "%02d", interface);
      return STATUS_INVALID_PARAMETER;
    }

  if(!device_extension->interface_info[interface].claimed)
    {
      debug_printf(LIBUSB_DEBUG_ERR, "claim_interface(): could not release "
		   "interface %d, interface is not claimed", interface);
      return STATUS_INVALID_DEVICE_STATE;
    }

  device_extension->interface_info[interface].claimed = FALSE;

  return STATUS_SUCCESS;
}

NTSTATUS release_all_interfaces(libusb_device_extension *device_extension)
{
  int i;
  
  for(i = 0; i < LIBUSB_MAX_NUMBER_OF_INTERFACES; i++)
    {
      device_extension->interface_info[i].claimed = FALSE;
    }

  return STATUS_SUCCESS;
}
