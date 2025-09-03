// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * Core driver infrastructure
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/workqueue.h>
#include <linux/usb.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"

MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_DESCRIPTION("Canon R5 Camera Driver Suite - Core Module");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(CANON_R5_DRIVER_VERSION);

static struct class *canon_r5_class;
static DEFINE_IDR(canon_r5_device_idr);
static DEFINE_MUTEX(canon_r5_device_lock);

static void canon_r5_device_release(struct kref *kref)
{
	struct canon_r5_device *dev = container_of(kref, struct canon_r5_device, kref);
	
	canon_r5_dbg(dev, "Releasing device");
	
	if (dev->ptp.event_wq)
		destroy_workqueue(dev->ptp.event_wq);
	
	idr_destroy(&dev->transaction_idr);
	
	kfree(dev->serial_number);
	kfree(dev->firmware_version);
	kfree(dev);
}

void canon_r5_device_get(struct canon_r5_device *dev)
{
	kref_get(&dev->kref);
}
EXPORT_SYMBOL_GPL(canon_r5_device_get);

void canon_r5_device_put(struct canon_r5_device *dev)
{
	if (dev)
		kref_put(&dev->kref, canon_r5_device_release);
}
EXPORT_SYMBOL_GPL(canon_r5_device_put);

struct canon_r5_device *canon_r5_device_alloc(struct device *parent)
{
	struct canon_r5_device *dev;
	int id;
	
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	
	kref_init(&dev->kref);
	mutex_init(&dev->state_lock);
	mutex_init(&dev->ptp.lock);
	spin_lock_init(&dev->transaction_lock);
	
	idr_init(&dev->transaction_idr);
	
	dev->state = CANON_R5_STATE_DISCONNECTED;
	dev->ptp.session_id = 0;
	dev->ptp.transaction_id = 1;
	dev->ptp.session_open = false;
	
	mutex_lock(&canon_r5_device_lock);
	id = idr_alloc(&canon_r5_device_idr, dev, 0, 0, GFP_KERNEL);
	mutex_unlock(&canon_r5_device_lock);
	
	if (id < 0) {
		canon_r5_device_put(dev);
		return NULL;
	}
	
	dev->dev = device_create(canon_r5_class, parent, MKDEV(0, 0), dev,
				"canon-r5-%d", id);
	if (IS_ERR(dev->dev)) {
		mutex_lock(&canon_r5_device_lock);
		idr_remove(&canon_r5_device_idr, id);
		mutex_unlock(&canon_r5_device_lock);
		canon_r5_device_put(dev);
		return NULL;
	}
	
	canon_r5_info(dev, "Canon R5 device allocated (id=%d)", id);
	
	return dev;
}
EXPORT_SYMBOL_GPL(canon_r5_device_alloc);

int canon_r5_device_initialize(struct canon_r5_device *dev)
{
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Initializing Canon R5 device");
	
	/* Create event workqueue */
	dev->ptp.event_wq = alloc_workqueue("canon-r5-events",
					   WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	if (!dev->ptp.event_wq) {
		canon_r5_err(dev, "Failed to create event workqueue");
		return -ENOMEM;
	}
	
	INIT_WORK(&dev->ptp.event_work, canon_r5_ptp_event_handler);
	
	/* Initialize PTP layer */
	ret = canon_r5_ptp_init(dev);
	if (ret) {
		canon_r5_err(dev, "Failed to initialize PTP layer: %d", ret);
		goto error_ptp;
	}
	
	canon_r5_set_state(dev, CANON_R5_STATE_INITIALIZED);
	
	canon_r5_info(dev, "Canon R5 device initialized successfully");
	return 0;
	
error_ptp:
	destroy_workqueue(dev->ptp.event_wq);
	dev->ptp.event_wq = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(canon_r5_device_initialize);

void canon_r5_device_cleanup(struct canon_r5_device *dev)
{
	if (!dev)
		return;
	
	canon_r5_info(dev, "Cleaning up Canon R5 device");
	
	/* Unregister all driver modules */
	canon_r5_unregister_video_driver(dev);
	canon_r5_unregister_still_driver(dev);
	canon_r5_unregister_audio_driver(dev);
	canon_r5_unregister_storage_driver(dev);
	canon_r5_unregister_control_driver(dev);
	canon_r5_unregister_power_driver(dev);
	canon_r5_unregister_input_driver(dev);
	canon_r5_unregister_lens_driver(dev);
	canon_r5_unregister_display_driver(dev);
	canon_r5_unregister_wireless_driver(dev);
	
	/* Cleanup PTP layer */
	canon_r5_ptp_cleanup(dev);
	
	/* Cancel pending work */
	if (dev->ptp.event_wq) {
		cancel_work_sync(&dev->ptp.event_work);
	}
	
	canon_r5_set_state(dev, CANON_R5_STATE_DISCONNECTED);
}
EXPORT_SYMBOL_GPL(canon_r5_device_cleanup);

int canon_r5_set_state(struct canon_r5_device *dev, enum canon_r5_state new_state)
{
	enum canon_r5_state old_state;
	
	if (!dev)
		return -EINVAL;
	
	mutex_lock(&dev->state_lock);
	old_state = dev->state;
	dev->state = new_state;
	mutex_unlock(&dev->state_lock);
	
	if (old_state != new_state) {
		canon_r5_dbg(dev, "State changed: %d -> %d", old_state, new_state);
	}
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_set_state);

enum canon_r5_state canon_r5_get_state(struct canon_r5_device *dev)
{
	enum canon_r5_state state;
	
	if (!dev)
		return CANON_R5_STATE_DISCONNECTED;
	
	mutex_lock(&dev->state_lock);
	state = dev->state;
	mutex_unlock(&dev->state_lock);
	
	return state;
}
EXPORT_SYMBOL_GPL(canon_r5_get_state);

void canon_r5_notify_event(struct canon_r5_device *dev, int event_type, void *data)
{
	if (!dev)
		return;
	
	canon_r5_dbg(dev, "Event notification: %d", event_type);
	
	/* Trigger event processing workqueue */
	if (dev->ptp.event_wq)
		queue_work(dev->ptp.event_wq, &dev->ptp.event_work);
}
EXPORT_SYMBOL_GPL(canon_r5_notify_event);

/* Driver registration functions */
int canon_r5_register_video_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->video_priv = priv;
	canon_r5_dbg(dev, "Video driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_video_driver);

void canon_r5_unregister_video_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->video_priv = NULL;
		canon_r5_dbg(dev, "Video driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_video_driver);

int canon_r5_register_still_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->still_priv = priv;
	canon_r5_dbg(dev, "Still driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_still_driver);

void canon_r5_unregister_still_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->still_priv = NULL;
		canon_r5_dbg(dev, "Still driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_still_driver);

int canon_r5_register_audio_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->audio_priv = priv;
	canon_r5_dbg(dev, "Audio driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_audio_driver);

void canon_r5_unregister_audio_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->audio_priv = NULL;
		canon_r5_dbg(dev, "Audio driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_audio_driver);

int canon_r5_register_storage_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->storage_priv = priv;
	canon_r5_dbg(dev, "Storage driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_storage_driver);

void canon_r5_unregister_storage_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->storage_priv = NULL;
		canon_r5_dbg(dev, "Storage driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_storage_driver);

int canon_r5_register_control_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->control_priv = priv;
	canon_r5_dbg(dev, "Control driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_control_driver);

void canon_r5_unregister_control_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->control_priv = NULL;
		canon_r5_dbg(dev, "Control driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_control_driver);

int canon_r5_register_power_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->power_priv = priv;
	canon_r5_dbg(dev, "Power driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_power_driver);

void canon_r5_unregister_power_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->power_priv = NULL;
		canon_r5_dbg(dev, "Power driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_power_driver);

int canon_r5_register_input_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->input_priv = priv;
	canon_r5_dbg(dev, "Input driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_input_driver);

void canon_r5_unregister_input_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->input_priv = NULL;
		canon_r5_dbg(dev, "Input driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_input_driver);

int canon_r5_register_lens_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->lens_priv = priv;
	canon_r5_dbg(dev, "Lens driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_lens_driver);

void canon_r5_unregister_lens_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->lens_priv = NULL;
		canon_r5_dbg(dev, "Lens driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_lens_driver);

int canon_r5_register_display_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->display_priv = priv;
	canon_r5_dbg(dev, "Display driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_display_driver);

void canon_r5_unregister_display_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->display_priv = NULL;
		canon_r5_dbg(dev, "Display driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_display_driver);

int canon_r5_register_wireless_driver(struct canon_r5_device *dev, void *priv)
{
	if (!dev)
		return -EINVAL;
	
	dev->wireless_priv = priv;
	canon_r5_dbg(dev, "Wireless driver registered");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_register_wireless_driver);

void canon_r5_unregister_wireless_driver(struct canon_r5_device *dev)
{
	if (dev) {
		dev->wireless_priv = NULL;
		canon_r5_dbg(dev, "Wireless driver unregistered");
	}
}
EXPORT_SYMBOL_GPL(canon_r5_unregister_wireless_driver);

static ssize_t version_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", CANON_R5_DRIVER_VERSION);
}
static CLASS_ATTR_RO(version);

static struct attribute *canon_r5_class_attrs[] = {
	&class_attr_version.attr,
	NULL,
};
ATTRIBUTE_GROUPS(canon_r5_class);

int canon_r5_core_init(void)
{
	int ret;
	
	pr_info("Canon R5 Driver Suite v%s - Core Module Loading\n", CANON_R5_DRIVER_VERSION);
	
	canon_r5_class = class_create(THIS_MODULE, CANON_R5_MODULE_NAME);
	if (IS_ERR(canon_r5_class)) {
		ret = PTR_ERR(canon_r5_class);
		pr_err("Failed to create class: %d\n", ret);
		return ret;
	}
	
	canon_r5_class->class_groups = canon_r5_class_groups;
	
	pr_info("Canon R5 Driver Suite - Core Module Loaded\n");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_core_init);

void canon_r5_core_exit(void)
{
	pr_info("Canon R5 Driver Suite - Core Module Unloading\n");
	
	class_destroy(canon_r5_class);
	
	pr_info("Canon R5 Driver Suite - Core Module Unloaded\n");
}
EXPORT_SYMBOL_GPL(canon_r5_core_exit);

static int __init canon_r5_core_module_init(void)
{
	return canon_r5_core_init();
}

static void __exit canon_r5_core_module_exit(void)
{
	canon_r5_core_exit();
}

module_init(canon_r5_core_module_init);
module_exit(canon_r5_core_module_exit);