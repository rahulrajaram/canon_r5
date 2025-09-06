/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite
 * Core definitions and interfaces
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#ifndef __CANON_R5_H__
#define __CANON_R5_H__

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/idr.h>

#define CANON_R5_MODULE_NAME		"canon-r5"
#define CANON_R5_DRIVER_VERSION		"0.1.0"

/* Canon USB VID */
#define CANON_USB_VID			0x04A9

/* Canon R5 USB PIDs - to be determined from actual device */
#define CANON_R5_PID_NORMAL		0x0000  /* Normal mode PID */
#define CANON_R5_PID_PC_CONNECT		0x0001  /* PC connection mode PID */

/* USB interface numbers */
#define CANON_R5_USB_INTF_PTP		0	/* PTP interface */
#define CANON_R5_USB_INTF_STORAGE	1	/* Mass storage interface */

/* USB endpoints */
#define CANON_R5_EP_INT_IN		0x81
#define CANON_R5_EP_BULK_IN		0x82
#define CANON_R5_EP_BULK_OUT		0x03

/* Device capabilities flags */
#define CANON_R5_CAP_VIDEO		BIT(0)
#define CANON_R5_CAP_STILL		BIT(1)
#define CANON_R5_CAP_AUDIO		BIT(2)
#define CANON_R5_CAP_STORAGE		BIT(3)
#define CANON_R5_CAP_WIRELESS		BIT(4)
#define CANON_R5_CAP_GPS		BIT(5)
#define CANON_R5_CAP_TOUCHSCREEN	BIT(6)

/* Forward declarations */
struct canon_r5_device;
struct canon_r5_ptp_command;
struct canon_r5_event;
struct canon_r5_usb;

/* Device state */
enum canon_r5_state {
	CANON_R5_STATE_DISCONNECTED = 0,
	CANON_R5_STATE_CONNECTED,
	CANON_R5_STATE_INITIALIZED,
	CANON_R5_STATE_READY,
	CANON_R5_STATE_ERROR
};

/* Transport layer abstraction */
struct canon_r5_transport_ops {
	int (*bulk_send)(struct canon_r5_device *dev, const void *data, size_t len);
	int (*bulk_receive)(struct canon_r5_device *dev, void *data, size_t len, size_t *actual_len);
};

/* PTP session information */
struct canon_r5_ptp {
	struct mutex		lock;
	u32			session_id;
	u32			transaction_id;
	bool			session_open;
	struct work_struct	event_work;
	struct workqueue_struct	*event_wq;
};

/* USB transport layer - defined in USB module */

/* Event handling */
struct canon_r5_event_handler {
	void (*video_frame_ready)(struct canon_r5_device *dev);
	void (*still_capture_complete)(struct canon_r5_device *dev);
	void (*card_inserted)(struct canon_r5_device *dev, int slot);
	void (*card_removed)(struct canon_r5_device *dev, int slot);
	void (*lens_attached)(struct canon_r5_device *dev);
	void (*lens_detached)(struct canon_r5_device *dev);
	void (*battery_changed)(struct canon_r5_device *dev);
	void (*error_occurred)(struct canon_r5_device *dev, int error_code);
};

/* Main device structure */
struct canon_r5_device {
	struct kref		kref;
	struct device		*dev;
	
	/* USB layer */
	struct canon_r5_usb	*usb;
	
	/* Transport layer */
	struct canon_r5_transport_ops *transport_ops;
	
	/* PTP layer */
	struct canon_r5_ptp	ptp;
	
	/* Device state */
	enum canon_r5_state	state;
	struct mutex		state_lock;
	
	/* Capabilities */
	u32			capabilities;
	
	/* Driver modules */
	void			*video_priv;
	void			*still_priv;
	void			*audio_priv;
	void			*storage_priv;
	void			*control_priv;
	void			*power_priv;
	void			*input_priv;
	void			*lens_priv;
	void			*display_priv;
	void			*wireless_priv;
	
	/* Event handling */
	struct canon_r5_event_handler event_handler;
	
	/* Device identification */
	char			serial_number[32];
	char			firmware_version[16];
	
	/* Runtime data */
	struct idr		transaction_idr;
	spinlock_t		transaction_lock;
	
	/* Sysfs */
	struct kobject		*sysfs_kobj;
};

/* Core API functions */
int canon_r5_core_init(void);
void canon_r5_core_exit(void);

/* Device management */
struct canon_r5_device *canon_r5_device_alloc(struct device *parent);
void canon_r5_device_get(struct canon_r5_device *dev);
void canon_r5_device_put(struct canon_r5_device *dev);
int canon_r5_device_initialize(struct canon_r5_device *dev);
void canon_r5_device_cleanup(struct canon_r5_device *dev);

/* Transport layer management */
int canon_r5_register_transport(struct canon_r5_device *dev, struct canon_r5_transport_ops *ops);
void canon_r5_unregister_transport(struct canon_r5_device *dev);

/* State management */
int canon_r5_set_state(struct canon_r5_device *dev, enum canon_r5_state new_state);
enum canon_r5_state canon_r5_get_state(struct canon_r5_device *dev);

/* Event notification */
void canon_r5_notify_event(struct canon_r5_device *dev, int event_type, void *data);

/* Inter-module communication */
int canon_r5_register_video_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_still_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_audio_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_storage_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_control_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_power_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_input_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_lens_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_display_driver(struct canon_r5_device *dev, void *priv);
int canon_r5_register_wireless_driver(struct canon_r5_device *dev, void *priv);

void canon_r5_unregister_video_driver(struct canon_r5_device *dev);
void canon_r5_unregister_still_driver(struct canon_r5_device *dev);
void canon_r5_unregister_audio_driver(struct canon_r5_device *dev);
void canon_r5_unregister_storage_driver(struct canon_r5_device *dev);
void canon_r5_unregister_control_driver(struct canon_r5_device *dev);
void canon_r5_unregister_power_driver(struct canon_r5_device *dev);
void canon_r5_unregister_input_driver(struct canon_r5_device *dev);
void canon_r5_unregister_lens_driver(struct canon_r5_device *dev);
void canon_r5_unregister_display_driver(struct canon_r5_device *dev);
void canon_r5_unregister_wireless_driver(struct canon_r5_device *dev);

/* Get driver private data */
void *canon_r5_get_video_driver(struct canon_r5_device *dev);
void *canon_r5_get_still_driver(struct canon_r5_device *dev);
void *canon_r5_get_audio_driver(struct canon_r5_device *dev);
void *canon_r5_get_storage_driver(struct canon_r5_device *dev);
void *canon_r5_get_control_driver(struct canon_r5_device *dev);
void *canon_r5_get_power_driver(struct canon_r5_device *dev);
void *canon_r5_get_input_driver(struct canon_r5_device *dev);
void *canon_r5_get_lens_driver(struct canon_r5_device *dev);
void *canon_r5_get_display_driver(struct canon_r5_device *dev);
void *canon_r5_get_wireless_driver(struct canon_r5_device *dev);

/* Debugging */
#define canon_r5_dbg(dev, fmt, ...) \
	dev_dbg((dev)->dev, fmt, ##__VA_ARGS__)

#define canon_r5_info(dev, fmt, ...) \
	dev_info((dev)->dev, fmt, ##__VA_ARGS__)

#define canon_r5_warn(dev, fmt, ...) \
	dev_warn((dev)->dev, fmt, ##__VA_ARGS__)

#define canon_r5_err(dev, fmt, ...) \
	dev_err((dev)->dev, fmt, ##__VA_ARGS__)

#endif /* __CANON_R5_H__ */