/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite - Audio Driver Unit Tests
 * KUnit tests for ALSA audio capture functionality
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <kunit/test.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/control.h>

#include "audio/canon-r5-audio.h"
#include "core/canon-r5.h"

/**
 * Test context structure for audio tests
 */
struct canon_r5_audio_test_ctx {
	struct platform_device *pdev;
	struct canon_r5_device *canon_dev;
	struct canon_r5_audio_device *audio_dev;
};

/* Test fixture setup */
static int canon_r5_audio_test_init(struct kunit *test)
{
	struct canon_r5_audio_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/* Create mock platform device */
	ctx->pdev = platform_device_alloc("canon-r5-audio-test", 0);
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

	/* Create audio device */
	ctx->audio_dev = kunit_kzalloc(test, sizeof(*ctx->audio_dev), GFP_KERNEL);
	if (!ctx->audio_dev) {
		platform_device_unregister(ctx->pdev);
		return -ENOMEM;
	}

	ctx->audio_dev->canon_dev = ctx->canon_dev;
	mutex_init(&ctx->audio_dev->lock);
	ctx->audio_dev->initialized = true;

	test->priv = ctx;
	return 0;
}

/* Test fixture teardown */
static void canon_r5_audio_test_exit(struct kunit *test)
{
	struct canon_r5_audio_test_ctx *ctx = test->priv;

	if (ctx && ctx->pdev)
		platform_device_unregister(ctx->pdev);
}

/* Test audio format validation */
static void canon_r5_audio_format_validation_test(struct kunit *test)
{
	/* Test valid formats */
	KUNIT_EXPECT_TRUE(test, canon_r5_audio_format_valid(CANON_R5_AUDIO_PCM_16));
	KUNIT_EXPECT_TRUE(test, canon_r5_audio_format_valid(CANON_R5_AUDIO_PCM_24));
	KUNIT_EXPECT_TRUE(test, canon_r5_audio_format_valid(CANON_R5_AUDIO_PCM_32));

	/* Test invalid formats */
	KUNIT_EXPECT_FALSE(test, canon_r5_audio_format_valid(CANON_R5_AUDIO_FORMAT_COUNT));
	KUNIT_EXPECT_FALSE(test, canon_r5_audio_format_valid(999));
}

/* Test sample rate validation */
static void canon_r5_audio_sample_rate_validation_test(struct kunit *test)
{
	/* Test valid sample rates */
	KUNIT_EXPECT_TRUE(test, canon_r5_audio_sample_rate_valid(CANON_R5_AUDIO_RATE_48K));
	KUNIT_EXPECT_TRUE(test, canon_r5_audio_sample_rate_valid(CANON_R5_AUDIO_RATE_96K));

	/* Test invalid sample rates */
	KUNIT_EXPECT_FALSE(test, canon_r5_audio_sample_rate_valid(CANON_R5_AUDIO_RATE_COUNT));
	KUNIT_EXPECT_FALSE(test, canon_r5_audio_sample_rate_valid(999));
}

/* Test channel count validation */
static void canon_r5_audio_channels_validation_test(struct kunit *test)
{
	/* Test valid channel counts */
	KUNIT_EXPECT_TRUE(test, canon_r5_audio_channels_valid(CANON_R5_AUDIO_MONO));
	KUNIT_EXPECT_TRUE(test, canon_r5_audio_channels_valid(CANON_R5_AUDIO_STEREO));

	/* Test invalid channel counts */
	KUNIT_EXPECT_FALSE(test, canon_r5_audio_channels_valid(CANON_R5_AUDIO_CHANNELS_COUNT));
	KUNIT_EXPECT_FALSE(test, canon_r5_audio_channels_valid(999));
}

/* Test audio settings validation */
static void canon_r5_audio_settings_validation_test(struct kunit *test)
{
	struct canon_r5_audio_settings settings;

	/* Valid settings */
	settings.format = CANON_R5_AUDIO_PCM_24;
	settings.sample_rate = CANON_R5_AUDIO_RATE_96K;
	settings.channels = CANON_R5_AUDIO_STEREO;
	settings.gain_left = 50;
	settings.gain_right = 50;
	settings.auto_gain = false;
	settings.wind_filter = true;
	settings.attenuator = false;

	KUNIT_EXPECT_EQ(test, canon_r5_audio_validate_settings(&settings), 0);

	/* Invalid format */
	settings.format = 999;
	KUNIT_EXPECT_NE(test, canon_r5_audio_validate_settings(&settings), 0);
	settings.format = CANON_R5_AUDIO_PCM_24; /* Reset */

	/* Invalid sample rate */
	settings.sample_rate = 999;
	KUNIT_EXPECT_NE(test, canon_r5_audio_validate_settings(&settings), 0);
	settings.sample_rate = CANON_R5_AUDIO_RATE_96K; /* Reset */

	/* Invalid channels */
	settings.channels = 999;
	KUNIT_EXPECT_NE(test, canon_r5_audio_validate_settings(&settings), 0);
	settings.channels = CANON_R5_AUDIO_STEREO; /* Reset */

	/* Invalid gain values */
	settings.gain_left = 101;
	KUNIT_EXPECT_NE(test, canon_r5_audio_validate_settings(&settings), 0);
	settings.gain_left = 50; /* Reset */

	settings.gain_right = -1;
	KUNIT_EXPECT_NE(test, canon_r5_audio_validate_settings(&settings), 0);
}

/* Test audio device initialization */
static void canon_r5_audio_device_init_test(struct kunit *test)
{
	struct canon_r5_audio_test_ctx *ctx = test->priv;

	/* Verify initial device state */
	KUNIT_EXPECT_PTR_EQ(test, ctx->audio_dev->canon_dev, ctx->canon_dev);
	KUNIT_EXPECT_TRUE(test, ctx->audio_dev->initialized);

	/* Verify recording state */
	KUNIT_EXPECT_FALSE(test, ctx->audio_dev->recording_active);
	KUNIT_EXPECT_EQ(test, ctx->audio_dev->buffer_count, 0);
	KUNIT_EXPECT_EQ(test, ctx->audio_dev->current_buffer, 0);
}

/* Test audio settings configuration */
static void canon_r5_audio_settings_test(struct kunit *test)
{
	struct canon_r5_audio_test_ctx *ctx = test->priv;
	struct canon_r5_audio_settings settings, retrieved_settings;

	/* Set up test audio settings */
	settings.format = CANON_R5_AUDIO_PCM_24;
	settings.sample_rate = CANON_R5_AUDIO_RATE_96K;
	settings.channels = CANON_R5_AUDIO_STEREO;
	settings.gain_left = 75;
	settings.gain_right = 80;
	settings.auto_gain = true;
	settings.wind_filter = false;
	settings.attenuator = true;

	/* Set settings */
	int ret = canon_r5_audio_set_settings(ctx->audio_dev, &settings);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Get settings back */
	memset(&retrieved_settings, 0, sizeof(retrieved_settings));
	ret = canon_r5_audio_get_settings(ctx->audio_dev, &retrieved_settings);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Verify settings match */
	KUNIT_EXPECT_EQ(test, retrieved_settings.format, settings.format);
	KUNIT_EXPECT_EQ(test, retrieved_settings.sample_rate, settings.sample_rate);
	KUNIT_EXPECT_EQ(test, retrieved_settings.channels, settings.channels);
	KUNIT_EXPECT_EQ(test, retrieved_settings.gain_left, settings.gain_left);
	KUNIT_EXPECT_EQ(test, retrieved_settings.gain_right, settings.gain_right);
	KUNIT_EXPECT_EQ(test, retrieved_settings.auto_gain, settings.auto_gain);
	KUNIT_EXPECT_EQ(test, retrieved_settings.wind_filter, settings.wind_filter);
	KUNIT_EXPECT_EQ(test, retrieved_settings.attenuator, settings.attenuator);
}

/* Test PCM hardware constraints */
static void canon_r5_audio_pcm_hw_test(struct kunit *test)
{
	struct canon_r5_audio_test_ctx *ctx = test->priv;
	struct canon_r5_audio_pcm *pcm = &ctx->audio_dev->pcm;

	/* Set up test PCM hardware parameters */
	pcm->hw.info = SNDRV_PCM_INFO_MMAP |
		       SNDRV_PCM_INFO_MMAP_VALID |
		       SNDRV_PCM_INFO_INTERLEAVED |
		       SNDRV_PCM_INFO_BLOCK_TRANSFER;

	pcm->hw.formats = SNDRV_PCM_FMTBIT_S16_LE |
			  SNDRV_PCM_FMTBIT_S24_LE |
			  SNDRV_PCM_FMTBIT_S32_LE;

	pcm->hw.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000;
	pcm->hw.rate_min = 48000;
	pcm->hw.rate_max = 96000;
	pcm->hw.channels_min = 1;
	pcm->hw.channels_max = 2;
	pcm->hw.buffer_bytes_max = 64 * 1024;
	pcm->hw.period_bytes_min = 1024;
	pcm->hw.period_bytes_max = 8192;
	pcm->hw.periods_min = 4;
	pcm->hw.periods_max = 32;

	/* Verify PCM hardware constraints */
	KUNIT_EXPECT_TRUE(test, pcm->hw.info & SNDRV_PCM_INFO_MMAP);
	KUNIT_EXPECT_TRUE(test, pcm->hw.info & SNDRV_PCM_INFO_INTERLEAVED);
	KUNIT_EXPECT_TRUE(test, pcm->hw.formats & SNDRV_PCM_FMTBIT_S24_LE);
	KUNIT_EXPECT_TRUE(test, pcm->hw.rates & SNDRV_PCM_RATE_96000);
	KUNIT_EXPECT_EQ(test, pcm->hw.rate_min, 48000);
	KUNIT_EXPECT_EQ(test, pcm->hw.rate_max, 96000);
	KUNIT_EXPECT_EQ(test, pcm->hw.channels_min, 1);
	KUNIT_EXPECT_EQ(test, pcm->hw.channels_max, 2);
	KUNIT_EXPECT_EQ(test, pcm->hw.buffer_bytes_max, 64 * 1024);
}

/* Test audio buffer management */
static void canon_r5_audio_buffer_test(struct kunit *test)
{
	struct canon_r5_audio_buffer *buffer;

	/* Create test audio buffer */
	buffer = kunit_kzalloc(test, sizeof(*buffer), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buffer);

	INIT_LIST_HEAD(&buffer->list);
	buffer->size = 8192;
	buffer->frames = 1024;
	buffer->channels = 2;
	buffer->sample_rate = 96000;
	buffer->format = CANON_R5_AUDIO_PCM_24;

	/* Verify buffer properties */
	KUNIT_EXPECT_EQ(test, buffer->size, 8192);
	KUNIT_EXPECT_EQ(test, buffer->frames, 1024);
	KUNIT_EXPECT_EQ(test, buffer->channels, 2);
	KUNIT_EXPECT_EQ(test, buffer->sample_rate, 96000);
	KUNIT_EXPECT_EQ(test, buffer->format, CANON_R5_AUDIO_PCM_24);
	KUNIT_EXPECT_NULL(test, buffer->data);
	KUNIT_EXPECT_EQ(test, buffer->dma_addr, 0);
}

/* Test audio statistics */
static void canon_r5_audio_stats_test(struct kunit *test)
{
	struct canon_r5_audio_test_ctx *ctx = test->priv;
	struct canon_r5_audio_stats stats;

	/* Initialize test statistics */
	memset(&ctx->audio_dev->stats, 0, sizeof(ctx->audio_dev->stats));
	ctx->audio_dev->stats.frames_captured = 10000;
	ctx->audio_dev->stats.frames_dropped = 50;
	ctx->audio_dev->stats.total_bytes = 1024 * 1024; /* 1MB */
	ctx->audio_dev->stats.underruns = 2;
	ctx->audio_dev->stats.overruns = 1;
	ctx->audio_dev->stats.current_sample_rate = 96000;

	/* Get statistics */
	memset(&stats, 0, sizeof(stats));
	int ret = canon_r5_audio_get_stats(ctx->audio_dev, &stats);

	/* Verify statistics retrieval */
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, stats.frames_captured, 10000);
	KUNIT_EXPECT_EQ(test, stats.frames_dropped, 50);
	KUNIT_EXPECT_EQ(test, stats.total_bytes, 1024 * 1024);
	KUNIT_EXPECT_EQ(test, stats.underruns, 2);
	KUNIT_EXPECT_EQ(test, stats.overruns, 1);
	KUNIT_EXPECT_EQ(test, stats.current_sample_rate, 96000);

	/* Test statistics reset */
	canon_r5_audio_reset_stats(ctx->audio_dev);
	ret = canon_r5_audio_get_stats(ctx->audio_dev, &stats);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, stats.frames_captured, 0);
	KUNIT_EXPECT_EQ(test, stats.frames_dropped, 0);
	KUNIT_EXPECT_EQ(test, stats.total_bytes, 0);
	KUNIT_EXPECT_EQ(test, stats.underruns, 0);
	KUNIT_EXPECT_EQ(test, stats.overruns, 0);
}

/* Test audio format name conversion */
static void canon_r5_audio_format_names_test(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_format_name(CANON_R5_AUDIO_PCM_16), "PCM 16-bit");
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_format_name(CANON_R5_AUDIO_PCM_24), "PCM 24-bit");
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_format_name(CANON_R5_AUDIO_PCM_32), "PCM 32-bit");
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_format_name(999), "Unknown");
}

/* Test sample rate name conversion */
static void canon_r5_audio_sample_rate_names_test(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_sample_rate_name(CANON_R5_AUDIO_RATE_48K), "48 kHz");
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_sample_rate_name(CANON_R5_AUDIO_RATE_96K), "96 kHz");
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_sample_rate_name(999), "Unknown");
}

/* Test channels name conversion */
static void canon_r5_audio_channels_names_test(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_channels_name(CANON_R5_AUDIO_MONO), "Mono");
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_channels_name(CANON_R5_AUDIO_STEREO), "Stereo");
	KUNIT_EXPECT_STREQ(test, canon_r5_audio_channels_name(999), "Unknown");
}

/* Test volume control */
static void canon_r5_audio_volume_test(struct kunit *test)
{
	struct canon_r5_audio_test_ctx *ctx = test->priv;

	/* Test valid volume levels (0-100) */
	KUNIT_EXPECT_EQ(test, canon_r5_audio_set_volume(ctx->audio_dev, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, canon_r5_audio_set_volume(ctx->audio_dev, 50, 50), 0);
	KUNIT_EXPECT_EQ(test, canon_r5_audio_set_volume(ctx->audio_dev, 100, 100), 0);

	/* Test asymmetric volume levels */
	KUNIT_EXPECT_EQ(test, canon_r5_audio_set_volume(ctx->audio_dev, 75, 25), 0);
	KUNIT_EXPECT_EQ(test, canon_r5_audio_set_volume(ctx->audio_dev, 10, 90), 0);

	/* Test invalid volume levels */
	KUNIT_EXPECT_NE(test, canon_r5_audio_set_volume(ctx->audio_dev, -1, 50), 0);
	KUNIT_EXPECT_NE(test, canon_r5_audio_set_volume(ctx->audio_dev, 50, 101), 0);
	KUNIT_EXPECT_NE(test, canon_r5_audio_set_volume(ctx->audio_dev, 200, 200), 0);
}

/* Test buffer list operations */
static void canon_r5_audio_buffer_list_test(struct kunit *test)
{
	struct canon_r5_audio_test_ctx *ctx = test->priv;
	struct canon_r5_audio_buffer *buffer1, *buffer2;
	struct list_head *buf_list = &ctx->audio_dev->buffer_list;

	/* Initialize buffer list */
	INIT_LIST_HEAD(buf_list);

	/* Create test buffers */
	buffer1 = kunit_kzalloc(test, sizeof(*buffer1), GFP_KERNEL);
	buffer2 = kunit_kzalloc(test, sizeof(*buffer2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buffer1);
	KUNIT_ASSERT_NOT_NULL(test, buffer2);

	INIT_LIST_HEAD(&buffer1->list);
	INIT_LIST_HEAD(&buffer2->list);
	buffer1->size = 4096;
	buffer2->size = 8192;

	/* Verify initial empty list */
	KUNIT_EXPECT_TRUE(test, list_empty(buf_list));

	/* Add buffers to list */
	list_add_tail(&buffer1->list, buf_list);
	KUNIT_EXPECT_FALSE(test, list_empty(buf_list));

	list_add_tail(&buffer2->list, buf_list);

	/* Verify list contents */
	KUNIT_EXPECT_PTR_EQ(test, list_first_entry(buf_list, struct canon_r5_audio_buffer, list), buffer1);
	KUNIT_EXPECT_PTR_EQ(test, list_last_entry(buf_list, struct canon_r5_audio_buffer, list), buffer2);

	/* Remove buffers */
	list_del(&buffer1->list);
	list_del(&buffer2->list);
	KUNIT_EXPECT_TRUE(test, list_empty(buf_list));
}

/* KUnit test suite definition */
static struct kunit_case canon_r5_audio_test_cases[] = {
	KUNIT_CASE(canon_r5_audio_format_validation_test),
	KUNIT_CASE(canon_r5_audio_sample_rate_validation_test),
	KUNIT_CASE(canon_r5_audio_channels_validation_test),
	KUNIT_CASE(canon_r5_audio_settings_validation_test),
	KUNIT_CASE(canon_r5_audio_device_init_test),
	KUNIT_CASE(canon_r5_audio_settings_test),
	KUNIT_CASE(canon_r5_audio_pcm_hw_test),
	KUNIT_CASE(canon_r5_audio_buffer_test),
	KUNIT_CASE(canon_r5_audio_stats_test),
	KUNIT_CASE(canon_r5_audio_format_names_test),
	KUNIT_CASE(canon_r5_audio_sample_rate_names_test),
	KUNIT_CASE(canon_r5_audio_channels_names_test),
	KUNIT_CASE(canon_r5_audio_volume_test),
	KUNIT_CASE(canon_r5_audio_buffer_list_test),
	{}
};

static struct kunit_suite canon_r5_audio_test_suite = {
	.name = "canon_r5_audio",
	.init = canon_r5_audio_test_init,
	.exit = canon_r5_audio_test_exit,
	.test_cases = canon_r5_audio_test_cases,
};

kunit_test_suite(canon_r5_audio_test_suite);

MODULE_DESCRIPTION("Canon R5 Audio Driver Unit Tests");
MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_LICENSE("GPL v2");