/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite
 * Still image capture driver definitions
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#ifndef __CANON_R5_STILL_H__
#define __CANON_R5_STILL_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>

/* Forward declarations */
struct canon_r5_device;
struct canon_r5_still_device;

/* Maximum number of images in capture buffer */
#define CANON_R5_STILL_MAX_IMAGES	64

/* Still image formats */
enum canon_r5_still_format {
	CANON_R5_STILL_JPEG = 0,	/* JPEG compression */
	CANON_R5_STILL_RAW_CR3,		/* Canon RAW v3 */
	CANON_R5_STILL_RAW_CR2,		/* Canon RAW v2 (compatibility) */
	CANON_R5_STILL_HEIF,		/* HEIF format */
	CANON_R5_STILL_FORMAT_COUNT
};

/* Still image sizes */
enum canon_r5_still_size {
	CANON_R5_STILL_SIZE_RAW = 0,	/* Full resolution RAW */
	CANON_R5_STILL_SIZE_LARGE,	/* Large JPEG (full res) */
	CANON_R5_STILL_SIZE_MEDIUM,	/* Medium JPEG */
	CANON_R5_STILL_SIZE_SMALL,	/* Small JPEG */
	CANON_R5_STILL_SIZE_COUNT
};

/* Capture modes */
enum canon_r5_capture_mode {
	CANON_R5_CAPTURE_SINGLE = 0,	/* Single shot */
	CANON_R5_CAPTURE_CONTINUOUS,	/* Continuous shooting */
	CANON_R5_CAPTURE_TIMER,		/* Self timer */
	CANON_R5_CAPTURE_BULB,		/* Bulb mode */
	CANON_R5_CAPTURE_BRACKET,	/* Exposure bracketing */
	CANON_R5_CAPTURE_HDR,		/* HDR capture */
	CANON_R5_CAPTURE_MODE_COUNT
};

/* Focus modes */
enum canon_r5_focus_mode {
	CANON_R5_FOCUS_MANUAL = 0,
	CANON_R5_FOCUS_SINGLE_AF,
	CANON_R5_FOCUS_CONTINUOUS_AF,
	CANON_R5_FOCUS_AUTOMATIC,
	CANON_R5_FOCUS_MODE_COUNT
};

/* Metering modes */
enum canon_r5_metering_mode {
	CANON_R5_METERING_EVALUATIVE = 0,
	CANON_R5_METERING_PARTIAL,
	CANON_R5_METERING_SPOT,
	CANON_R5_METERING_CENTER_WEIGHTED,
	CANON_R5_METERING_MODE_COUNT
};

/* Image quality settings */
struct canon_r5_image_quality {
	enum canon_r5_still_format format;
	enum canon_r5_still_size size;
	u8 jpeg_quality;		/* 1-10 for JPEG */
	bool raw_plus_jpeg;		/* Dual format recording */
};

/* Capture settings */
struct canon_r5_capture_settings {
	enum canon_r5_capture_mode mode;
	enum canon_r5_focus_mode focus_mode;
	enum canon_r5_metering_mode metering_mode;
	
	/* Exposure settings */
	u32 iso;			/* ISO sensitivity */
	struct {
		u32 numerator;
		u32 denominator;
	} shutter_speed;
	
	struct {
		u32 numerator;
		u32 denominator;
	} aperture;			/* f-stop */
	
	s32 exposure_compensation;	/* EV compensation in 1/3 stops */
	
	/* Continuous shooting settings */
	u8 continuous_fps;		/* Frames per second */
	u16 burst_count;		/* Number of images in burst */
	
	/* Bracketing settings */
	u8 bracket_shots;		/* Number of bracketed shots */
	s8 bracket_step;		/* EV step between shots */
	
	/* Timer settings */
	u8 timer_delay;			/* Self timer delay in seconds */
};

/* Captured image metadata */
struct canon_r5_image_metadata {
	u64 timestamp;			/* Capture timestamp */
	u32 image_number;		/* Sequential image number */
	size_t file_size;		/* Image file size */
	
	/* EXIF-like data */
	struct canon_r5_capture_settings capture_settings;
	
	/* Camera state at capture */
	u32 battery_level;		/* Battery percentage */
	u32 card_free_space;		/* Free space on card (MB) */
	s16 camera_temperature;		/* Camera temperature (°C) */
	
	/* Image processing info */
	bool image_stabilization;
	bool flash_fired;
	u8 white_balance;
	u8 color_space;
};

/* Captured image data */
struct canon_r5_captured_image {
	struct list_head list;
	struct canon_r5_image_metadata metadata;
	
	void *data;			/* Image data buffer */
	size_t data_size;		/* Size of image data */
	dma_addr_t dma_addr;		/* DMA address if applicable */
	
	struct completion ready;	/* Image ready for retrieval */
	atomic_t ref_count;		/* Reference counting */
};

/* Still image capture statistics */
struct canon_r5_still_stats {
	u64 images_captured;
	u64 images_failed;
	u64 total_bytes;
	u64 af_operations;
	u64 af_success;
	u32 average_focus_time_ms;
	u32 average_capture_time_ms;
	ktime_t last_capture;
};

/* Still image device */
struct canon_r5_still_device {
	struct canon_r5_device *canon_dev;
	
	/* Device state */
	struct mutex lock;
	bool initialized;
	bool capture_active;
	
	/* Image quality and capture settings */
	struct canon_r5_image_quality quality;
	struct canon_r5_capture_settings settings;
	
	/* Capture buffer management */
	struct list_head captured_images;
	spinlock_t image_list_lock;
	DECLARE_KFIFO(image_queue, struct canon_r5_captured_image *, CANON_R5_STILL_MAX_IMAGES);
	
	wait_queue_head_t capture_wait;
	atomic_t pending_captures;
	
	/* Capture processing */
	struct work_struct capture_work;
	struct workqueue_struct *capture_wq;
	
	/* Continuous shooting */
	struct timer_list continuous_timer;
	bool continuous_active;
	u32 continuous_count;
	
	/* Statistics */
	struct canon_r5_still_stats stats;
	
	/* Focus system */
	struct {
		struct mutex lock;
		bool af_active;
		struct completion af_complete;
		struct work_struct af_work;
		u32 focus_position;
		bool focus_achieved;
	} focus;
};

/* Still image driver private data */
struct canon_r5_still {
	struct canon_r5_still_device device;
	
	/* Memory management */
	struct {
		void *buffer_pool;
		size_t buffer_size;
		unsigned long *bitmap;
		spinlock_t lock;
	} memory;
};

/* API functions */
int canon_r5_still_init(struct canon_r5_device *dev);
void canon_r5_still_cleanup(struct canon_r5_device *dev);

/* Image quality and settings */
int canon_r5_still_set_quality(struct canon_r5_still_device *still,
			       const struct canon_r5_image_quality *quality);
int canon_r5_still_get_quality(struct canon_r5_still_device *still,
			       struct canon_r5_image_quality *quality);

int canon_r5_still_set_capture_settings(struct canon_r5_still_device *still,
					const struct canon_r5_capture_settings *settings);
int canon_r5_still_get_capture_settings(struct canon_r5_still_device *still,
					struct canon_r5_capture_settings *settings);

/* Capture operations */
int canon_r5_still_capture_single(struct canon_r5_still_device *still);
int canon_r5_still_capture_burst(struct canon_r5_still_device *still, u16 count);
int canon_r5_still_start_continuous(struct canon_r5_still_device *still);
int canon_r5_still_stop_continuous(struct canon_r5_still_device *still);

/* Image retrieval */
struct canon_r5_captured_image *canon_r5_still_get_next_image(struct canon_r5_still_device *still);
void canon_r5_still_release_image(struct canon_r5_captured_image *image);

/* Focus control */
int canon_r5_still_autofocus(struct canon_r5_still_device *still);
int canon_r5_still_manual_focus(struct canon_r5_still_device *still, u32 position);
int canon_r5_still_get_focus_info(struct canon_r5_still_device *still,
				 u32 *position, bool *achieved);

/* Statistics */
int canon_r5_still_get_stats(struct canon_r5_still_device *still,
			     struct canon_r5_still_stats *stats);
void canon_r5_still_reset_stats(struct canon_r5_still_device *still);

/* Internal functions */
void canon_r5_still_capture_work(struct work_struct *work);
void canon_r5_still_continuous_timer(struct timer_list *timer);
void canon_r5_still_af_work(struct work_struct *work);

/* Memory management */
void *canon_r5_still_alloc_image_buffer(struct canon_r5_still_device *still, size_t size);
void canon_r5_still_free_image_buffer(struct canon_r5_still_device *still, void *buffer);

/* Debugging */
#define canon_r5_still_dbg(still, fmt, ...) \
	dev_dbg((still)->canon_dev->dev, "[STILL] " fmt, ##__VA_ARGS__)

#define canon_r5_still_info(still, fmt, ...) \
	dev_info((still)->canon_dev->dev, "[STILL] " fmt, ##__VA_ARGS__)

#define canon_r5_still_warn(still, fmt, ...) \
	dev_warn((still)->canon_dev->dev, "[STILL] " fmt, ##__VA_ARGS__)

#define canon_r5_still_err(still, fmt, ...) \
	dev_err((still)->canon_dev->dev, "[STILL] " fmt, ##__VA_ARGS__)

/* Helper functions */
const char *canon_r5_still_format_name(enum canon_r5_still_format format);
const char *canon_r5_still_size_name(enum canon_r5_still_size size);
const char *canon_r5_capture_mode_name(enum canon_r5_capture_mode mode);
const char *canon_r5_focus_mode_name(enum canon_r5_focus_mode mode);
const char *canon_r5_metering_mode_name(enum canon_r5_metering_mode mode);

/* Format validation */
bool canon_r5_still_format_valid(enum canon_r5_still_format format);
bool canon_r5_still_size_valid(enum canon_r5_still_size size);
bool canon_r5_capture_mode_valid(enum canon_r5_capture_mode mode);

/* Settings validation */
int canon_r5_still_validate_quality(const struct canon_r5_image_quality *quality);
int canon_r5_still_validate_capture_settings(const struct canon_r5_capture_settings *settings);

#endif /* __CANON_R5_STILL_H__ */