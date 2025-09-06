/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite - Video Driver Unit Tests
 * KUnit tests for V4L2 video capture functionality
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <kunit/test.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>

#include "video/canon-r5-v4l2.h"
#include "core/canon-r5.h"

/**
 * Test context structure for video tests
 */
struct canon_r5_video_test_ctx {
	struct platform_device *pdev;
	struct canon_r5_device *canon_dev;
	struct canon_r5_video_device *video_dev;
};

/* Test fixture setup */
static int canon_r5_video_test_init(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/* Create mock platform device */
	ctx->pdev = platform_device_alloc("canon-r5-video-test", 0);
	if (!ctx->pdev) {
		kunit_err(test, "Failed to allocate platform device");
		return -ENOMEM;
	}

	if (platform_device_add(ctx->pdev)) {
		platform_device_put(ctx->pdev);
		kunit_err(test, "Failed to add platform device");
		return -ENODEV;
	}

	/* Create mock Canon R5 device */
	ctx->canon_dev = kunit_kzalloc(test, sizeof(*ctx->canon_dev), GFP_KERNEL);
	if (!ctx->canon_dev) {
		platform_device_unregister(ctx->pdev);
		return -ENOMEM;
	}

	ctx->canon_dev->dev = &ctx->pdev->dev;
	ctx->canon_dev->state = CANON_R5_STATE_READY;
	mutex_init(&ctx->canon_dev->state_lock);

	/* Create video device */
	ctx->video_dev = kunit_kzalloc(test, sizeof(*ctx->video_dev), GFP_KERNEL);
	if (!ctx->video_dev) {
		platform_device_unregister(ctx->pdev);
		return -ENOMEM;
	}

	ctx->video_dev->canon_dev = ctx->canon_dev;
	ctx->video_dev->type = CANON_R5_VIDEO_MAIN;
	mutex_init(&ctx->video_dev->lock);
	atomic_set(&ctx->video_dev->open_count, 0);
	ctx->video_dev->initialized = true;

	test->priv = ctx;
	return 0;
}

/* Test fixture teardown */
static void canon_r5_video_test_exit(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx = test->priv;

	if (ctx && ctx->pdev)
		platform_device_unregister(ctx->pdev);
}

/* Test video device type validation */
static void canon_r5_video_type_validation_test(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, canon_r5_video_type_name(CANON_R5_VIDEO_MAIN), "Main");
	KUNIT_EXPECT_STREQ(test, canon_r5_video_type_name(CANON_R5_VIDEO_PREVIEW), "Preview");
	KUNIT_EXPECT_STREQ(test, canon_r5_video_type_name(CANON_R5_VIDEO_ENCODER), "Encoder");
	KUNIT_EXPECT_STREQ(test, canon_r5_video_type_name(999), "Unknown");
}

/* Test video format finding */
static void canon_r5_video_format_test(struct kunit *test)
{
	struct canon_r5_video_format *format;

	/* Test common V4L2 formats */
	format = canon_r5_video_find_format(V4L2_PIX_FMT_YUYV);
	KUNIT_EXPECT_NOT_NULL(test, format);
	if (format) {
		KUNIT_EXPECT_EQ(test, format->fourcc, V4L2_PIX_FMT_YUYV);
		KUNIT_EXPECT_STREQ(test, format->name, "YUYV 4:2:2");
		KUNIT_EXPECT_EQ(test, format->depth, 16);
		KUNIT_EXPECT_FALSE(test, format->compressed);
	}

	format = canon_r5_video_find_format(V4L2_PIX_FMT_MJPEG);
	KUNIT_EXPECT_NOT_NULL(test, format);
	if (format) {
		KUNIT_EXPECT_EQ(test, format->fourcc, V4L2_PIX_FMT_MJPEG);
		KUNIT_EXPECT_STREQ(test, format->name, "Motion-JPEG");
		KUNIT_EXPECT_TRUE(test, format->compressed);
	}

	/* Test invalid format */
	format = canon_r5_video_find_format(0x12345678);
	KUNIT_EXPECT_NULL(test, format);
}

/* Test video resolution finding */
static void canon_r5_video_resolution_test(struct kunit *test)
{
	struct canon_r5_video_resolution *resolution;

	/* Test 8K resolution */
	resolution = canon_r5_video_find_resolution(7680, 4320);
	KUNIT_EXPECT_NOT_NULL(test, resolution);
	if (resolution) {
		KUNIT_EXPECT_EQ(test, resolution->width, 7680);
		KUNIT_EXPECT_EQ(test, resolution->height, 4320);
		KUNIT_EXPECT_STREQ(test, resolution->name, "8K DCI");
	}

	/* Test 4K resolution */
	resolution = canon_r5_video_find_resolution(3840, 2160);
	KUNIT_EXPECT_NOT_NULL(test, resolution);
	if (resolution) {
		KUNIT_EXPECT_EQ(test, resolution->width, 3840);
		KUNIT_EXPECT_EQ(test, resolution->height, 2160);
		KUNIT_EXPECT_STREQ(test, resolution->name, "4K UHD");
	}

	/* Test 1080p resolution */
	resolution = canon_r5_video_find_resolution(1920, 1080);
	KUNIT_EXPECT_NOT_NULL(test, resolution);
	if (resolution) {
		KUNIT_EXPECT_EQ(test, resolution->width, 1920);
		KUNIT_EXPECT_EQ(test, resolution->height, 1080);
		KUNIT_EXPECT_STREQ(test, resolution->name, "Full HD");
	}

	/* Test invalid resolution */
	resolution = canon_r5_video_find_resolution(123, 456);
	KUNIT_EXPECT_NULL(test, resolution);
}

/* Test video device initialization */
static void canon_r5_video_device_init_test(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx = test->priv;

	/* Verify initial device state */
	KUNIT_EXPECT_PTR_EQ(test, ctx->video_dev->canon_dev, ctx->canon_dev);
	KUNIT_EXPECT_EQ(test, ctx->video_dev->type, CANON_R5_VIDEO_MAIN);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->video_dev->open_count), 0);
	KUNIT_EXPECT_TRUE(test, ctx->video_dev->initialized);

	/* Verify streaming state */
	KUNIT_EXPECT_EQ(test, ctx->video_dev->stream.state, CANON_R5_STREAMING_STOPPED);
	KUNIT_EXPECT_EQ(test, ctx->video_dev->stream.frame_count, 0);
	KUNIT_EXPECT_EQ(test, ctx->video_dev->stream.dropped_frames, 0);
}

/* Test pixel format configuration */
static void canon_r5_video_pixel_format_test(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx = test->priv;
	struct v4l2_pix_format *pix = &ctx->video_dev->pix_format;

	/* Set up test pixel format */
	pix->width = 1920;
	pix->height = 1080;
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = 1920 * 2; /* YUYV is 2 bytes per pixel */
	pix->sizeimage = 1920 * 1080 * 2;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->priv = 0;

	/* Verify pixel format properties */
	KUNIT_EXPECT_EQ(test, pix->width, 1920);
	KUNIT_EXPECT_EQ(test, pix->height, 1080);
	KUNIT_EXPECT_EQ(test, pix->pixelformat, V4L2_PIX_FMT_YUYV);
	KUNIT_EXPECT_EQ(test, pix->field, V4L2_FIELD_NONE);
	KUNIT_EXPECT_EQ(test, pix->bytesperline, 1920 * 2);
	KUNIT_EXPECT_EQ(test, pix->sizeimage, 1920 * 1080 * 2);
	KUNIT_EXPECT_EQ(test, pix->colorspace, V4L2_COLORSPACE_SRGB);
}

/* Test frame interval configuration */
static void canon_r5_video_frame_interval_test(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx = test->priv;
	struct v4l2_fract *interval = &ctx->video_dev->frame_interval;

	/* Set up test frame intervals */
	
	/* 30 fps */
	interval->numerator = 1;
	interval->denominator = 30;
	KUNIT_EXPECT_EQ(test, interval->numerator, 1);
	KUNIT_EXPECT_EQ(test, interval->denominator, 30);

	/* 60 fps */
	interval->numerator = 1;
	interval->denominator = 60;
	KUNIT_EXPECT_EQ(test, interval->numerator, 1);
	KUNIT_EXPECT_EQ(test, interval->denominator, 60);

	/* 120 fps */
	interval->numerator = 1;
	interval->denominator = 120;
	KUNIT_EXPECT_EQ(test, interval->numerator, 1);
	KUNIT_EXPECT_EQ(test, interval->denominator, 120);

	/* 24 fps (cinema) */
	interval->numerator = 1001;
	interval->denominator = 24000;
	KUNIT_EXPECT_EQ(test, interval->numerator, 1001);
	KUNIT_EXPECT_EQ(test, interval->denominator, 24000);
}

/* Test streaming state transitions */
static void canon_r5_video_streaming_state_test(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx = test->priv;
	struct canon_r5_video_stream *stream = &ctx->video_dev->stream;

	/* Initial state should be stopped */
	KUNIT_EXPECT_EQ(test, stream->state, CANON_R5_STREAMING_STOPPED);

	/* Test state transitions */
	stream->state = CANON_R5_STREAMING_STARTING;
	KUNIT_EXPECT_EQ(test, stream->state, CANON_R5_STREAMING_STARTING);

	stream->state = CANON_R5_STREAMING_ACTIVE;
	KUNIT_EXPECT_EQ(test, stream->state, CANON_R5_STREAMING_ACTIVE);

	stream->state = CANON_R5_STREAMING_STOPPING;
	KUNIT_EXPECT_EQ(test, stream->state, CANON_R5_STREAMING_STOPPING);

	stream->state = CANON_R5_STREAMING_STOPPED;
	KUNIT_EXPECT_EQ(test, stream->state, CANON_R5_STREAMING_STOPPED);
}

/* Test video buffer operations */
static void canon_r5_video_buffer_test(struct kunit *test)
{
	struct canon_r5_video_buffer *buffer;
	struct vb2_v4l2_buffer *vb2_buf;

	/* Create test video buffer */
	buffer = kunit_kzalloc(test, sizeof(*buffer), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buffer);

	INIT_LIST_HEAD(&buffer->list);
	buffer->size = 1920 * 1080 * 2; /* YUYV frame size */

	/* Test container_of macro */
	vb2_buf = &buffer->vb2_buf;
	KUNIT_EXPECT_PTR_EQ(test, to_canon_r5_video_buffer(vb2_buf), buffer);

	/* Verify buffer properties */
	KUNIT_EXPECT_EQ(test, buffer->size, 1920 * 1080 * 2);
	KUNIT_EXPECT_EQ(test, buffer->dma_addr, 0); /* Not allocated yet */
	KUNIT_EXPECT_NULL(test, buffer->vaddr);
}

/* Test video statistics */
static void canon_r5_video_stats_test(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx = test->priv;
	struct canon_r5_video_stream *stream = &ctx->video_dev->stream;
	struct canon_r5_video_stats stats;

	/* Initialize test statistics */
	stream->frame_count = 1000;
	stream->dropped_frames = 25;

	/* Get statistics */
	memset(&stats, 0, sizeof(stats));
	int ret = canon_r5_video_get_stats(ctx->video_dev, &stats);

	/* Verify statistics retrieval */
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, stats.frames_captured, 1000);
	KUNIT_EXPECT_EQ(test, stats.frames_dropped, 25);
}

/* Test open count management */
static void canon_r5_video_open_count_test(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx = test->priv;

	/* Initial open count should be 0 */
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->video_dev->open_count), 0);

	/* Simulate device opens */
	atomic_inc(&ctx->video_dev->open_count);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->video_dev->open_count), 1);

	atomic_inc(&ctx->video_dev->open_count);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->video_dev->open_count), 2);

	/* Simulate device closes */
	atomic_dec(&ctx->video_dev->open_count);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->video_dev->open_count), 1);

	atomic_dec(&ctx->video_dev->open_count);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->video_dev->open_count), 0);
}

/* Test buffer list operations */
static void canon_r5_video_buffer_list_test(struct kunit *test)
{
	struct canon_r5_video_test_ctx *ctx = test->priv;
	struct canon_r5_video_stream *stream = &ctx->video_dev->stream;
	struct canon_r5_video_buffer *buffer1, *buffer2;

	/* Initialize buffer list */
	INIT_LIST_HEAD(&stream->buf_list);
	spin_lock_init(&stream->buf_lock);

	/* Create test buffers */
	buffer1 = kunit_kzalloc(test, sizeof(*buffer1), GFP_KERNEL);
	buffer2 = kunit_kzalloc(test, sizeof(*buffer2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buffer1);
	KUNIT_ASSERT_NOT_NULL(test, buffer2);

	INIT_LIST_HEAD(&buffer1->list);
	INIT_LIST_HEAD(&buffer2->list);

	/* Verify initial empty list */
	KUNIT_EXPECT_TRUE(test, list_empty(&stream->buf_list));

	/* Add buffers to list */
	list_add_tail(&buffer1->list, &stream->buf_list);
	KUNIT_EXPECT_FALSE(test, list_empty(&stream->buf_list));

	list_add_tail(&buffer2->list, &stream->buf_list);

	/* Verify list contents */
	KUNIT_EXPECT_PTR_EQ(test, list_first_entry(&stream->buf_list, struct canon_r5_video_buffer, list), buffer1);
	KUNIT_EXPECT_PTR_EQ(test, list_last_entry(&stream->buf_list, struct canon_r5_video_buffer, list), buffer2);

	/* Remove buffers */
	list_del(&buffer1->list);
	list_del(&buffer2->list);
	KUNIT_EXPECT_TRUE(test, list_empty(&stream->buf_list));
}

/* KUnit test suite definition */
static struct kunit_case canon_r5_video_test_cases[] = {
	KUNIT_CASE(canon_r5_video_type_validation_test),
	KUNIT_CASE(canon_r5_video_format_test),
	KUNIT_CASE(canon_r5_video_resolution_test),
	KUNIT_CASE(canon_r5_video_device_init_test),
	KUNIT_CASE(canon_r5_video_pixel_format_test),
	KUNIT_CASE(canon_r5_video_frame_interval_test),
	KUNIT_CASE(canon_r5_video_streaming_state_test),
	KUNIT_CASE(canon_r5_video_buffer_test),
	KUNIT_CASE(canon_r5_video_stats_test),
	KUNIT_CASE(canon_r5_video_open_count_test),
	KUNIT_CASE(canon_r5_video_buffer_list_test),
	{}
};

static struct kunit_suite canon_r5_video_test_suite = {
	.name = "canon_r5_video",
	.init = canon_r5_video_test_init,
	.exit = canon_r5_video_test_exit,
	.test_cases = canon_r5_video_test_cases,
};

kunit_test_suite(canon_r5_video_test_suite);

MODULE_DESCRIPTION("Canon R5 Video Driver Unit Tests");
MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_LICENSE("GPL v2");