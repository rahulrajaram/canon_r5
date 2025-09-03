// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * PTP protocol implementation
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/byteorder/little_endian.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"

MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_DESCRIPTION("Canon R5 Camera Driver Suite - PTP Protocol");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(CANON_R5_DRIVER_VERSION);

/* Forward declarations */
extern int canon_r5_usb_bulk_send(struct canon_r5_device *dev, const void *data, size_t len);
extern int canon_r5_usb_bulk_receive(struct canon_r5_device *dev, void *data, size_t len, size_t *actual_len);

/* Helper function to build PTP container */
static void build_ptp_container(struct ptp_container *container, u16 type, u16 code, 
				u32 trans_id, u32 *params, int param_count)
{
	int i;
	
	container->length = cpu_to_le32(sizeof(*container) - sizeof(container->params) + 
					param_count * sizeof(u32));
	container->type = cpu_to_le16(type);
	container->code = cpu_to_le16(code);
	container->trans_id = cpu_to_le32(trans_id);
	
	for (i = 0; i < param_count && i < 5; i++) {
		container->params[i] = cpu_to_le32(params[i]);
	}
}

/* Send PTP command and receive response */
int canon_r5_ptp_command(struct canon_r5_device *dev, u16 code, 
			 u32 *params, int param_count,
			 void *data, int data_len,
			 u16 *response_code)
{
	struct ptp_container cmd, resp;
	size_t resp_len;
	u32 trans_id;
	int ret;
	
	if (!dev || !response_code)
		return -EINVAL;
	
	mutex_lock(&dev->ptp.lock);
	
	if (!dev->ptp.session_open && code != PTP_OP_OPEN_SESSION) {
		canon_r5_warn(dev, "PTP session not open for command 0x%04x", code);
		mutex_unlock(&dev->ptp.lock);
		return -ENOTCONN;
	}
	
	/* Get next transaction ID */
	trans_id = dev->ptp.transaction_id++;
	
	/* Build command container */
	build_ptp_container(&cmd, PTP_CONTAINER_COMMAND, code, trans_id, params, param_count);
	
	/* Send command */
	ret = canon_r5_usb_bulk_send(dev, &cmd, le32_to_cpu(cmd.length));
	if (ret) {
		canon_r5_err(dev, "Failed to send PTP command 0x%04x: %d", code, ret);
		mutex_unlock(&dev->ptp.lock);
		return ret;
	}
	
	canon_r5_dbg(dev, "Sent PTP command 0x%04x (trans_id: %u)", code, trans_id);
	
	/* Send data phase if provided */
	if (data && data_len > 0) {
		struct ptp_container data_container;
		
		build_ptp_container(&data_container, PTP_CONTAINER_DATA, code, trans_id, NULL, 0);
		data_container.length = cpu_to_le32(sizeof(data_container) + data_len);
		
		ret = canon_r5_usb_bulk_send(dev, &data_container, sizeof(data_container));
		if (ret) {
			canon_r5_err(dev, "Failed to send PTP data header: %d", ret);
			mutex_unlock(&dev->ptp.lock);
			return ret;
		}
		
		ret = canon_r5_usb_bulk_send(dev, data, data_len);
		if (ret) {
			canon_r5_err(dev, "Failed to send PTP data: %d", ret);
			mutex_unlock(&dev->ptp.lock);
			return ret;
		}
		
		canon_r5_dbg(dev, "Sent PTP data phase (%d bytes)", data_len);
	}
	
	/* Receive response */
	ret = canon_r5_usb_bulk_receive(dev, &resp, sizeof(resp), &resp_len);
	if (ret) {
		canon_r5_err(dev, "Failed to receive PTP response: %d", ret);
		mutex_unlock(&dev->ptp.lock);
		return ret;
	}
	
	/* Validate response */
	if (resp_len < sizeof(resp) - sizeof(resp.params)) {
		canon_r5_err(dev, "PTP response too short: %zu bytes", resp_len);
		mutex_unlock(&dev->ptp.lock);
		return -EPROTO;
	}
	
	if (le16_to_cpu(resp.type) != PTP_CONTAINER_RESPONSE) {
		canon_r5_err(dev, "Invalid PTP response type: 0x%04x", le16_to_cpu(resp.type));
		mutex_unlock(&dev->ptp.lock);
		return -EPROTO;
	}
	
	if (le32_to_cpu(resp.trans_id) != trans_id) {
		canon_r5_err(dev, "PTP transaction ID mismatch: expected %u, got %u",
			    trans_id, le32_to_cpu(resp.trans_id));
		mutex_unlock(&dev->ptp.lock);
		return -EPROTO;
	}
	
	*response_code = le16_to_cpu(resp.code);
	
	canon_r5_dbg(dev, "Received PTP response 0x%04x for command 0x%04x", 
		    *response_code, code);
	
	mutex_unlock(&dev->ptp.lock);
	
	return (*response_code == PTP_RC_OK) ? 0 : -EIO;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_command);

/* Open PTP session */
int canon_r5_ptp_open_session(struct canon_r5_device *dev)
{
	u32 session_id = 1;
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Opening PTP session");
	
	ret = canon_r5_ptp_command(dev, PTP_OP_OPEN_SESSION, &session_id, 1, 
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_err(dev, "Failed to open PTP session: %d", ret);
		return ret;
	}
	
	if (response_code != PTP_RC_OK) {
		canon_r5_err(dev, "PTP session open failed: 0x%04x", response_code);
		return -EIO;
	}
	
	mutex_lock(&dev->ptp.lock);
	dev->ptp.session_id = session_id;
	dev->ptp.session_open = true;
	mutex_unlock(&dev->ptp.lock);
	
	canon_r5_info(dev, "PTP session opened successfully (ID: %u)", session_id);
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_open_session);

/* Close PTP session */
int canon_r5_ptp_close_session(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	mutex_lock(&dev->ptp.lock);
	if (!dev->ptp.session_open) {
		mutex_unlock(&dev->ptp.lock);
		return 0;
	}
	mutex_unlock(&dev->ptp.lock);
	
	canon_r5_info(dev, "Closing PTP session");
	
	ret = canon_r5_ptp_command(dev, PTP_OP_CLOSE_SESSION, NULL, 0,
				  NULL, 0, &response_code);
	
	mutex_lock(&dev->ptp.lock);
	dev->ptp.session_open = false;
	dev->ptp.session_id = 0;
	mutex_unlock(&dev->ptp.lock);
	
	if (ret) {
		canon_r5_warn(dev, "Failed to close PTP session: %d", ret);
	} else {
		canon_r5_info(dev, "PTP session closed successfully");
	}
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_close_session);

/* Get device information */
int canon_r5_ptp_get_device_info(struct canon_r5_device *dev, struct ptp_device_info *info)
{
	u16 response_code;
	int ret;
	
	if (!dev || !info)
		return -EINVAL;
	
	canon_r5_dbg(dev, "Getting PTP device info");
	
	ret = canon_r5_ptp_command(dev, PTP_OP_GET_DEVICE_INFO, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_err(dev, "Failed to get device info: %d", ret);
		return ret;
	}
	
	/* TODO: Parse device info data from response */
	/* For now, just initialize with basic values */
	memset(info, 0, sizeof(*info));
	info->standard_version = 0x0100;
	info->vendor_extension_id = CANON_USB_VID;
	
	canon_r5_info(dev, "Device info retrieved successfully");
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_get_device_info);

/* Initiate release control (required for Canon cameras) */
int canon_r5_ptp_initiate_release_control(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Initiating release control");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_INITIATE_RELEASE_CONTROL, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_err(dev, "Failed to initiate release control: %d", ret);
		return ret;
	}
	
	if (response_code != PTP_RC_OK) {
		canon_r5_err(dev, "Release control initiation failed: 0x%04x", response_code);
		return -EIO;
	}
	
	canon_r5_info(dev, "Release control initiated successfully");
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_initiate_release_control);

/* Terminate release control */
int canon_r5_ptp_terminate_release_control(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Terminating release control");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_TERMINATE_RELEASE_CONTROL, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_warn(dev, "Failed to terminate release control: %d", ret);
	} else {
		canon_r5_info(dev, "Release control terminated successfully");
	}
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_terminate_release_control);

/* Start live view */
int canon_r5_ptp_liveview_start(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Starting live view");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_LIVEVIEW_START, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_err(dev, "Failed to start live view: %d", ret);
		return ret;
	}
	
	if (response_code != PTP_RC_OK) {
		canon_r5_err(dev, "Live view start failed: 0x%04x", response_code);
		return -EIO;
	}
	
	canon_r5_info(dev, "Live view started successfully");
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_liveview_start);

/* Stop live view */
int canon_r5_ptp_liveview_stop(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Stopping live view");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_LIVEVIEW_STOP, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_warn(dev, "Failed to stop live view: %d", ret);
	} else {
		canon_r5_info(dev, "Live view stopped successfully");
	}
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_liveview_stop);

/* Get live view frame */
int canon_r5_ptp_get_liveview_frame(struct canon_r5_device *dev,
				   void **frame_data, size_t *frame_size)
{
	u16 response_code;
	int ret;
	
	if (!dev || !frame_data || !frame_size)
		return -EINVAL;
	
	canon_r5_dbg(dev, "Getting live view frame");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_GET_LIVEVIEW, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_dbg(dev, "Failed to get live view frame: %d", ret);
		return ret;
	}
	
	/* TODO: Implement frame data retrieval and parsing */
	/* For now, return no data */
	*frame_data = NULL;
	*frame_size = 0;
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_get_liveview_frame);

/* Capture image */
int canon_r5_ptp_capture_image(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Capturing image");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_CAPTURE, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_err(dev, "Failed to capture image: %d", ret);
		return ret;
	}
	
	if (response_code != PTP_RC_OK) {
		canon_r5_err(dev, "Image capture failed: 0x%04x", response_code);
		return -EIO;
	}
	
	canon_r5_info(dev, "Image captured successfully");
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_capture_image);

/* Start movie recording */
int canon_r5_ptp_start_movie(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Starting movie recording");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_MOVIE_START, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_err(dev, "Failed to start movie recording: %d", ret);
		return ret;
	}
	
	if (response_code != PTP_RC_OK) {
		canon_r5_err(dev, "Movie start failed: 0x%04x", response_code);
		return -EIO;
	}
	
	canon_r5_info(dev, "Movie recording started successfully");
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_start_movie);

/* Stop movie recording */
int canon_r5_ptp_stop_movie(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Stopping movie recording");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_MOVIE_STOP, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_warn(dev, "Failed to stop movie recording: %d", ret);
	} else {
		canon_r5_info(dev, "Movie recording stopped successfully");
	}
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_stop_movie);

/* Get device property */
int canon_r5_ptp_get_property(struct canon_r5_device *dev, u16 property,
			     void *value, size_t value_size)
{
	u32 params[1] = { property };
	u16 response_code;
	int ret;
	
	if (!dev || !value)
		return -EINVAL;
	
	canon_r5_dbg(dev, "Getting device property 0x%04x", property);
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_GET_PROPERTY, params, 1,
				  NULL, 0, &response_code);
	if (ret) {
		canon_r5_dbg(dev, "Failed to get property 0x%04x: %d", property, ret);
		return ret;
	}
	
	/* TODO: Parse property data from response */
	memset(value, 0, value_size);
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_get_property);

/* Set device property */
int canon_r5_ptp_set_property(struct canon_r5_device *dev, u16 property,
			     void *value, size_t value_size)
{
	u32 params[1] = { property };
	u16 response_code;
	int ret;
	
	if (!dev || !value)
		return -EINVAL;
	
	canon_r5_dbg(dev, "Setting device property 0x%04x", property);
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_SET_PROPERTY, params, 1,
				  value, value_size, &response_code);
	if (ret) {
		canon_r5_warn(dev, "Failed to set property 0x%04x: %d", property, ret);
		return ret;
	}
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_set_property);

/* Check for PTP events */
int canon_r5_ptp_check_event(struct canon_r5_device *dev)
{
	/* TODO: Implement event checking from interrupt endpoint data */
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_check_event);

/* PTP event handler work function */
void canon_r5_ptp_event_handler(struct work_struct *work)
{
	struct canon_r5_device *dev = container_of(work, struct canon_r5_device, ptp.event_work);
	
	if (!dev)
		return;
	
	canon_r5_dbg(dev, "Processing PTP events");
	
	/* Check for events */
	canon_r5_ptp_check_event(dev);
	
	/* TODO: Process specific event types and call appropriate handlers */
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_event_handler);

/* Initialize PTP layer */
int canon_r5_ptp_init(struct canon_r5_device *dev)
{
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Initializing PTP layer");
	
	/* PTP layer is already initialized in core device allocation */
	
	canon_r5_info(dev, "PTP layer initialized successfully");
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_init);

/* Cleanup PTP layer */
void canon_r5_ptp_cleanup(struct canon_r5_device *dev)
{
	if (!dev)
		return;
	
	canon_r5_info(dev, "Cleaning up PTP layer");
	
	/* Close PTP session if open */
	canon_r5_ptp_close_session(dev);
	
	canon_r5_info(dev, "PTP layer cleaned up");
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_cleanup);

/* Still Image Capture PTP Functions */

/* Single shot capture */
int canon_r5_ptp_capture_single(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Starting single shot capture");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_CAPTURE, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		dev_err(dev->dev, "Failed to send capture command: %d", ret);
		return ret;
	}
	
	if (response_code != PTP_RC_OK) {
		dev_err(dev->dev, "Capture command failed: 0x%04x", response_code);
		return -EIO;
	}
	
	canon_r5_info(dev, "Single shot capture completed");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_capture_single);

/* Burst capture */
int canon_r5_ptp_capture_burst(struct canon_r5_device *dev, u16 count)
{
	u32 params[1];
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	if (count == 0 || count > 999) {
		dev_err(dev->dev, "Invalid burst count: %u", count);
		return -EINVAL;
	}
	
	canon_r5_info(dev, "Starting burst capture of %u images", count);
	
	params[0] = count;
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_CAPTURE_BURST, params, 1,
				  NULL, 0, &response_code);
	if (ret) {
		dev_err(dev->dev, "Failed to send burst capture command: %d", ret);
		return ret;
	}
	
	if (response_code != PTP_RC_OK) {
		dev_err(dev->dev, "Burst capture command failed: 0x%04x", response_code);
		return -EIO;
	}
	
	canon_r5_info(dev, "Burst capture of %u images started", count);
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_capture_burst);

/* Autofocus operation */
int canon_r5_ptp_autofocus(struct canon_r5_device *dev)
{
	u16 response_code;
	int ret;
	
	if (!dev)
		return -EINVAL;
	
	canon_r5_info(dev, "Starting autofocus operation");
	
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_AUTOFOCUS, NULL, 0,
				  NULL, 0, &response_code);
	if (ret) {
		dev_err(dev->dev, "Failed to send autofocus command: %d", ret);
		return ret;
	}
	
	if (response_code != PTP_RC_OK) {
		dev_warn(dev->dev, "Autofocus operation result: 0x%04x", response_code);
		return (response_code == 0x2019) ? -EAGAIN : -EIO; /* Focus failed vs other error */
	}
	
	canon_r5_info(dev, "Autofocus operation completed successfully");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_autofocus);

/* Remaining PTP still capture functions - stub implementations for now */

int canon_r5_ptp_manual_focus(struct canon_r5_device *dev, u32 position)
{
	if (!dev) return -EINVAL;
	canon_r5_info(dev, "Manual focus stub: position %u", position);
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_manual_focus);

int canon_r5_ptp_get_focus_info(struct canon_r5_device *dev, u32 *position, bool *achieved)
{
	if (!dev || !position || !achieved) return -EINVAL;
	*position = 100; *achieved = true;
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_get_focus_info);

int canon_r5_ptp_set_image_quality(struct canon_r5_device *dev, u32 format, u32 size, u32 quality)
{
	if (!dev) return -EINVAL;
	canon_r5_info(dev, "Set image quality stub: %u/%u/%u", format, size, quality);
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_set_image_quality);

int canon_r5_ptp_get_image_quality(struct canon_r5_device *dev, u32 *format, u32 *size, u32 *quality)
{
	if (!dev || !format || !size || !quality) return -EINVAL;
	*format = 0; *size = 1; *quality = 8;
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_get_image_quality);

int canon_r5_ptp_bulb_start(struct canon_r5_device *dev)
{
	if (!dev) return -EINVAL;
	canon_r5_info(dev, "Bulb start stub");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_bulb_start);

int canon_r5_ptp_bulb_end(struct canon_r5_device *dev)
{
	if (!dev) return -EINVAL;
	canon_r5_info(dev, "Bulb end stub");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_bulb_end);

int canon_r5_ptp_set_bracketing(struct canon_r5_device *dev, u8 shots, s8 step)
{
	if (!dev) return -EINVAL;
	canon_r5_info(dev, "Set bracketing stub: %u shots, %d step", shots, step);
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_set_bracketing);

int canon_r5_ptp_get_battery_info(struct canon_r5_device *dev, u32 *level, u32 *status)
{
	if (!dev || !level || !status) return -EINVAL;
	*level = 85; *status = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_get_battery_info);

int canon_r5_ptp_get_captured_image(struct canon_r5_device *dev, u32 object_id, void **data, size_t *size)
{
	if (!dev || !data || !size) return -EINVAL;
	canon_r5_info(dev, "Get captured image stub: object_id 0x%08x", object_id);
	*data = NULL; *size = 0;
	return -ENODATA;
}
EXPORT_SYMBOL_GPL(canon_r5_ptp_get_captured_image);