// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * Still image capture driver implementation
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/dma-mapping.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"
#include "../../include/still/canon-r5-still.h"

MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_DESCRIPTION("Canon R5 Camera Driver Suite - Still Image Capture");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(CANON_R5_DRIVER_VERSION);

/* Default image buffer size (50MB for high-res RAW) */
#define CANON_R5_STILL_BUFFER_SIZE	(50 * 1024 * 1024)

/* Helper function implementations */

const char *canon_r5_still_format_name(enum canon_r5_still_format format)
{
	switch (format) {
	case CANON_R5_STILL_JPEG:
		return "JPEG";
	case CANON_R5_STILL_RAW_CR3:
		return "Canon RAW v3";
	case CANON_R5_STILL_RAW_CR2:
		return "Canon RAW v2";
	case CANON_R5_STILL_HEIF:
		return "HEIF";
	default:
		return "Unknown";
	}
}

const char *canon_r5_still_size_name(enum canon_r5_still_size size)
{
	switch (size) {
	case CANON_R5_STILL_SIZE_RAW:
		return "RAW";
	case CANON_R5_STILL_SIZE_LARGE:
		return "Large JPEG";
	case CANON_R5_STILL_SIZE_MEDIUM:
		return "Medium JPEG";
	case CANON_R5_STILL_SIZE_SMALL:
		return "Small JPEG";
	default:
		return "Unknown";
	}
}

const char *canon_r5_capture_mode_name(enum canon_r5_capture_mode mode)
{
	switch (mode) {
	case CANON_R5_CAPTURE_SINGLE:
		return "Single Shot";
	case CANON_R5_CAPTURE_CONTINUOUS:
		return "Continuous";
	case CANON_R5_CAPTURE_TIMER:
		return "Self Timer";
	case CANON_R5_CAPTURE_BULB:
		return "Bulb";
	case CANON_R5_CAPTURE_BRACKET:
		return "Bracketing";
	case CANON_R5_CAPTURE_HDR:
		return "HDR";
	default:
		return "Unknown";
	}
}

const char *canon_r5_focus_mode_name(enum canon_r5_focus_mode mode)
{
	switch (mode) {
	case CANON_R5_FOCUS_MANUAL:
		return "Manual";
	case CANON_R5_FOCUS_SINGLE_AF:
		return "Single AF";
	case CANON_R5_FOCUS_CONTINUOUS_AF:
		return "Continuous AF";
	case CANON_R5_FOCUS_AUTOMATIC:
		return "Automatic";
	default:
		return "Unknown";
	}
}

const char *canon_r5_metering_mode_name(enum canon_r5_metering_mode mode)
{
	switch (mode) {
	case CANON_R5_METERING_EVALUATIVE:
		return "Evaluative";
	case CANON_R5_METERING_PARTIAL:
		return "Partial";
	case CANON_R5_METERING_SPOT:
		return "Spot";
	case CANON_R5_METERING_CENTER_WEIGHTED:
		return "Center Weighted";
	default:
		return "Unknown";
	}
}

/* Validation functions */

bool canon_r5_still_format_valid(enum canon_r5_still_format format)
{
	return format >= 0 && format < CANON_R5_STILL_FORMAT_COUNT;
}

bool canon_r5_still_size_valid(enum canon_r5_still_size size)
{
	return size >= 0 && size < CANON_R5_STILL_SIZE_COUNT;
}

bool canon_r5_capture_mode_valid(enum canon_r5_capture_mode mode)
{
	return mode >= 0 && mode < CANON_R5_CAPTURE_MODE_COUNT;
}

int canon_r5_still_validate_quality(const struct canon_r5_image_quality *quality)
{
	if (!quality)
		return -EINVAL;
	
	if (!canon_r5_still_format_valid(quality->format))
		return -EINVAL;
	
	if (!canon_r5_still_size_valid(quality->size))
		return -EINVAL;
	
	if (quality->format == CANON_R5_STILL_JPEG && 
	    (quality->jpeg_quality < 1 || quality->jpeg_quality > 10))
		return -EINVAL;
	
	return 0;
}

int canon_r5_still_validate_capture_settings(const struct canon_r5_capture_settings *settings)
{
	if (!settings)
		return -EINVAL;
	
	if (!canon_r5_capture_mode_valid(settings->mode))
		return -EINVAL;
	
	if (settings->focus_mode >= CANON_R5_FOCUS_MODE_COUNT)
		return -EINVAL;
	
	if (settings->metering_mode >= CANON_R5_METERING_MODE_COUNT)
		return -EINVAL;
	
	/* Validate ISO range (Canon R5: 50-102400) */
	if (settings->iso < 50 || settings->iso > 102400)
		return -EINVAL;
	
	/* Validate continuous settings */
	if (settings->mode == CANON_R5_CAPTURE_CONTINUOUS) {
		if (settings->continuous_fps < 1 || settings->continuous_fps > 30)
			return -EINVAL;
		if (settings->burst_count < 1 || settings->burst_count > 999)
			return -EINVAL;
	}
	
	/* Validate bracketing settings */
	if (settings->mode == CANON_R5_CAPTURE_BRACKET) {
		if (settings->bracket_shots < 3 || settings->bracket_shots > 9 || 
		    (settings->bracket_shots % 2) == 0)
			return -EINVAL;
		if (settings->bracket_step < -3 || settings->bracket_step > 3 || 
		    settings->bracket_step == 0)
			return -EINVAL;
	}
	
	return 0;
}

/* Memory management */

void *canon_r5_still_alloc_image_buffer(struct canon_r5_still_device *still, size_t size)
{
	struct canon_r5_still *driver_data;
	unsigned long flags;
	void *buffer;
	int bit;
	
	if (!still || !still->canon_dev)
		return NULL;
	
	driver_data = still->canon_dev->still_priv;
	if (!driver_data)
		return NULL;
	
	/* For now, use simple kmalloc - could be optimized with buffer pools */
	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer) {
		canon_r5_still_err(still, "Failed to allocate image buffer (%zu bytes)", size);
		return NULL;
	}
	
	canon_r5_still_dbg(still, "Allocated image buffer: %zu bytes", size);
	return buffer;
}

void canon_r5_still_free_image_buffer(struct canon_r5_still_device *still, void *buffer)
{
	if (buffer) {
		kfree(buffer);
		canon_r5_still_dbg(still, "Freed image buffer");
	}
}

/* Image management */

static struct canon_r5_captured_image *alloc_captured_image(struct canon_r5_still_device *still)
{
	struct canon_r5_captured_image *image;
	
	image = kzalloc(sizeof(*image), GFP_KERNEL);
	if (!image)
		return NULL;
	
	INIT_LIST_HEAD(&image->list);
	init_completion(&image->ready);
	atomic_set(&image->ref_count, 1);
	
	return image;
}

/* Work functions */

void canon_r5_still_capture_work(struct work_struct *work)
{
	struct canon_r5_still_device *still = container_of(work, 
		struct canon_r5_still_device, capture_work);
	struct canon_r5_captured_image *image;
	u32 object_id;
	void *data;
	size_t size;
	int ret;
	
	canon_r5_still_dbg(still, "Processing capture work");
	
	/* Allocate new image structure */
	image = alloc_captured_image(still);
	if (!image) {
		canon_r5_still_err(still, "Failed to allocate captured image");
		goto error;
	}
	
	/* Simulate getting object ID from PTP event - in real implementation
	 * this would come from the camera's ObjectAdded event */
	object_id = 0x12345678; /* Placeholder */
	
	/* Retrieve image data from camera */
	ret = canon_r5_ptp_get_captured_image(still->canon_dev, object_id, &data, &size);
	if (ret) {
		canon_r5_still_err(still, "Failed to retrieve captured image: %d", ret);
		goto error_free_image;
	}
	
	/* Fill in image metadata */
	image->metadata.timestamp = ktime_get_real();
	image->metadata.file_size = size;
	image->metadata.capture_settings = still->settings;
	
	/* Copy image data to our buffer */
	image->data = canon_r5_still_alloc_image_buffer(still, size);
	if (!image->data) {
		canon_r5_still_err(still, "Failed to allocate image data buffer");
		goto error_free_image;
	}
	
	memcpy(image->data, data, size);
	image->data_size = size;
	
	/* Add to captured images list */
	spin_lock(&still->image_list_lock);
	list_add_tail(&image->list, &still->captured_images);
	kfifo_put(&still->image_queue, image);
	spin_unlock(&still->image_list_lock);
	
	/* Signal completion */
	complete(&image->ready);
	wake_up(&still->capture_wait);
	
	/* Update statistics */
	still->stats.images_captured++;
	still->stats.total_bytes += size;
	still->stats.last_capture = ktime_get();
	
	canon_r5_still_info(still, "Captured image: %zu bytes", size);
	return;

error_free_image:
	kfree(image);
error:
	still->stats.images_failed++;
	atomic_dec(&still->pending_captures);
}

void canon_r5_still_continuous_timer(struct timer_list *timer)
{
	struct canon_r5_still_device *still = from_timer(still, timer, continuous_timer);
	int ret;
	
	if (!still->continuous_active)
		return;
	
	/* Trigger next capture */
	ret = canon_r5_ptp_capture_single(still->canon_dev);
	if (ret) {
		canon_r5_still_err(still, "Continuous capture failed: %d", ret);
		still->continuous_active = false;
		return;
	}
	
	still->continuous_count++;
	
	/* Schedule next capture if within burst limit */
	if (still->continuous_count < still->settings.burst_count) {
		unsigned long next_jiffies = jiffies + 
			HZ / still->settings.continuous_fps;
		mod_timer(&still->continuous_timer, next_jiffies);
	} else {
		still->continuous_active = false;
		canon_r5_still_info(still, "Continuous capture completed: %u images", 
				    still->continuous_count);
	}
}

void canon_r5_still_af_work(struct work_struct *work)
{
	struct canon_r5_still_device *still = container_of(work, 
		struct canon_r5_still_device, focus.af_work);
	int ret;
	u32 position;
	bool achieved;
	
	canon_r5_still_dbg(still, "Starting autofocus operation");
	
	mutex_lock(&still->focus.lock);
	still->focus.af_active = true;
	
	ret = canon_r5_ptp_autofocus(still->canon_dev);
	if (ret == 0) {
		/* Get focus result */
		ret = canon_r5_ptp_get_focus_info(still->canon_dev, &position, &achieved);
		if (ret == 0) {
			still->focus.focus_position = position;
			still->focus.focus_achieved = achieved;
			still->stats.af_success += achieved ? 1 : 0;
		}
	}
	
	still->focus.af_active = false;
	still->stats.af_operations++;
	
	mutex_unlock(&still->focus.lock);
	complete(&still->focus.af_complete);
	
	if (ret == 0 && still->focus.focus_achieved) {
		canon_r5_still_info(still, "Autofocus achieved at position %u", 
				    still->focus.focus_position);
	} else {
		canon_r5_still_warn(still, "Autofocus failed or not achieved");
	}
}

/* API Functions */

int canon_r5_still_set_quality(struct canon_r5_still_device *still,
			       const struct canon_r5_image_quality *quality)
{
	int ret;
	
	if (!still || !quality)
		return -EINVAL;
	
	ret = canon_r5_still_validate_quality(quality);
	if (ret)
		return ret;
	
	mutex_lock(&still->lock);
	
	ret = canon_r5_ptp_set_image_quality(still->canon_dev, 
					    quality->format, 
					    quality->size, 
					    quality->jpeg_quality);
	if (ret == 0) {
		still->quality = *quality;
		canon_r5_still_info(still, "Set image quality: %s, %s, Q%u", 
				    canon_r5_still_format_name(quality->format),
				    canon_r5_still_size_name(quality->size),
				    quality->jpeg_quality);
	}
	
	mutex_unlock(&still->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(canon_r5_still_set_quality);

int canon_r5_still_get_quality(struct canon_r5_still_device *still,
			       struct canon_r5_image_quality *quality)
{
	if (!still || !quality)
		return -EINVAL;
	
	mutex_lock(&still->lock);
	*quality = still->quality;
	mutex_unlock(&still->lock);
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_get_quality);

int canon_r5_still_set_capture_settings(struct canon_r5_still_device *still,
					const struct canon_r5_capture_settings *settings)
{
	int ret;
	
	if (!still || !settings)
		return -EINVAL;
	
	ret = canon_r5_still_validate_capture_settings(settings);
	if (ret)
		return ret;
	
	mutex_lock(&still->lock);
	still->settings = *settings;
	
	/* Set bracketing if enabled */
	if (settings->mode == CANON_R5_CAPTURE_BRACKET) {
		ret = canon_r5_ptp_set_bracketing(still->canon_dev, 
						 settings->bracket_shots,
						 settings->bracket_step);
	}
	
	mutex_unlock(&still->lock);
	
	canon_r5_still_info(still, "Set capture settings: %s mode, ISO %u", 
			    canon_r5_capture_mode_name(settings->mode),
			    settings->iso);
	
	return ret;
}
EXPORT_SYMBOL_GPL(canon_r5_still_set_capture_settings);

int canon_r5_still_get_capture_settings(struct canon_r5_still_device *still,
					struct canon_r5_capture_settings *settings)
{
	if (!still || !settings)
		return -EINVAL;
	
	mutex_lock(&still->lock);
	*settings = still->settings;
	mutex_unlock(&still->lock);
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_get_capture_settings);

int canon_r5_still_capture_single(struct canon_r5_still_device *still)
{
	int ret;
	
	if (!still)
		return -EINVAL;
	
	mutex_lock(&still->lock);
	
	if (still->capture_active) {
		mutex_unlock(&still->lock);
		return -EBUSY;
	}
	
	still->capture_active = true;
	atomic_inc(&still->pending_captures);
	
	ret = canon_r5_ptp_capture_single(still->canon_dev);
	if (ret) {
		still->capture_active = false;
		atomic_dec(&still->pending_captures);
		mutex_unlock(&still->lock);
		return ret;
	}
	
	/* Schedule work to process the capture */
	queue_work(still->capture_wq, &still->capture_work);
	
	mutex_unlock(&still->lock);
	
	canon_r5_still_info(still, "Single capture initiated");
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_capture_single);

int canon_r5_still_capture_burst(struct canon_r5_still_device *still, u16 count)
{
	int ret;
	
	if (!still)
		return -EINVAL;
	
	if (count == 0 || count > 999)
		return -EINVAL;
	
	mutex_lock(&still->lock);
	
	if (still->capture_active) {
		mutex_unlock(&still->lock);
		return -EBUSY;
	}
	
	still->capture_active = true;
	atomic_add(count, &still->pending_captures);
	
	ret = canon_r5_ptp_capture_burst(still->canon_dev, count);
	if (ret) {
		still->capture_active = false;
		atomic_sub(count, &still->pending_captures);
		mutex_unlock(&still->lock);
		return ret;
	}
	
	mutex_unlock(&still->lock);
	
	canon_r5_still_info(still, "Burst capture initiated: %u images", count);
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_capture_burst);

int canon_r5_still_start_continuous(struct canon_r5_still_device *still)
{
	int ret;
	unsigned long first_capture;
	
	if (!still)
		return -EINVAL;
	
	mutex_lock(&still->lock);
	
	if (still->continuous_active || still->capture_active) {
		mutex_unlock(&still->lock);
		return -EBUSY;
	}
	
	still->continuous_active = true;
	still->continuous_count = 0;
	still->capture_active = true;
	
	/* Start first capture immediately */
	ret = canon_r5_ptp_capture_single(still->canon_dev);
	if (ret) {
		still->continuous_active = false;
		still->capture_active = false;
		mutex_unlock(&still->lock);
		return ret;
	}
	
	/* Schedule next captures */
	first_capture = jiffies + HZ / still->settings.continuous_fps;
	mod_timer(&still->continuous_timer, first_capture);
	
	mutex_unlock(&still->lock);
	
	canon_r5_still_info(still, "Continuous capture started: %u fps, %u images", 
			    still->settings.continuous_fps, still->settings.burst_count);
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_start_continuous);

int canon_r5_still_stop_continuous(struct canon_r5_still_device *still)
{
	if (!still)
		return -EINVAL;
	
	mutex_lock(&still->lock);
	
	if (!still->continuous_active) {
		mutex_unlock(&still->lock);
		return -EINVAL;
	}
	
	still->continuous_active = false;
	still->capture_active = false;
	del_timer_sync(&still->continuous_timer);
	
	mutex_unlock(&still->lock);
	
	canon_r5_still_info(still, "Continuous capture stopped after %u images", 
			    still->continuous_count);
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_stop_continuous);

struct canon_r5_captured_image *canon_r5_still_get_next_image(struct canon_r5_still_device *still)
{
	struct canon_r5_captured_image *image = NULL;
	
	if (!still)
		return NULL;
	
	spin_lock(&still->image_list_lock);
	
	if (kfifo_get(&still->image_queue, &image)) {
		atomic_inc(&image->ref_count);
	}
	
	spin_unlock(&still->image_list_lock);
	
	return image;
}
EXPORT_SYMBOL_GPL(canon_r5_still_get_next_image);

void canon_r5_still_release_image(struct canon_r5_captured_image *image)
{
	if (!image)
		return;
	
	if (atomic_dec_and_test(&image->ref_count)) {
		if (image->data) {
			kfree(image->data);
		}
		kfree(image);
	}
}
EXPORT_SYMBOL_GPL(canon_r5_still_release_image);

/* Focus operations */

int canon_r5_still_autofocus(struct canon_r5_still_device *still)
{
	if (!still)
		return -EINVAL;
	
	reinit_completion(&still->focus.af_complete);
	queue_work(system_wq, &still->focus.af_work);
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_autofocus);

int canon_r5_still_manual_focus(struct canon_r5_still_device *still, u32 position)
{
	int ret;
	
	if (!still)
		return -EINVAL;
	
	mutex_lock(&still->focus.lock);
	
	ret = canon_r5_ptp_manual_focus(still->canon_dev, position);
	if (ret == 0) {
		still->focus.focus_position = position;
		canon_r5_still_info(still, "Manual focus set to position %u", position);
	}
	
	mutex_unlock(&still->focus.lock);
	
	return ret;
}
EXPORT_SYMBOL_GPL(canon_r5_still_manual_focus);

int canon_r5_still_get_focus_info(struct canon_r5_still_device *still,
				 u32 *position, bool *achieved)
{
	if (!still || !position || !achieved)
		return -EINVAL;
	
	mutex_lock(&still->focus.lock);
	*position = still->focus.focus_position;
	*achieved = still->focus.focus_achieved;
	mutex_unlock(&still->focus.lock);
	
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_get_focus_info);

/* Statistics */

int canon_r5_still_get_stats(struct canon_r5_still_device *still,
			     struct canon_r5_still_stats *stats)
{
	if (!still || !stats)
		return -EINVAL;
	
	*stats = still->stats;
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_still_get_stats);

void canon_r5_still_reset_stats(struct canon_r5_still_device *still)
{
	if (!still)
		return;
	
	memset(&still->stats, 0, sizeof(still->stats));
	canon_r5_still_info(still, "Statistics reset");
}
EXPORT_SYMBOL_GPL(canon_r5_still_reset_stats);

/* Initialization and cleanup */

int canon_r5_still_init(struct canon_r5_device *dev)
{
	struct canon_r5_still *still_priv;
	struct canon_r5_still_device *still;
	int ret;
	
	if (!dev) {
		pr_err("canon-r5-still: Invalid device\n");
		return -EINVAL;
	}
	
	canon_r5_info(dev, "Initializing still image capture driver");
	
	/* Allocate private data */
	still_priv = kzalloc(sizeof(*still_priv), GFP_KERNEL);
	if (!still_priv)
		return -ENOMEM;
	
	still = &still_priv->device;
	still->canon_dev = dev;
	
	/* Initialize device structure */
	mutex_init(&still->lock);
	still->initialized = false;
	still->capture_active = false;
	
	/* Set default image quality */
	still->quality.format = CANON_R5_STILL_JPEG;
	still->quality.size = CANON_R5_STILL_SIZE_LARGE;
	still->quality.jpeg_quality = 8;
	still->quality.raw_plus_jpeg = false;
	
	/* Set default capture settings */
	still->settings.mode = CANON_R5_CAPTURE_SINGLE;
	still->settings.focus_mode = CANON_R5_FOCUS_SINGLE_AF;
	still->settings.metering_mode = CANON_R5_METERING_EVALUATIVE;
	still->settings.iso = 200;
	still->settings.shutter_speed.numerator = 1;
	still->settings.shutter_speed.denominator = 125;
	still->settings.aperture.numerator = 56;  /* f/5.6 */
	still->settings.aperture.denominator = 10;
	still->settings.exposure_compensation = 0;
	still->settings.continuous_fps = 10;
	still->settings.burst_count = 10;
	
	/* Initialize image management */
	INIT_LIST_HEAD(&still->captured_images);
	spin_lock_init(&still->image_list_lock);
	INIT_KFIFO(still->image_queue);
	init_waitqueue_head(&still->capture_wait);
	atomic_set(&still->pending_captures, 0);
	
	/* Initialize work structures */
	INIT_WORK(&still->capture_work, canon_r5_still_capture_work);
	still->capture_wq = create_singlethread_workqueue("canon-r5-still-capture");
	if (!still->capture_wq) {
		ret = -ENOMEM;
		goto error_free;
	}
	
	/* Initialize continuous timer */
	timer_setup(&still->continuous_timer, canon_r5_still_continuous_timer, 0);
	still->continuous_active = false;
	
	/* Initialize focus system */
	mutex_init(&still->focus.lock);
	still->focus.af_active = false;
	init_completion(&still->focus.af_complete);
	INIT_WORK(&still->focus.af_work, canon_r5_still_af_work);
	
	/* Register with core driver */
	ret = canon_r5_register_still_driver(dev, still_priv);
	if (ret) {
		canon_r5_err(dev, "Failed to register still driver: %d", ret);
		goto error_cleanup;
	}
	
	still->initialized = true;
	canon_r5_info(dev, "Still image capture driver initialized successfully");
	
	return 0;

error_cleanup:
	destroy_workqueue(still->capture_wq);
error_free:
	kfree(still_priv);
	return ret;
}
EXPORT_SYMBOL_GPL(canon_r5_still_init);

void canon_r5_still_cleanup(struct canon_r5_device *dev)
{
	struct canon_r5_still *still_priv;
	struct canon_r5_still_device *still;
	struct canon_r5_captured_image *image, *tmp;
	
	if (!dev)
		return;
	
	canon_r5_info(dev, "Cleaning up still image capture driver");
	
	still_priv = dev->still_priv;
	if (!still_priv)
		return;
	
	still = &still_priv->device;
	
	/* Stop any ongoing captures */
	if (still->continuous_active) {
		canon_r5_still_stop_continuous(still);
	}
	
	/* Cancel work and destroy workqueue */
	cancel_work_sync(&still->capture_work);
	destroy_workqueue(still->capture_wq);
	
	/* Free all captured images */
	spin_lock(&still->image_list_lock);
	list_for_each_entry_safe(image, tmp, &still->captured_images, list) {
		list_del(&image->list);
		if (image->data) {
			canon_r5_still_free_image_buffer(still, image->data);
		}
		kfree(image);
	}
	spin_unlock(&still->image_list_lock);
	
	/* Unregister from core driver */
	canon_r5_unregister_still_driver(dev);
	
	/* Free private data */
	kfree(still_priv);
	
	canon_r5_info(dev, "Still image capture driver cleaned up");
}
EXPORT_SYMBOL_GPL(canon_r5_still_cleanup);