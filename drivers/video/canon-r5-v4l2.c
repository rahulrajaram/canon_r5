// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * V4L2 video capture driver implementation
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"
#include "../../include/video/canon-r5-v4l2.h"

MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_DESCRIPTION("Canon R5 Camera Driver Suite - V4L2 Video Capture");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(CANON_R5_DRIVER_VERSION);

/* Supported video formats */
const struct canon_r5_video_format canon_r5_video_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_MJPEG,
		.depth = 16,
		.bytesperline_align = 16,
		.name = "Motion-JPEG",
		.compressed = true,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.depth = 16,
		.bytesperline_align = 16,
		.name = "YUYV 4:2:2",
		.compressed = false,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.depth = 12,
		.bytesperline_align = 16,
		.name = "Y/CbCr 4:2:0",
		.compressed = false,
	},
};
const int canon_r5_video_num_formats = ARRAY_SIZE(canon_r5_video_formats);

/* Supported resolutions */
const struct canon_r5_video_resolution canon_r5_video_resolutions[] = {
	{ 8192, 5464, 30, 1, "8K RAW" },
	{ 7680, 4320, 30, 1, "8K UHD" },
	{ 4096, 2160, 60, 1, "4K Cinema" },
	{ 3840, 2160, 60, 1, "4K UHD" },
	{ 1920, 1080, 120, 1, "Full HD 120p" },
	{ 1920, 1080, 60, 1, "Full HD 60p" },
	{ 1920, 1080, 30, 1, "Full HD 30p" },
	{ 1280, 720, 120, 1, "HD 120p" },
	{ 1280, 720, 60, 1, "HD 60p" },
	{ 640, 480, 30, 1, "VGA" },
};
const int canon_r5_video_num_resolutions = ARRAY_SIZE(canon_r5_video_resolutions);

/* Helper functions */
struct canon_r5_video_format *canon_r5_video_find_format(u32 fourcc)
{
	int i;
	
	for (i = 0; i < canon_r5_video_num_formats; i++) {
		if (canon_r5_video_formats[i].fourcc == fourcc)
			return (struct canon_r5_video_format *)&canon_r5_video_formats[i];
	}
	
	return NULL;
}

struct canon_r5_video_resolution *canon_r5_video_find_resolution(u32 width, u32 height)
{
	int i;
	
	for (i = 0; i < canon_r5_video_num_resolutions; i++) {
		if (canon_r5_video_resolutions[i].width == width &&
		    canon_r5_video_resolutions[i].height == height)
			return (struct canon_r5_video_resolution *)&canon_r5_video_resolutions[i];
	}
	
	return NULL;
}

const char *canon_r5_video_type_name(enum canon_r5_video_type type)
{
	switch (type) {
	case CANON_R5_VIDEO_MAIN:
		return "MAIN";
	case CANON_R5_VIDEO_PREVIEW:
		return "PREVIEW";
	case CANON_R5_VIDEO_ENCODER:
		return "ENCODER";
	default:
		return "UNKNOWN";
	}
}

/* V4L2 File Operations */
static int canon_r5_video_open(struct file *file)
{
	struct canon_r5_video_device *vdev = video_drvdata(file);
	struct canon_r5_device *canon_dev = vdev->canon_dev;
	int ret;
	
	canon_r5_video_info(vdev, "Device opened");
	
	mutex_lock(&vdev->lock);
	
	if (!vdev->initialized) {
		canon_r5_video_err(vdev, "Device not initialized");
		ret = -ENODEV;
		goto unlock;
	}
	
	ret = v4l2_fh_open(file);
	if (ret) {
		canon_r5_video_err(vdev, "Failed to open v4l2 file handle: %d", ret);
		goto unlock;
	}
	
	/* Initialize streaming on first open */
	if (atomic_inc_return(&vdev->open_count) == 1) {
		/* Open PTP session if not already open */
		if (!canon_dev->ptp.session_open) {
			ret = canon_r5_ptp_open_session(canon_dev);
			if (ret) {
				canon_r5_video_err(vdev, "Failed to open PTP session: %d", ret);
				goto dec_count;
			}
		}
		
		/* Initialize release control */
		ret = canon_r5_ptp_initiate_release_control(canon_dev);
		if (ret) {
			canon_r5_video_warn(vdev, "Failed to initiate release control: %d", ret);
			/* Continue anyway - not critical for live view */
		}
	}
	
	mutex_unlock(&vdev->lock);
	return 0;
	
dec_count:
	atomic_dec(&vdev->open_count);
	v4l2_fh_release(file);
unlock:
	mutex_unlock(&vdev->lock);
	return ret;
}

static int canon_r5_video_release(struct file *file)
{
	struct canon_r5_video_device *vdev = video_drvdata(file);
	struct canon_r5_device *canon_dev = vdev->canon_dev;
	
	canon_r5_video_info(vdev, "Device released");
	
	mutex_lock(&vdev->lock);
	
	if (atomic_dec_and_test(&vdev->open_count)) {
		/* Stop streaming if active */
		if (vdev->stream.state != CANON_R5_STREAMING_STOPPED) {
			canon_r5_video_stop_live_view(canon_dev);
		}
		
		/* Terminate release control */
		canon_r5_ptp_terminate_release_control(canon_dev);
	}
	
	mutex_unlock(&vdev->lock);
	
	return v4l2_fh_release(file);
}

const struct v4l2_file_operations canon_r5_video_fops = {
	.owner = THIS_MODULE,
	.open = canon_r5_video_open,
	.release = canon_r5_video_release,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
};

/* V4L2 IOCTL Operations */
static int canon_r5_video_querycap(struct file *file, void *priv,
				   struct v4l2_capability *cap)
{
	struct canon_r5_video_device *vdev = video_drvdata(file);
	
	strscpy(cap->driver, CANON_R5_MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, "Canon R5 Camera", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "usb-%s",
		 dev_name(vdev->canon_dev->dev));
	
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			   V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	
	return 0;
}

static int canon_r5_video_enum_fmt_vid_cap(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	if (f->index >= canon_r5_video_num_formats)
		return -EINVAL;
	
	f->flags = canon_r5_video_formats[f->index].compressed ? 
		   V4L2_FMT_FLAG_COMPRESSED : 0;
	f->pixelformat = canon_r5_video_formats[f->index].fourcc;
	strscpy(f->description, canon_r5_video_formats[f->index].name,
		sizeof(f->description));
	
	return 0;
}

static int canon_r5_video_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct canon_r5_video_device *vdev = video_drvdata(file);
	
	f->fmt.pix = vdev->pix_format;
	
	return 0;
}

static int canon_r5_video_try_fmt_vid_cap(struct file *file, void *priv,
					  struct v4l2_format *f)
{
	struct canon_r5_video_format *fmt;
	struct canon_r5_video_resolution *res;
	
	/* Find supported format */
	fmt = canon_r5_video_find_format(f->fmt.pix.pixelformat);
	if (!fmt) {
		/* Default to MJPEG */
		fmt = (struct canon_r5_video_format *)&canon_r5_video_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}
	
	/* Find supported resolution */
	res = canon_r5_video_find_resolution(f->fmt.pix.width, f->fmt.pix.height);
	if (!res) {
		/* Default to 1080p */
		res = canon_r5_video_find_resolution(1920, 1080);
		if (!res) {
			/* Fallback to first available resolution */
			res = (struct canon_r5_video_resolution *)&canon_r5_video_resolutions[0];
		}
		f->fmt.pix.width = res->width;
		f->fmt.pix.height = res->height;
	}
	
	/* Calculate image size */
	if (fmt->compressed) {
		/* For compressed formats, estimate size */
		f->fmt.pix.sizeimage = (f->fmt.pix.width * f->fmt.pix.height * fmt->depth) / 8;
		f->fmt.pix.bytesperline = 0;
	} else {
		/* For uncompressed formats, calculate exact size */
		f->fmt.pix.bytesperline = ALIGN(f->fmt.pix.width * fmt->depth / 8,
					       fmt->bytesperline_align);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
	}
	
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	
	return 0;
}

static int canon_r5_video_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct canon_r5_video_device *vdev = video_drvdata(file);
	int ret;
	
	ret = canon_r5_video_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;
	
	/* Check if streaming is active */
	if (vb2_is_streaming(&vdev->stream.queue))
		return -EBUSY;
	
	/* Update current format */
	vdev->pix_format = f->fmt.pix;
	vdev->stream.format = canon_r5_video_find_format(f->fmt.pix.pixelformat);
	vdev->stream.resolution = canon_r5_video_find_resolution(f->fmt.pix.width,
								f->fmt.pix.height);
	
	canon_r5_video_info(vdev, "Format set to %s %dx%d",
			   vdev->stream.format->name,
			   vdev->stream.resolution->width,
			   vdev->stream.resolution->height);
	
	return 0;
}

static int canon_r5_video_enum_framesizes(struct file *file, void *priv,
					  struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= canon_r5_video_num_resolutions)
		return -EINVAL;
	
	/* Check if format is supported */
	if (!canon_r5_video_find_format(fsize->pixel_format))
		return -EINVAL;
	
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = canon_r5_video_resolutions[fsize->index].width;
	fsize->discrete.height = canon_r5_video_resolutions[fsize->index].height;
	
	return 0;
}

static int canon_r5_video_enum_frameintervals(struct file *file, void *priv,
					      struct v4l2_frmivalenum *fival)
{
	struct canon_r5_video_resolution *res;
	
	if (fival->index > 0)
		return -EINVAL;
	
	/* Check if format is supported */
	if (!canon_r5_video_find_format(fival->pixel_format))
		return -EINVAL;
	
	/* Find resolution */
	res = canon_r5_video_find_resolution(fival->width, fival->height);
	if (!res)
		return -EINVAL;
	
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = res->fps_den;
	fival->discrete.denominator = res->fps_num;
	
	return 0;
}

static int canon_r5_video_g_parm(struct file *file, void *priv,
				 struct v4l2_streamparm *parm)
{
	struct canon_r5_video_device *vdev = video_drvdata(file);
	
	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe = vdev->frame_interval;
	parm->parm.capture.readbuffers = 3;
	
	return 0;
}

static int canon_r5_video_s_parm(struct file *file, void *priv,
				 struct v4l2_streamparm *parm)
{
	struct canon_r5_video_device *vdev = video_drvdata(file);
	struct v4l2_fract *interval;
	
	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	interval = &parm->parm.capture.timeperframe;
	
	/* Validate frame interval */
	if (interval->numerator == 0 || interval->denominator == 0) {
		/* Use current resolution's default frame rate */
		if (vdev->stream.resolution) {
			interval->numerator = vdev->stream.resolution->fps_den;
			interval->denominator = vdev->stream.resolution->fps_num;
		} else {
			interval->numerator = 1;
			interval->denominator = 30;
		}
	}
	
	vdev->frame_interval = *interval;
	parm->parm.capture.timeperframe = vdev->frame_interval;
	
	return 0;
}

const struct v4l2_ioctl_ops canon_r5_video_ioctl_ops = {
	.vidioc_querycap = canon_r5_video_querycap,
	.vidioc_enum_fmt_vid_cap = canon_r5_video_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = canon_r5_video_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = canon_r5_video_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = canon_r5_video_s_fmt_vid_cap,
	.vidioc_enum_framesizes = canon_r5_video_enum_framesizes,
	.vidioc_enum_frameintervals = canon_r5_video_enum_frameintervals,
	.vidioc_g_parm = canon_r5_video_g_parm,
	.vidioc_s_parm = canon_r5_video_s_parm,
	
	/* Streaming ioctls */
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

/* Initialize video device */
int canon_r5_video_init_device(struct canon_r5_device *canon_dev,
			       struct canon_r5_video_device *vdev,
			       enum canon_r5_video_type type)
{
	struct video_device *video_dev = &vdev->vdev;
	int ret;
	
	vdev->canon_dev = canon_dev;
	vdev->type = type;
	vdev->initialized = false;
	atomic_set(&vdev->open_count, 0);
	mutex_init(&vdev->lock);
	
	/* Set default format */
	vdev->stream.format = (struct canon_r5_video_format *)&canon_r5_video_formats[0];
	vdev->stream.resolution = canon_r5_video_find_resolution(1920, 1080);
	if (!vdev->stream.resolution) {
		vdev->stream.resolution = (struct canon_r5_video_resolution *)&canon_r5_video_resolutions[0];
	}
	
	/* Initialize pixel format */
	vdev->pix_format.width = vdev->stream.resolution->width;
	vdev->pix_format.height = vdev->stream.resolution->height;
	vdev->pix_format.pixelformat = vdev->stream.format->fourcc;
	vdev->pix_format.field = V4L2_FIELD_NONE;
	vdev->pix_format.colorspace = V4L2_COLORSPACE_SRGB;
	
	if (vdev->stream.format->compressed) {
		vdev->pix_format.sizeimage = (vdev->pix_format.width * 
					     vdev->pix_format.height *
					     vdev->stream.format->depth) / 8;
		vdev->pix_format.bytesperline = 0;
	} else {
		vdev->pix_format.bytesperline = 
			ALIGN(vdev->pix_format.width * vdev->stream.format->depth / 8,
			     vdev->stream.format->bytesperline_align);
		vdev->pix_format.sizeimage = vdev->pix_format.bytesperline * 
					    vdev->pix_format.height;
	}
	
	/* Initialize frame interval */
	vdev->frame_interval.numerator = vdev->stream.resolution->fps_den;
	vdev->frame_interval.denominator = vdev->stream.resolution->fps_num;
	
	/* Initialize streaming state */
	vdev->stream.state = CANON_R5_STREAMING_STOPPED;
	INIT_LIST_HEAD(&vdev->stream.buf_list);
	spin_lock_init(&vdev->stream.buf_lock);
	
	/* Set up video device */
	snprintf(video_dev->name, sizeof(video_dev->name),
		 "Canon R5 %s", canon_r5_video_type_name(type));
	video_dev->fops = &canon_r5_video_fops;
	video_dev->ioctl_ops = &canon_r5_video_ioctl_ops;
	video_dev->release = video_device_release_empty;
	video_dev->lock = &vdev->lock;
	video_dev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	
	video_set_drvdata(video_dev, vdev);
	
	canon_r5_video_info(vdev, "Video device initialized: %s (%dx%d %s)",
			   video_dev->name,
			   vdev->stream.resolution->width,
			   vdev->stream.resolution->height,
			   vdev->stream.format->name);
	
	vdev->initialized = true;
	return 0;
}
EXPORT_SYMBOL_GPL(canon_r5_video_init_device);

/* Register video devices */
int canon_r5_video_register_devices(struct canon_r5_device *canon_dev)
{
	struct canon_r5_video *video = canon_dev->video_priv;
	int i, ret;
	
	if (!video) {
		dev_err(canon_dev->dev, "Video driver not initialized");
		return -ENODEV;
	}
	
	for (i = 0; i < video->num_devices; i++) {
		struct canon_r5_video_device *vdev = &video->devices[i];
		
		ret = video_register_device(&vdev->vdev, VFL_TYPE_VIDEO, -1);
		if (ret) {
			canon_r5_video_err(vdev, "Failed to register video device: %d", ret);
			goto unreg_devices;
		}
		
		canon_r5_video_info(vdev, "Registered as %s", 
				   video_device_node_name(&vdev->vdev));
	}
	
	return 0;
	
unreg_devices:
	while (--i >= 0) {
		video_unregister_device(&video->devices[i].vdev);
	}
	return ret;
}

/* Unregister video devices */
void canon_r5_video_unregister_devices(struct canon_r5_device *canon_dev)
{
	struct canon_r5_video *video = canon_dev->video_priv;
	int i;
	
	if (!video)
		return;
	
	for (i = 0; i < video->num_devices; i++) {
		struct canon_r5_video_device *vdev = &video->devices[i];
		
		if (video_is_registered(&vdev->vdev)) {
			canon_r5_video_info(vdev, "Unregistering %s",
					   video_device_node_name(&vdev->vdev));
			video_unregister_device(&vdev->vdev);
		}
	}
}

/* Forward declarations for enhanced functions */
extern int canon_r5_video_init_enhanced(struct canon_r5_device *canon_dev);
extern void canon_r5_video_cleanup_enhanced(struct canon_r5_device *canon_dev);

/* Initialize video driver */
int canon_r5_video_init(struct canon_r5_device *canon_dev)
{
	return canon_r5_video_init_enhanced(canon_dev);
}

/* Cleanup video driver */
void canon_r5_video_cleanup(struct canon_r5_device *canon_dev)
{
	canon_r5_video_cleanup_enhanced(canon_dev);
}

static int __init canon_r5_video_module_init(void)
{
	pr_info("Canon R5 Driver Suite - V4L2 Video Module Loading\n");
	
	/* Video driver will be initialized when core driver detects device */
	
	pr_info("Canon R5 Driver Suite - V4L2 Video Module Loaded\n");
	return 0;
}

static void __exit canon_r5_video_module_exit(void)
{
	pr_info("Canon R5 Driver Suite - V4L2 Video Module Unloading\n");
	
	/* Cleanup handled by core driver */
	
	pr_info("Canon R5 Driver Suite - V4L2 Video Module Unloaded\n");
}

module_init(canon_r5_video_module_init);
module_exit(canon_r5_video_module_exit);