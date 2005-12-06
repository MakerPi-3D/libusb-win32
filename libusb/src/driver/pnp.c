/* LIBUSB-WIN32, Generic Windows USB Library
 * Copyright (c) 2002-2005 Stephan Meyer <ste_meyer@web.de>
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



static NTSTATUS DDKAPI 
on_start_complete(DEVICE_OBJECT *device_object, IRP *irp, 
                  void *context);

static NTSTATUS DDKAPI
on_device_usage_notification_complete(DEVICE_OBJECT *device_object,
                                      IRP *irp, void *context);

static NTSTATUS DDKAPI 
on_query_capabilities_complete(DEVICE_OBJECT *device_object,
                               IRP *irp, void *context);

static NTSTATUS DDKAPI 
on_query_device_relations_complete(DEVICE_OBJECT *device_object,
                                   IRP *irp, void *context);


NTSTATUS dispatch_pnp(libusb_device_t *dev, IRP *irp)
{
  NTSTATUS status = STATUS_SUCCESS;
  IO_STACK_LOCATION *stack_location = IoGetCurrentIrpStackLocation(irp);
  UNICODE_STRING symbolic_link_name;
  WCHAR tmp_name[128];

  status = remove_lock_acquire(&dev->remove_lock);
  
  if(!NT_SUCCESS(status))
    { 
      return complete_irp(irp, status, 0);
    }

  DEBUG_PRINT_NL();

  switch(stack_location->MinorFunction) 
    {     
    case IRP_MN_REMOVE_DEVICE:

      DEBUG_MESSAGE("dispatch_pnp(): IRP_MN_REMOVE_DEVICE");

      dev->is_started = FALSE;
      remove_lock_release_and_wait(&dev->remove_lock);

      status = pass_irp_down(dev, irp, NULL, NULL); 

      DEBUG_MESSAGE("dispatch_pnp(): deleting device %d", dev->id);
      
      _snwprintf(tmp_name, sizeof(tmp_name)/sizeof(WCHAR), L"%s%04d", 
                 LIBUSB_SYMBOLIC_LINK_NAME, dev->id);
      
      RtlInitUnicodeString(&symbolic_link_name, tmp_name);
      IoDeleteSymbolicLink(&symbolic_link_name);

      device_list_remove(dev);
      IoDetachDevice(dev->next_stack_device);
      IoDeleteDevice(dev->self);

      return status;

    case IRP_MN_SURPRISE_REMOVAL:

      DEBUG_MESSAGE("dispatch_pnp(): IRP_MN_SURPRISE_REMOVAL");
      dev->is_started = FALSE;
      break;

    case IRP_MN_START_DEVICE:

      DEBUG_MESSAGE("dispatch_pnp(): IRP_MN_START_DEVICE");

      if(!dev->topology.is_hub )
        {
          if(NT_SUCCESS(set_configuration(dev, 1, 1000)))
            {
              dev->configuration = 1;
            }
          else
            {
              DEBUG_ERROR("dispatch_pnp(): IRP_MN_START_DEVICE: selecting "
                          "configuration failed");
              dev->configuration = 0;
            }
        }

      /* report device state to Power Manager */
      /* DeviceState has been set to D0 by add_device() */
      PoSetPowerState(dev->self, DevicePowerState, dev->power_state);

      return pass_irp_down(dev, irp, on_start_complete, NULL);

    case IRP_MN_STOP_DEVICE:

      dev->is_started = FALSE;
      DEBUG_MESSAGE("dispatch_pnp(): IRP_MN_STOP_DEVICE");
      break;

    case IRP_MN_DEVICE_USAGE_NOTIFICATION:

      DEBUG_MESSAGE("dispatch_pnp(): IRP_MN_DEVICE_USAGE_NOTIFICATION");
      if(!dev->self->AttachedDevice
         || (dev->self->AttachedDevice->Flags & DO_POWER_PAGABLE))
        {
          dev->self->Flags |= DO_POWER_PAGABLE;
        }

      return pass_irp_down(dev, irp, on_device_usage_notification_complete,
                           NULL);

    case IRP_MN_QUERY_CAPABILITIES: 

      DEBUG_MESSAGE("dispatch_pnp(): IRP_MN_QUERY_CAPABILITIES");

      if(!dev->is_filter)
        {
          stack_location->Parameters.DeviceCapabilities.Capabilities
            ->SurpriseRemovalOK = TRUE;
        }

      return pass_irp_down(dev, irp, on_query_capabilities_complete,  NULL);

    case IRP_MN_QUERY_DEVICE_RELATIONS:

      DEBUG_MESSAGE("dispatch_pnp(): IRP_MN_QUERY_DEVICE_RELATIONS");

      if(stack_location->Parameters.QueryDeviceRelations.Type == BusRelations)
        { 
          return pass_irp_down(dev, irp, on_query_device_relations_complete, 
                               NULL);
        }

      break;

    default:
      ;
    }

  remove_lock_release(&dev->remove_lock);
  return pass_irp_down(dev, irp, NULL, NULL);
}

static NTSTATUS DDKAPI 
on_start_complete(DEVICE_OBJECT *device_object, IRP *irp, void *context)
{
  libusb_device_t *dev = (libusb_device_t *)device_object->DeviceExtension;

  if(irp->PendingReturned)
    {
      IoMarkIrpPending(irp);
    }
  
  if(dev->next_stack_device->Characteristics & FILE_REMOVABLE_MEDIA) 
    {
      device_object->Characteristics |= FILE_REMOVABLE_MEDIA;
    }
  
  remove_lock_release(&dev->remove_lock);
  
  dev->is_started = TRUE;

  return STATUS_SUCCESS;
}

static NTSTATUS DDKAPI 
on_device_usage_notification_complete(DEVICE_OBJECT *device_object,
                                      IRP *irp, void *context)
{
  libusb_device_t *dev = (libusb_device_t *)device_object->DeviceExtension;

  if(irp->PendingReturned)
    {
      IoMarkIrpPending(irp);
    }

  if(!(dev->next_stack_device->Flags & DO_POWER_PAGABLE))
    {
      device_object->Flags &= ~DO_POWER_PAGABLE;
    }

  remove_lock_release(&dev->remove_lock);

  return STATUS_SUCCESS;
}

static NTSTATUS DDKAPI 
on_query_capabilities_complete(DEVICE_OBJECT *device_object,
                               IRP *irp, void *context)
{
  libusb_device_t *dev = (libusb_device_t *)device_object->DeviceExtension;
  IO_STACK_LOCATION *stack_location = IoGetCurrentIrpStackLocation(irp);

  if(irp->PendingReturned)
    {
      IoMarkIrpPending(irp);
    }

  if(NT_SUCCESS(irp->IoStatus.Status))
    {
      if(!dev->is_filter)
        {
          stack_location->Parameters.DeviceCapabilities.Capabilities
            ->SurpriseRemovalOK = TRUE;
        }

      if(dev->topology.is_root_hub)
        {
          dev->topology.port = 
            stack_location
            ->Parameters.DeviceCapabilities.Capabilities->Address + 1;
        }
      else
        {
          dev->topology.port
            = stack_location
            ->Parameters.DeviceCapabilities.Capabilities->Address;
        }

      /* save supported device power states */
      memcpy(dev->device_power_states, stack_location
             ->Parameters.DeviceCapabilities.Capabilities->DeviceState,
             sizeof(dev->device_power_states));
    }

  remove_lock_release(&dev->remove_lock);

  return STATUS_SUCCESS;
}

static NTSTATUS DDKAPI 
on_query_device_relations_complete(DEVICE_OBJECT *device_object,
                                   IRP *irp, void *context)
{
  libusb_device_t *dev = (libusb_device_t *)device_object->DeviceExtension;
  DEVICE_RELATIONS *device_relations;
  int i;

  if(irp->PendingReturned)
    {
      IoMarkIrpPending(irp);
    }

  if(NT_SUCCESS(irp->IoStatus.Status))
    {
      device_relations = (DEVICE_RELATIONS *)irp->IoStatus.Information;

      if(device_relations)
        {
          memset(&dev->topology.child_pdos, 0,
                 sizeof(dev->topology.child_pdos));

          dev->topology.num_child_pdos = 0;
          dev->topology.update = TRUE;

          for(i = 0; (i < (int)device_relations->Count)
                && (i < LIBUSB_MAX_NUMBER_OF_CHILDREN); i++)
            {
              dev->topology.child_pdos[i] = device_relations->Objects[i];
              dev->topology.num_child_pdos++;
            }
        }
    }

  remove_lock_release(&dev->remove_lock);

  return STATUS_SUCCESS;
}
