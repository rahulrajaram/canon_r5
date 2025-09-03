/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite
 * V4L2 video capture driver definitions
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#ifndef __CANON_R5_V4L2_H__
#define __CANON_R5_V4L2_H__

#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>

/* Forward declarations */
struct canon_r5_device;

/* Maximum number of video devices per Canon R5 */
#define CANON_R5_MAX_VIDEO_DEVICES	3

/* Video device types */
enum canon_r5_video_type {
	CANON_R5_VIDEO_MAIN = 0,	/* Main sensor output /dev/video0 */
	CANON_R5_VIDEO_PREVIEW,		/* Viewfinder/preview /dev/video1 */
	CANON_R5_VIDEO_ENCODER,		/* Hardware encoder /dev/video2 */
};

/* Supported video formats */
struct canon_r5_video_format {
	u32		fourcc;		/* V4L2 pixel format */
	u32		depth;		/* Bits per pixel */
	u32		bytesperline_align;	/* Alignment for bytesperline */
	const char	*name;		/* Format name */
	bool		compressed;	/* Is compressed format */
};

/* Video resolution */
struct canon_r5_video_resolution {
	u32	width;
	u32	height;
	u32	fps_num;	/* Frame rate numerator */
	u32	fps_den;	/* Frame rate denominator */
	const char *name;	/* Resolution name */
};

/* Video streaming state */
enum canon_r5_streaming_state {
	CANON_R5_STREAMING_STOPPED = 0,
	CANON_R5_STREAMING_STARTING,
	CANON_R5_STREAMING_ACTIVE,
	CANON_R5_STREAMING_STOPPING,
};

/* Video buffer */
struct canon_r5_video_buffer {
	struct vb2_v4l2_buffer	vb2_buf;
	struct list_head	list;
	dma_addr_t		dma_addr;
	void			*vaddr;
	size_t			size;
};

/* Video streaming context */
struct canon_r5_video_stream {
	struct vb2_queue		queue;
	struct list_head		buf_list;
	spinlock_t			buf_lock;
	
	/* Current streaming parameters */
	struct canon_r5_video_format	*format;
	struct canon_r5_video_resolution *resolution;
	enum canon_r5_streaming_state	state;
	
	/* Frame handling */
	struct work_struct		frame_work;
	struct workqueue_struct		*frame_wq;
	
	/* Statistics */
	u64				frame_count;
	u64				dropped_frames;
	ktime_t				last_frame_time;
};

/* Video device context */
struct canon_r5_video_device {
	struct video_device		vdev;
	struct v4l2_device		v4l2_dev;
	struct v4l2_ctrl_handler	ctrl_handler;
	
	/* Parent device */
	struct canon_r5_device		*canon_dev;
	enum canon_r5_video_type	type;
	
	/* Device state */
	struct mutex			lock;
	atomic_t			open_count;
	bool				initialized;
	
	/* Streaming */
	struct canon_r5_video_stream	stream;
	
	/* Supported formats and resolutions */
	const struct canon_r5_video_format	*formats;
	int					num_formats;
	const struct canon_r5_video_resolution	*resolutions;
	int					num_resolutions;
	
	/* Current settings */
	struct v4l2_pix_format		pix_format;
	struct v4l2_fract		frame_interval;
};

/* Video driver private data */
struct canon_r5_video {
	struct canon_r5_video_device	devices[CANON_R5_MAX_VIDEO_DEVICES];
	int				num_devices;
	
	/* Live view state */
	bool				live_view_active;
	struct mutex			live_view_lock;
	
	/* Frame processing */
	struct work_struct		frame_processor_work;
	struct workqueue_struct		*frame_processor_wq;
	struct timer_list		frame_timer;
};

/* Format definitions */
extern const struct canon_r5_video_format canon_r5_video_formats[];
extern const int canon_r5_video_num_formats;

/* Resolution definitions */
extern const struct canon_r5_video_resolution canon_r5_video_resolutions[];
extern const int canon_r5_video_num_resolutions;

/* V4L2 driver API */
int canon_r5_video_init(struct canon_r5_device *dev);
void canon_r5_video_cleanup(struct canon_r5_device *dev);

int canon_r5_video_register_devices(struct canon_r5_device *dev);
void canon_r5_video_unregister_devices(struct canon_r5_device *dev);

/* Enhanced initialization */
int canon_r5_video_init_enhanced(struct canon_r5_device *canon_dev);
void canon_r5_video_cleanup_enhanced(struct canon_r5_device *canon_dev);
int canon_r5_video_init_device(struct canon_r5_device *canon_dev,
			       struct canon_r5_video_device *vdev,
			       enum canon_r5_video_type type);

/* Frame handling */
int canon_r5_video_queue_frame(struct canon_r5_video_device *vdev, 
			       const void *frame_data, size_t frame_size);
void canon_r5_video_frame_done(struct canon_r5_video_device *vdev);
void canon_r5_video_frame_work(struct work_struct *work);

/* Live view control */
int canon_r5_video_start_live_view(struct canon_r5_device *dev);
int canon_r5_video_stop_live_view(struct canon_r5_device *dev);

/* VB2 functions */
int canon_r5_vb2_queue_init(struct canon_r5_video_device *vdev);
void canon_r5_vb2_return_all_buffers(struct canon_r5_video_device *vdev,
				     enum vb2_buffer_state state);
struct canon_r5_video_buffer *canon_r5_vb2_get_next_buffer(struct canon_r5_video_device *vdev);

/* V4L2 operations */
extern const struct v4l2_file_operations canon_r5_video_fops;
extern const struct v4l2_ioctl_ops canon_r5_video_ioctl_ops;
extern const struct vb2_ops canon_r5_video_vb2_ops;

/* Helper functions */
struct canon_r5_video_format *canon_r5_video_find_format(u32 fourcc);
struct canon_r5_video_resolution *canon_r5_video_find_resolution(u32 width, u32 height);
const char *canon_r5_video_type_name(enum canon_r5_video_type type);

/* Buffer helpers */
static inline struct canon_r5_video_buffer *
to_canon_r5_video_buffer(struct vb2_v4l2_buffer *vb2_buf)
{
	return container_of(vb2_buf, struct canon_r5_video_buffer, vb2_buf);
}

static inline struct canon_r5_video_device *
video_drvdata_to_canon_r5_video_device(struct file *file)
{
	return video_drvdata(file);
}

/* Controls */
enum canon_r5_video_controls {
	CANON_R5_CID_LIVE_VIEW_MODE = V4L2_CID_PRIVATE_BASE,
	CANON_R5_CID_FRAME_RATE,
	CANON_R5_CID_HDR_MODE,
	CANON_R5_CID_STABILIZATION,
	CANON_R5_CID_FOCUS_PEAKING,
	CANON_R5_CID_ZEBRA_PATTERN,
	CANON_R5_CID_HISTOGRAM,
};

/* Control definitions */
struct canon_r5_video_ctrl {
	struct v4l2_ctrl_config	config;
	int			(*set_ctrl)(struct canon_r5_video_device *vdev, 
					    struct v4l2_ctrl *ctrl);
	int			(*get_ctrl)(struct canon_r5_video_device *vdev, 
					    struct v4l2_ctrl *ctrl);
};

extern const struct canon_r5_video_ctrl canon_r5_video_controls[];
extern const int canon_r5_video_num_controls;

/* Statistics and debugging */
struct canon_r5_video_stats {
	u64	frames_captured;
	u64	frames_dropped;
	u64	bytes_transferred;
	u64	errors;
	u32	current_fps;
	ktime_t	last_frame;
};

int canon_r5_video_get_stats(struct canon_r5_video_device *vdev,
			     struct canon_r5_video_stats *stats);

/* Debugging */
#define canon_r5_video_dbg(vdev, fmt, ...) \
	dev_dbg((vdev)->canon_dev->dev, "[VIDEO:%s] " fmt, \
		    canon_r5_video_type_name((vdev)->type), ##__VA_ARGS__)

#define canon_r5_video_info(vdev, fmt, ...) \
	dev_info((vdev)->canon_dev->dev, "[VIDEO:%s] " fmt, \
		     canon_r5_video_type_name((vdev)->type), ##__VA_ARGS__)

#define canon_r5_video_warn(vdev, fmt, ...) \
	dev_warn((vdev)->canon_dev->dev, "[VIDEO:%s] " fmt, \
		     canon_r5_video_type_name((vdev)->type), ##__VA_ARGS__)

#define canon_r5_video_err(vdev, fmt, ...) \
	dev_err((vdev)->canon_dev->dev, "[VIDEO:%s] " fmt, \
		    canon_r5_video_type_name((vdev)->type), ##__VA_ARGS__)

#endif /* __CANON_R5_V4L2_H__ */