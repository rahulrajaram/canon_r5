// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * USB transport layer
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/completion.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"
#include "../../include/core/canon-r5-usb.h"

/* USB transport layer structure */
struct canon_r5_usb {
	struct usb_device	*udev;
	struct usb_interface	*intf;
	struct usb_endpoint_descriptor *ep_int_in;
	struct usb_endpoint_descriptor *ep_bulk_in;
	struct usb_endpoint_descriptor *ep_bulk_out;
	struct urb		*int_urb;
	u8			*int_buffer;
	size_t			max_packet_size;
};

MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_DESCRIPTION("Canon R5 Camera Driver Suite - USB Transport");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(CANON_R5_DRIVER_VERSION);
MODULE_SOFTDEP("pre: canon-r5-core");

/* Forward declarations for USB transport functions */

/* USB device table - PIDs will be updated when actual device is analyzed */
static const struct usb_device_id canon_r5_usb_id_table[] = {
	{ USB_DEVICE(CANON_USB_VID, CANON_R5_PID_NORMAL) },
	{ USB_DEVICE(CANON_USB_VID, CANON_R5_PID_PC_CONNECT) },
	{ }
};
MODULE_DEVICE_TABLE(usb, canon_r5_usb_id_table);

/* USB bulk transfer completion callback */
/* Not currently used; keep as placeholder but annotate to avoid warnings */
static void __maybe_unused canon_r5_usb_bulk_callback(struct urb *urb)
{
	struct canon_r5_device *dev = urb->context;
	
	if (!dev) {
		pr_err("canon-r5-usb: NULL device in bulk callback\n");
		return;
	}
	
	switch (urb->status) {
	case 0:
		/* Success */
		canon_r5_dbg(dev, "USB bulk transfer completed successfully (%d bytes)",
			    urb->actual_length);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* URB was cancelled */
		canon_r5_dbg(dev, "USB bulk transfer cancelled");
		break;
	case -EPROTO:
		canon_r5_warn(dev, "USB protocol error in bulk transfer");
		break;
	case -EILSEQ:
		canon_r5_warn(dev, "USB CRC error in bulk transfer");
		break;
	case -ETIME:
		canon_r5_warn(dev, "USB timeout in bulk transfer");
		break;
	case -EPIPE:
		canon_r5_warn(dev, "USB endpoint stalled in bulk transfer");
		usb_clear_halt(dev->usb->udev, urb->pipe);
		break;
	default:
		canon_r5_err(dev, "USB bulk transfer failed with error %d", urb->status);
		break;
	}
}

/* USB interrupt transfer completion callback */
static void canon_r5_usb_int_callback(struct urb *urb)
{
	struct canon_r5_device *dev = urb->context;
	int ret;
	
	if (!dev) {
		pr_err("canon-r5-usb: NULL device in interrupt callback\n");
		return;
	}
	
	switch (urb->status) {
	case 0:
		/* Success - process the interrupt data */
		canon_r5_dbg(dev, "USB interrupt received (%d bytes)", urb->actual_length);
		
		/* Trigger event processing */
		canon_r5_notify_event(dev, 0, NULL);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* URB was cancelled */
		canon_r5_dbg(dev, "USB interrupt transfer cancelled");
		return;
	case -EPROTO:
		canon_r5_warn(dev, "USB protocol error in interrupt transfer");
		break;
	case -EILSEQ:
		canon_r5_warn(dev, "USB CRC error in interrupt transfer");
		break;
	case -ETIME:
		canon_r5_warn(dev, "USB timeout in interrupt transfer");
		break;
	case -EPIPE:
		canon_r5_warn(dev, "USB endpoint stalled in interrupt transfer");
		usb_clear_halt(dev->usb->udev, urb->pipe);
		break;
	default:
		canon_r5_err(dev, "USB interrupt transfer failed with error %d", urb->status);
		break;
	}
	
	/* Resubmit the interrupt URB if still connected */
	if (canon_r5_get_state(dev) != CANON_R5_STATE_DISCONNECTED) {
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret) {
			canon_r5_err(dev, "Failed to resubmit interrupt URB: %d", ret);
		}
	}
}

/* Initialize USB endpoints and URBs */
static int canon_r5_usb_init_endpoints(struct canon_r5_device *dev)
{
	struct usb_interface *intf = dev->usb->intf;
	struct usb_host_interface *alt_setting;
	struct usb_endpoint_descriptor *ep_desc;
	int i, ret;
	
	alt_setting = intf->cur_altsetting;
	
	/* Find the required endpoints */
	for (i = 0; i < alt_setting->desc.bNumEndpoints; i++) {
		ep_desc = &alt_setting->endpoint[i].desc;
		
		if (usb_endpoint_is_int_in(ep_desc) &&
		    ep_desc->bEndpointAddress == CANON_R5_EP_INT_IN) {
			dev->usb->ep_int_in = ep_desc;
			canon_r5_dbg(dev, "Found interrupt IN endpoint: 0x%02x",
				    ep_desc->bEndpointAddress);
		} else if (usb_endpoint_is_bulk_in(ep_desc) &&
			   ep_desc->bEndpointAddress == CANON_R5_EP_BULK_IN) {
			dev->usb->ep_bulk_in = ep_desc;
			canon_r5_dbg(dev, "Found bulk IN endpoint: 0x%02x",
				    ep_desc->bEndpointAddress);
		} else if (usb_endpoint_is_bulk_out(ep_desc) &&
			   ep_desc->bEndpointAddress == CANON_R5_EP_BULK_OUT) {
			dev->usb->ep_bulk_out = ep_desc;
			canon_r5_dbg(dev, "Found bulk OUT endpoint: 0x%02x",
				    ep_desc->bEndpointAddress);
		}
	}
	
	/* Verify we found all required endpoints */
	if (!dev->usb->ep_int_in || !dev->usb->ep_bulk_in || !dev->usb->ep_bulk_out) {
		canon_r5_err(dev, "Missing required USB endpoints");
		return -ENODEV;
	}
	
	/* Calculate max packet size */
	dev->usb->max_packet_size = max_t(size_t,
					usb_endpoint_maxp(dev->usb->ep_bulk_in),
					usb_endpoint_maxp(dev->usb->ep_bulk_out));
	
	canon_r5_info(dev, "USB endpoints initialized, max packet size: %zu",
		     dev->usb->max_packet_size);
	
	/* Allocate interrupt URB and buffer */
	dev->usb->int_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->usb->int_urb) {
		canon_r5_err(dev, "Failed to allocate interrupt URB");
		return -ENOMEM;
	}
	
	dev->usb->int_buffer = kmalloc(usb_endpoint_maxp(dev->usb->ep_int_in), GFP_KERNEL);
	if (!dev->usb->int_buffer) {
		canon_r5_err(dev, "Failed to allocate interrupt buffer");
		usb_free_urb(dev->usb->int_urb);
		dev->usb->int_urb = NULL;
		return -ENOMEM;
	}
	
	/* Initialize interrupt URB */
	usb_fill_int_urb(dev->usb->int_urb, dev->usb->udev,
			 usb_rcvintpipe(dev->usb->udev, dev->usb->ep_int_in->bEndpointAddress),
			 dev->usb->int_buffer, usb_endpoint_maxp(dev->usb->ep_int_in),
			 canon_r5_usb_int_callback, dev, dev->usb->ep_int_in->bInterval);
	
	/* Submit interrupt URB */
	ret = usb_submit_urb(dev->usb->int_urb, GFP_KERNEL);
	if (ret) {
		canon_r5_err(dev, "Failed to submit interrupt URB: %d", ret);
		kfree(dev->usb->int_buffer);
		dev->usb->int_buffer = NULL;
		usb_free_urb(dev->usb->int_urb);
		dev->usb->int_urb = NULL;
		return ret;
	}
	
	return 0;
}

/* Cleanup USB resources */
static void canon_r5_usb_cleanup_endpoints(struct canon_r5_device *dev)
{
	if (dev->usb->int_urb) {
		usb_kill_urb(dev->usb->int_urb);
		usb_free_urb(dev->usb->int_urb);
		dev->usb->int_urb = NULL;
	}
	
	if (dev->usb->int_buffer) {
		kfree(dev->usb->int_buffer);
		dev->usb->int_buffer = NULL;
	}
}

/* Send data via USB bulk out */
int canon_r5_usb_bulk_send(struct canon_r5_device *dev, const void *data, size_t len)
{
	void *transfer_buffer;
	int actual_len;
	int ret;
	
	if (!dev || !data || !len)
		return -EINVAL;
	
	if (!dev->usb->ep_bulk_out)
		return -ENODEV;
	
	/* Allocate transfer buffer */
	transfer_buffer = kmalloc(len, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;
	
	/* Copy data to transfer buffer */
	memcpy(transfer_buffer, data, len);
	
	/* Use synchronous bulk transfer */
	ret = usb_bulk_msg(dev->usb->udev,
			   usb_sndbulkpipe(dev->usb->udev, dev->usb->ep_bulk_out->bEndpointAddress),
			   transfer_buffer, len, &actual_len, 5000);
	
	if (ret) {
		canon_r5_err(dev, "Bulk send failed: %d", ret);
	} else {
		canon_r5_dbg(dev, "Bulk send completed successfully (%d bytes)", actual_len);
	}
	
	kfree(transfer_buffer);
	
	return ret;
}
EXPORT_SYMBOL_GPL(canon_r5_usb_bulk_send);

/* Receive data via USB bulk in */
int canon_r5_usb_bulk_receive(struct canon_r5_device *dev, void *data, size_t len, size_t *actual_len)
{
	void *transfer_buffer;
	int received_len;
	int ret;
	
	if (!dev || !data || !len)
		return -EINVAL;
	
	if (!dev->usb->ep_bulk_in)
		return -ENODEV;
	
	/* Allocate transfer buffer */
	transfer_buffer = kmalloc(len, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;
	
	/* Use synchronous bulk transfer */
	ret = usb_bulk_msg(dev->usb->udev,
			   usb_rcvbulkpipe(dev->usb->udev, dev->usb->ep_bulk_in->bEndpointAddress),
			   transfer_buffer, len, &received_len, 5000);
	
	if (ret) {
		canon_r5_err(dev, "Bulk receive failed: %d", ret);
	} else {
		/* Copy received data */
		memcpy(data, transfer_buffer, received_len);
		if (actual_len)
			*actual_len = received_len;
		
		canon_r5_dbg(dev, "Bulk receive completed successfully (%d bytes)", received_len);
	}
	
	kfree(transfer_buffer);
	
	return ret;
}
EXPORT_SYMBOL_GPL(canon_r5_usb_bulk_receive);

/* USB transport operations */
static struct canon_r5_transport_ops usb_transport_ops = {
	.bulk_send = canon_r5_usb_bulk_send,
	.bulk_receive = canon_r5_usb_bulk_receive,
};

/* USB device probe function */
static int canon_r5_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct canon_r5_device *dev;
	int ret;
	
	(void)id; /* Unused parameter */
	
	pr_info("canon-r5-usb: Probing Canon R5 device (VID: 0x%04x, PID: 0x%04x)\n",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct));
	
	/* Allocate device structure */
	dev = canon_r5_device_alloc(&intf->dev);
	if (!dev) {
		dev_err(&intf->dev, "Failed to allocate device structure\n");
		return -ENOMEM;
	}
	
	/* Allocate USB transport structure */
	dev->usb = kzalloc(sizeof(struct canon_r5_usb), GFP_KERNEL);
	if (!dev->usb) {
		dev_err(&intf->dev, "Failed to allocate USB transport data\n");
		canon_r5_device_put(dev);
		return -ENOMEM;
	}
	
	/* Initialize USB layer */
	dev->usb->udev = usb_get_dev(udev);
	dev->usb->intf = usb_get_intf(intf);
	
	/* Set device data */
	usb_set_intfdata(intf, dev);
	
	/* Initialize USB endpoints */
	ret = canon_r5_usb_init_endpoints(dev);
	if (ret) {
		dev_err(&intf->dev, "Failed to initialize USB endpoints: %d\n", ret);
		goto error_endpoints;
	}
	
	/* Register transport layer */
	ret = canon_r5_register_transport(dev, &usb_transport_ops);
	if (ret) {
		dev_err(&intf->dev, "Failed to register transport: %d\n", ret);
		goto error_endpoints;
	}
	
	/* Initialize device */
	ret = canon_r5_device_initialize(dev);
	if (ret) {
		dev_err(&intf->dev, "Failed to initialize device: %d\n", ret);
		goto error_transport;
	}
	
	canon_r5_set_state(dev, CANON_R5_STATE_CONNECTED);
	
	/* TODO: Initialize PTP session and detect device capabilities */
	
	dev_info(&intf->dev, "Canon R5 device successfully probed and initialized\n");
	
	return 0;
	
error_transport:
	canon_r5_unregister_transport(dev);
	canon_r5_usb_cleanup_endpoints(dev);
error_endpoints:
	usb_put_intf(dev->usb->intf);
	usb_put_dev(dev->usb->udev);
	usb_set_intfdata(intf, NULL);
	canon_r5_device_put(dev);
	return ret;
}

/* USB device disconnect function */
static void canon_r5_usb_disconnect(struct usb_interface *intf)
{
	struct canon_r5_device *dev = usb_get_intfdata(intf);
	
	if (!dev)
		return;
	
	dev_info(&intf->dev, "Canon R5 device disconnecting\n");
	
	/* Unregister transport */
	canon_r5_unregister_transport(dev);
	
	/* Cleanup device */
	canon_r5_device_cleanup(dev);
	
	/* Cleanup USB resources */
	canon_r5_usb_cleanup_endpoints(dev);
	
	/* Release USB references */
	usb_put_intf(dev->usb->intf);
	usb_put_dev(dev->usb->udev);
	
	/* Free USB transport structure */
	kfree(dev->usb);
	dev->usb = NULL;
	
	usb_set_intfdata(intf, NULL);
	
	/* Release device */
	canon_r5_device_put(dev);
	
	dev_info(&intf->dev, "Canon R5 device disconnected\n");
}

static struct usb_driver canon_r5_usb_driver = {
	.name = CANON_R5_MODULE_NAME "-usb",
	.id_table = canon_r5_usb_id_table,
	.probe = canon_r5_usb_probe,
	.disconnect = canon_r5_usb_disconnect,
	.supports_autosuspend = 1,
};

static int __init canon_r5_usb_init(void)
{
	int ret;
	
	pr_info("Canon R5 Driver Suite - USB Transport Module Loading\n");
	
	/* Register USB driver - core module should be loaded separately */
	ret = usb_register(&canon_r5_usb_driver);
	if (ret) {
		pr_err("Failed to register USB driver: %d\n", ret);
		return ret;
	}
	
	pr_info("Canon R5 Driver Suite - USB Transport Module Loaded\n");
	return 0;
}

static void __exit canon_r5_usb_exit(void)
{
	pr_info("Canon R5 Driver Suite - USB Transport Module Unloading\n");
	
	usb_deregister(&canon_r5_usb_driver);
	
	pr_info("Canon R5 Driver Suite - USB Transport Module Unloaded\n");
}

module_init(canon_r5_usb_init);
module_exit(canon_r5_usb_exit);
