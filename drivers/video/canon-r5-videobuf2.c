// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * Videobuf2 integration for video capture
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"
#include "../../include/video/canon-r5-v4l2.h"

/* VB2 Queue Operations */
static int canon_r5_vb2_queue_setup(struct vb2_queue *vq,
				    unsigned int *nbuffers, unsigned int *nplanes,
				    unsigned int sizes[], struct device *alloc_devs[])
{
	struct canon_r5_video_device *vdev = vb2_get_drv_priv(vq);
	unsigned int size;
	
	if (*nbuffers < 3) {
		*nbuffers = 3;
	}
	
	if (*nbuffers > 8) {
		*nbuffers = 8;
	}
	
	size = vdev->pix_format.sizeimage;
	
	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
		return 0;
	}
	
	*nplanes = 1;
	sizes[0] = size;
	
	canon_r5_video_dbg(vdev, "Queue setup: %d buffers, size %d", *nbuffers, size);
	
	return 0;
}

static int canon_r5_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct canon_r5_video_device *vdev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2 = to_vb2_v4l2_buffer(vb);
	unsigned long size;
	
	size = vdev->pix_format.sizeimage;
	
	if (vb2_plane_size(vb, 0) < size) {
		canon_r5_video_err(vdev, "Buffer too small: %lu < %lu",
				   vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	
	vb2_set_plane_payload(vb, 0, size);
	vb2_v4l2->field = vdev->pix_format.field;
	
	return 0;
}

static void canon_r5_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct canon_r5_video_device *vdev = vb2_get_drv_priv(vb->vb2_queue);
	struct canon_r5_video_buffer *buf = to_canon_r5_video_buffer(to_vb2_v4l2_buffer(vb));
	unsigned long flags;
	
	spin_lock_irqsave(&vdev->stream.buf_lock, flags);
	list_add_tail(&buf->list, &vdev->stream.buf_list);
	spin_unlock_irqrestore(&vdev->stream.buf_lock, flags);
	
	canon_r5_video_dbg(vdev, "Buffer queued");
}

static int canon_r5_vb2_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct canon_r5_video_device *vdev = vb2_get_drv_priv(vq);
	struct canon_r5_device *canon_dev = vdev->canon_dev;
	int ret;
	
	canon_r5_video_info(vdev, "Starting streaming");
	
	vdev->stream.state = CANON_R5_STREAMING_STARTING;
	vdev->stream.frame_count = 0;
	vdev->stream.dropped_frames = 0;
	
	/* Start live view */
	ret = canon_r5_video_start_live_view(canon_dev);
	if (ret) {
		canon_r5_video_err(vdev, "Failed to start live view: %d", ret);
		goto error;
	}
	
	/* Create frame processing workqueue */
	vdev->stream.frame_wq = alloc_ordered_workqueue("canon-r5-frames", WQ_MEM_RECLAIM);
	if (!vdev->stream.frame_wq) {
		canon_r5_video_err(vdev, "Failed to create frame workqueue");
		ret = -ENOMEM;
		goto stop_live_view;
	}
	
	INIT_WORK(&vdev->stream.frame_work, canon_r5_video_frame_work);
	
	vdev->stream.state = CANON_R5_STREAMING_ACTIVE;
	vdev->stream.last_frame_time = ktime_get();
	
	canon_r5_video_info(vdev, "Streaming started successfully");
	
	return 0;
	
stop_live_view:
	canon_r5_video_stop_live_view(canon_dev);
error:
	vdev->stream.state = CANON_R5_STREAMING_STOPPED;
	
	/* Return all buffers with error */
	canon_r5_vb2_return_all_buffers(vdev, VB2_BUF_STATE_QUEUED);
	
	return ret;
}

static void canon_r5_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct canon_r5_video_device *vdev = vb2_get_drv_priv(vq);
	struct canon_r5_device *canon_dev = vdev->canon_dev;
	
	canon_r5_video_info(vdev, "Stopping streaming");
	
	vdev->stream.state = CANON_R5_STREAMING_STOPPING;
	
	/* Cancel any pending frame work */
	if (vdev->stream.frame_wq) {
		cancel_work_sync(&vdev->stream.frame_work);
		destroy_workqueue(vdev->stream.frame_wq);
		vdev->stream.frame_wq = NULL;
	}
	
	/* Stop live view */
	canon_r5_video_stop_live_view(canon_dev);
	
	/* Return all queued buffers */
	canon_r5_vb2_return_all_buffers(vdev, VB2_BUF_STATE_ERROR);
	
	vdev->stream.state = CANON_R5_STREAMING_STOPPED;
	
	canon_r5_video_info(vdev, "Streaming stopped");
}

const struct vb2_ops canon_r5_video_vb2_ops = {
	.queue_setup = canon_r5_vb2_queue_setup,
	.buf_prepare = canon_r5_vb2_buf_prepare,
	.buf_queue = canon_r5_vb2_buf_queue,
	.start_streaming = canon_r5_vb2_start_streaming,
	.stop_streaming = canon_r5_vb2_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

/* Buffer management helpers */
void canon_r5_vb2_return_all_buffers(struct canon_r5_video_device *vdev,
				     enum vb2_buffer_state state)
{
	struct canon_r5_video_buffer *buf, *node;
	unsigned long flags;
	
	spin_lock_irqsave(&vdev->stream.buf_lock, flags);
	list_for_each_entry_safe(buf, node, &vdev->stream.buf_list, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb2_buf.vb2_buf, state);
	}
	spin_unlock_irqrestore(&vdev->stream.buf_lock, flags);
}

struct canon_r5_video_buffer *canon_r5_vb2_get_next_buffer(struct canon_r5_video_device *vdev)
{
	struct canon_r5_video_buffer *buf = NULL;
	unsigned long flags;
	
	spin_lock_irqsave(&vdev->stream.buf_lock, flags);
	if (!list_empty(&vdev->stream.buf_list)) {
		buf = list_first_entry(&vdev->stream.buf_list,
				      struct canon_r5_video_buffer, list);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&vdev->stream.buf_lock, flags);
	
	return buf;
}

/* Frame processing work function */
void canon_r5_video_frame_work(struct work_struct *work)
{
	struct canon_r5_video_device *vdev = container_of(work, 
				struct canon_r5_video_device, stream.frame_work);
	struct canon_r5_device *canon_dev = vdev->canon_dev;
	struct canon_r5_video_buffer *buf;
	void *frame_data;
	size_t frame_size;
	ktime_t now;
	int ret;
	
	if (vdev->stream.state != CANON_R5_STREAMING_ACTIVE) {
		return;
	}
	
	/* Get next available buffer */
	buf = canon_r5_vb2_get_next_buffer(vdev);
	if (!buf) {
		canon_r5_video_dbg(vdev, "No buffer available, dropping frame");
		vdev->stream.dropped_frames++;
		return;
	}
	
	/* Get frame from camera */
	ret = canon_r5_ptp_get_liveview_frame(canon_dev, &frame_data, &frame_size);
	if (ret) {
		canon_r5_video_dbg(vdev, "Failed to get live view frame: %d", ret);
		vdev->stream.dropped_frames++;
		goto requeue_buffer;
	}
	
	if (!frame_data || frame_size == 0) {
		canon_r5_video_dbg(vdev, "Empty frame received");
		vdev->stream.dropped_frames++;
		goto requeue_buffer;
	}
	
	if (frame_size > vb2_plane_size(&buf->vb2_buf.vb2_buf, 0)) {
		canon_r5_video_warn(vdev, "Frame too large: %zu > %lu",
				   frame_size, vb2_plane_size(&buf->vb2_buf.vb2_buf, 0));
		frame_size = vb2_plane_size(&buf->vb2_buf.vb2_buf, 0);
	}
	
	/* Copy frame data to buffer */
	memcpy(vb2_plane_vaddr(&buf->vb2_buf.vb2_buf, 0), frame_data, frame_size);
	vb2_set_plane_payload(&buf->vb2_buf.vb2_buf, 0, frame_size);
	
	/* Set timestamp and sequence */
	now = ktime_get();
	buf->vb2_buf.vb2_buf.timestamp = ktime_to_ns(now);
	buf->vb2_buf.sequence = vdev->stream.frame_count++;
	
	/* Update statistics */
	vdev->stream.last_frame_time = now;
	
	/* Return buffer to userspace */
	vb2_buffer_done(&buf->vb2_buf.vb2_buf, VB2_BUF_STATE_DONE);
	
	canon_r5_video_dbg(vdev, "Frame %llu delivered (%zu bytes)",
			   vdev->stream.frame_count, frame_size);
	
	return;
	
requeue_buffer:
	/* Requeue buffer for next frame */
	{
		unsigned long flags;
		spin_lock_irqsave(&vdev->stream.buf_lock, flags);
		list_add(&buf->list, &vdev->stream.buf_list);
		spin_unlock_irqrestore(&vdev->stream.buf_lock, flags);
	}
}

/* Initialize videobuf2 queue */
int canon_r5_vb2_queue_init(struct canon_r5_video_device *vdev)
{
	struct vb2_queue *q = &vdev->stream.queue;
	int ret;
	
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->drv_priv = vdev;
	q->buf_struct_size = sizeof(struct canon_r5_video_buffer);
	q->ops = &canon_r5_video_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	/* min_buffers_needed field was removed in newer kernels */
	q->lock = &vdev->lock;
	q->dev = vdev->canon_dev->dev;
	
	ret = vb2_queue_init(q);
	if (ret) {
		canon_r5_video_err(vdev, "Failed to initialize VB2 queue: %d", ret);
		return ret;
	}
	
	/* Set queue in video device for VB2 helper functions */
	vdev->vdev.queue = q;
	
	canon_r5_video_info(vdev, "VB2 queue initialized");
	
	return 0;
}