// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * Live view implementation and frame processing
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"
#include "../../include/video/canon-r5-v4l2.h"

/* Forward declarations */
extern void canon_r5_video_frame_work(struct work_struct *work);
extern int canon_r5_vb2_queue_init(struct canon_r5_video_device *vdev);
extern void canon_r5_vb2_return_all_buffers(struct canon_r5_video_device *vdev,
					    enum vb2_buffer_state state);

/* Live view frame timer callback */
static void canon_r5_liveview_timer_callback(struct timer_list *t)
{
	struct canon_r5_video *video = container_of(t, struct canon_r5_video, frame_timer);
	struct canon_r5_video_device *vdev = &video->devices[0];  /* Main device */
	
	if (vdev->stream.state == CANON_R5_STREAMING_ACTIVE && vdev->stream.frame_wq) {
		queue_work(vdev->stream.frame_wq, &vdev->stream.frame_work);
		
		/* Schedule next frame */
		mod_timer(&video->frame_timer, jiffies + msecs_to_jiffies(33)); /* ~30fps */
	}
}

/* Start live view */
int canon_r5_video_start_live_view(struct canon_r5_device *canon_dev)
{
	struct canon_r5_video *video = canon_dev->video_priv;
	int ret;
	
	if (!video) {
		dev_err(canon_dev->dev, "Video driver not initialized");
		return -ENODEV;
	}
	
	mutex_lock(&video->live_view_lock);
	
	if (video->live_view_active) {
		dev_info(canon_dev->dev, "Live view already active");
		mutex_unlock(&video->live_view_lock);
		return 0;
	}
	
	dev_info(canon_dev->dev, "Starting Canon R5 live view");
	
	/* Start PTP live view */
	ret = canon_r5_ptp_liveview_start(canon_dev);
	if (ret) {
		dev_err(canon_dev->dev, "Failed to start PTP live view: %d", ret);
		goto unlock;
	}
	
	video->live_view_active = true;
	
	/* Setup frame timer for continuous capture */
	timer_setup(&video->frame_timer, canon_r5_liveview_timer_callback, 0);
	mod_timer(&video->frame_timer, jiffies + msecs_to_jiffies(100)); /* Start after 100ms */
	
	dev_info(canon_dev->dev, "Live view started successfully");
	
unlock:
	mutex_unlock(&video->live_view_lock);
	return ret;
}

/* Stop live view */
int canon_r5_video_stop_live_view(struct canon_r5_device *canon_dev)
{
	struct canon_r5_video *video = canon_dev->video_priv;
	int ret = 0;
	
	if (!video) {
		return 0;
	}
	
	mutex_lock(&video->live_view_lock);
	
	if (!video->live_view_active) {
		mutex_unlock(&video->live_view_lock);
		return 0;
	}
	
	dev_info(canon_dev->dev, "Stopping Canon R5 live view");
	
	/* Stop frame timer */
	del_timer_sync(&video->frame_timer);
	
	/* Stop PTP live view */
	ret = canon_r5_ptp_liveview_stop(canon_dev);
	if (ret) {
		dev_warn(canon_dev->dev, "Failed to stop PTP live view: %d", ret);
	}
	
	video->live_view_active = false;
	
	dev_info(canon_dev->dev, "Live view stopped");
	
	mutex_unlock(&video->live_view_lock);
	return ret;
}

/* Queue frame to video device */
int canon_r5_video_queue_frame(struct canon_r5_video_device *vdev,
			       const void *frame_data, size_t frame_size)
{
	struct canon_r5_video_buffer *buf;
	
	if (vdev->stream.state != CANON_R5_STREAMING_ACTIVE) {
		return -ENODEV;
	}
	
	buf = canon_r5_vb2_get_next_buffer(vdev);
	if (!buf) {
		vdev->stream.dropped_frames++;
		return -ENOBUFS;
	}
	
	if (frame_size > vb2_plane_size(&buf->vb2_buf.vb2_buf, 0)) {
		canon_r5_video_warn(vdev, "Frame too large: %zu > %lu",
				   frame_size, vb2_plane_size(&buf->vb2_buf.vb2_buf, 0));
		frame_size = vb2_plane_size(&buf->vb2_buf.vb2_buf, 0);
	}
	
	/* Copy frame data */
	memcpy(vb2_plane_vaddr(&buf->vb2_buf.vb2_buf, 0), frame_data, frame_size);
	vb2_set_plane_payload(&buf->vb2_buf.vb2_buf, 0, frame_size);
	
	/* Set metadata */
	buf->vb2_buf.vb2_buf.timestamp = ktime_to_ns(ktime_get());
	buf->vb2_buf.sequence = vdev->stream.frame_count++;
	
	/* Submit buffer */
	vb2_buffer_done(&buf->vb2_buf.vb2_buf, VB2_BUF_STATE_DONE);
	
	return 0;
}

/* Frame processing completed callback */
void canon_r5_video_frame_done(struct canon_r5_video_device *vdev)
{
	vdev->stream.last_frame_time = ktime_get();
}

/* Get video statistics */
int canon_r5_video_get_stats(struct canon_r5_video_device *vdev,
			     struct canon_r5_video_stats *stats)
{
	ktime_t now = ktime_get();
	u64 time_diff;
	
	if (!stats)
		return -EINVAL;
	
	stats->frames_captured = vdev->stream.frame_count;
	stats->frames_dropped = vdev->stream.dropped_frames;
	stats->bytes_transferred = vdev->stream.frame_count * vdev->pix_format.sizeimage;
	stats->errors = 0; /* TODO: Track errors */
	stats->last_frame = vdev->stream.last_frame_time;
	
	/* Calculate current FPS */
	time_diff = ktime_to_ns(ktime_sub(now, vdev->stream.last_frame_time));
	if (time_diff > 0) {
		stats->current_fps = (u32)(NSEC_PER_SEC / time_diff);
	} else {
		stats->current_fps = 0;
	}
	
	return 0;
}

/* Enhanced V4L2 device initialization with VB2 */
static int canon_r5_video_init_device_complete(struct canon_r5_video_device *vdev)
{
	struct v4l2_device *v4l2_dev = &vdev->v4l2_dev;
	int ret;
	
	/* Initialize V4L2 device */
	ret = v4l2_device_register(vdev->canon_dev->dev, v4l2_dev);
	if (ret) {
		canon_r5_video_err(vdev, "Failed to register V4L2 device: %d", ret);
		return ret;
	}
	
	snprintf(v4l2_dev->name, sizeof(v4l2_dev->name),
		 "canon-r5-%s", canon_r5_video_type_name(vdev->type));
	
	/* Initialize VB2 queue */
	ret = canon_r5_vb2_queue_init(vdev);
	if (ret) {
		canon_r5_video_err(vdev, "Failed to initialize VB2 queue: %d", ret);
		goto unreg_v4l2;
	}
	
	/* Set parent V4L2 device */
	vdev->vdev.v4l2_dev = v4l2_dev;
	
	canon_r5_video_info(vdev, "Device initialization complete");
	
	return 0;
	
unreg_v4l2:
	v4l2_device_unregister(v4l2_dev);
	return ret;
}

/* Enhanced video driver initialization */
int canon_r5_video_init_enhanced(struct canon_r5_device *canon_dev)
{
	struct canon_r5_video *video;
	int ret;
	int i;
	
	dev_info(canon_dev->dev, "Initializing enhanced V4L2 video driver");
	
	video = kzalloc(sizeof(*video), GFP_KERNEL);
	if (!video)
		return -ENOMEM;
	
	mutex_init(&video->live_view_lock);
	video->live_view_active = false;
	video->num_devices = 1; /* Start with main device */
	
	/* Create frame processing workqueue */
	video->frame_processor_wq = alloc_ordered_workqueue("canon-r5-frame-processor", 
							   WQ_MEM_RECLAIM);
	if (!video->frame_processor_wq) {
		dev_err(canon_dev->dev, "Failed to create frame processor workqueue");
		ret = -ENOMEM;
		goto free_video;
	}
	
	INIT_WORK(&video->frame_processor_work, canon_r5_video_frame_work);
	
	/* Initialize video devices */
	for (i = 0; i < video->num_devices; i++) {
		ret = canon_r5_video_init_device(canon_dev, &video->devices[i], i);
		if (ret) {
			dev_err(canon_dev->dev, "Failed to initialize video device %d: %d", i, ret);
			goto cleanup_devices;
		}
		
		ret = canon_r5_video_init_device_complete(&video->devices[i]);
		if (ret) {
			dev_err(canon_dev->dev, "Failed to complete video device %d init: %d", i, ret);
			goto cleanup_devices;
		}
	}
	
	/* Register with core driver */
	ret = canon_r5_register_video_driver(canon_dev, video);
	if (ret) {
		dev_err(canon_dev->dev, "Failed to register video driver: %d", ret);
		goto cleanup_devices;
	}
	
	dev_info(canon_dev->dev, "Enhanced V4L2 video driver initialized successfully");
	
	return 0;
	
cleanup_devices:
	for (i = 0; i < video->num_devices; i++) {
		struct canon_r5_video_device *vdev = &video->devices[i];
		if (vdev->initialized) {
			v4l2_device_unregister(&vdev->v4l2_dev);
		}
	}
	destroy_workqueue(video->frame_processor_wq);
free_video:
	kfree(video);
	return ret;
}

/* Enhanced cleanup */
void canon_r5_video_cleanup_enhanced(struct canon_r5_device *canon_dev)
{
	struct canon_r5_video *video = canon_dev->video_priv;
	int i;
	
	if (!video)
		return;
	
	dev_info(canon_dev->dev, "Cleaning up enhanced V4L2 video driver");
	
	/* Stop live view */
	canon_r5_video_stop_live_view(canon_dev);
	
	/* Unregister devices */
	canon_r5_video_unregister_devices(canon_dev);
	
	/* Cleanup devices */
	for (i = 0; i < video->num_devices; i++) {
		struct canon_r5_video_device *vdev = &video->devices[i];
		if (vdev->initialized) {
			v4l2_device_unregister(&vdev->v4l2_dev);
		}
	}
	
	/* Cleanup workqueue */
	if (video->frame_processor_wq) {
		cancel_work_sync(&video->frame_processor_work);
		destroy_workqueue(video->frame_processor_wq);
	}
	
	/* Unregister from core */
	canon_r5_unregister_video_driver(canon_dev);
	
	kfree(video);
}