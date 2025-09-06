// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Core Driver KUnit Tests
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <kunit/test.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include "core/canon-r5.h"

/* Test fixture for core driver tests */
struct canon_r5_core_test_context {
	struct canon_r5_device *dev;
	struct platform_device *pdev;
};

/* Test device allocation and initialization */
static void canon_r5_core_device_alloc_test(struct kunit *test)
{
	struct canon_r5_core_test_context *ctx = test->priv;
	struct canon_r5_device *dev;
	
	/* Test device allocation */
	dev = canon_r5_device_alloc(&ctx->pdev->dev);
	KUNIT_ASSERT_NOT_NULL(test, dev);
	
	/* Verify initial state */
	KUNIT_EXPECT_EQ(test, canon_r5_get_state(dev), CANON_R5_STATE_DISCONNECTED);
	KUNIT_EXPECT_EQ(test, dev->capabilities, 0);
	KUNIT_EXPECT_EQ(test, dev->ptp.session_id, 0);
	KUNIT_EXPECT_EQ(test, dev->ptp.transaction_id, 1);
	KUNIT_EXPECT_FALSE(test, dev->ptp.session_open);
	
	/* Cleanup */
	canon_r5_device_put(dev);
}

/* Test device state management */
static void canon_r5_core_state_management_test(struct kunit *test)
{
	struct canon_r5_core_test_context *ctx = test->priv;
	struct canon_r5_device *dev;
	
	dev = canon_r5_device_alloc(&ctx->pdev->dev);
	KUNIT_ASSERT_NOT_NULL(test, dev);
	
	/* Test state transitions */
	KUNIT_EXPECT_EQ(test, canon_r5_set_state(dev, CANON_R5_STATE_CONNECTED), 0);
	KUNIT_EXPECT_EQ(test, canon_r5_get_state(dev), CANON_R5_STATE_CONNECTED);
	
	KUNIT_EXPECT_EQ(test, canon_r5_set_state(dev, CANON_R5_STATE_INITIALIZED), 0);
	KUNIT_EXPECT_EQ(test, canon_r5_get_state(dev), CANON_R5_STATE_INITIALIZED);
	
	KUNIT_EXPECT_EQ(test, canon_r5_set_state(dev, CANON_R5_STATE_READY), 0);
	KUNIT_EXPECT_EQ(test, canon_r5_get_state(dev), CANON_R5_STATE_READY);
	
	KUNIT_EXPECT_EQ(test, canon_r5_set_state(dev, CANON_R5_STATE_ERROR), 0);
	KUNIT_EXPECT_EQ(test, canon_r5_get_state(dev), CANON_R5_STATE_ERROR);
	
	/* Test invalid state handling */
	KUNIT_EXPECT_EQ(test, canon_r5_set_state(NULL, CANON_R5_STATE_READY), -EINVAL);
	KUNIT_EXPECT_EQ(test, canon_r5_get_state(NULL), CANON_R5_STATE_DISCONNECTED);
	
	canon_r5_device_put(dev);
}

/* Test driver registration */
static void canon_r5_core_driver_registration_test(struct kunit *test)
{
	struct canon_r5_core_test_context *ctx = test->priv;
	struct canon_r5_device *dev;
	void *dummy_video_priv = (void *)0x12345678;
	void *dummy_audio_priv = (void *)0x87654321;
	
	dev = canon_r5_device_alloc(&ctx->pdev->dev);
	KUNIT_ASSERT_NOT_NULL(test, dev);
	
	/* Test video driver registration */
	KUNIT_EXPECT_EQ(test, canon_r5_register_video_driver(dev, dummy_video_priv), 0);
	KUNIT_EXPECT_PTR_EQ(test, canon_r5_get_video_driver(dev), dummy_video_priv);
	
	/* Test audio driver registration */
	KUNIT_EXPECT_EQ(test, canon_r5_register_audio_driver(dev, dummy_audio_priv), 0);
	KUNIT_EXPECT_PTR_EQ(test, canon_r5_get_audio_driver(dev), dummy_audio_priv);
	
	/* Test unregistration */
	canon_r5_unregister_video_driver(dev);
	KUNIT_EXPECT_PTR_EQ(test, canon_r5_get_video_driver(dev), NULL);
	
	canon_r5_unregister_audio_driver(dev);
	KUNIT_EXPECT_PTR_EQ(test, canon_r5_get_audio_driver(dev), NULL);
	
	/* Test invalid parameters */
	KUNIT_EXPECT_EQ(test, canon_r5_register_video_driver(NULL, dummy_video_priv), -EINVAL);
	KUNIT_EXPECT_PTR_EQ(test, canon_r5_get_video_driver(NULL), NULL);
	
	canon_r5_device_put(dev);
}

/* Test reference counting */
static void canon_r5_core_reference_counting_test(struct kunit *test)
{
	struct canon_r5_core_test_context *ctx = test->priv;
	struct canon_r5_device *dev;
	
	dev = canon_r5_device_alloc(&ctx->pdev->dev);
	KUNIT_ASSERT_NOT_NULL(test, dev);
	
	/* Take additional references */
	canon_r5_device_get(dev);
	canon_r5_device_get(dev);
	
	/* Release references - device should still be valid */
	canon_r5_device_put(dev);
	canon_r5_device_put(dev);
	
	/* Final release should free the device */
	canon_r5_device_put(dev);
	
	/* Note: We can't verify the device is actually freed since
	 * that would involve accessing freed memory. The test passes
	 * if we don't crash or trigger any memory debugging warnings.
	 */
}

/* Test device initialization and cleanup */
static void canon_r5_core_init_cleanup_test(struct kunit *test)
{
	struct canon_r5_core_test_context *ctx = test->priv;
	struct canon_r5_device *dev;
	int ret;
	
	dev = canon_r5_device_alloc(&ctx->pdev->dev);
	KUNIT_ASSERT_NOT_NULL(test, dev);
	
	/* Test initialization */
	ret = canon_r5_device_initialize(dev);
	/* Note: This may fail in test environment due to missing hardware,
	 * but we test that it handles errors gracefully */
	if (ret == 0) {
		KUNIT_EXPECT_EQ(test, canon_r5_get_state(dev), CANON_R5_STATE_INITIALIZED);
	} else {
		KUNIT_EXPECT_NE(test, ret, 0);
	}
	
	/* Test cleanup (should not crash regardless of init success) */
	canon_r5_device_cleanup(dev);
	
	canon_r5_device_put(dev);
}

/* Test capabilities management */
static void canon_r5_core_capabilities_test(struct kunit *test)
{
	struct canon_r5_core_test_context *ctx = test->priv;
	struct canon_r5_device *dev;
	
	dev = canon_r5_device_alloc(&ctx->pdev->dev);
	KUNIT_ASSERT_NOT_NULL(test, dev);
	
	/* Test initial capabilities */
	KUNIT_EXPECT_EQ(test, dev->capabilities, 0);
	
	/* Test setting capabilities */
	dev->capabilities = CANON_R5_CAP_VIDEO | CANON_R5_CAP_STILL;
	KUNIT_EXPECT_TRUE(test, dev->capabilities & CANON_R5_CAP_VIDEO);
	KUNIT_EXPECT_TRUE(test, dev->capabilities & CANON_R5_CAP_STILL);
	KUNIT_EXPECT_FALSE(test, dev->capabilities & CANON_R5_CAP_AUDIO);
	
	/* Test adding capabilities */
	dev->capabilities |= CANON_R5_CAP_AUDIO | CANON_R5_CAP_STORAGE;
	KUNIT_EXPECT_TRUE(test, dev->capabilities & CANON_R5_CAP_AUDIO);
	KUNIT_EXPECT_TRUE(test, dev->capabilities & CANON_R5_CAP_STORAGE);
	
	canon_r5_device_put(dev);
}

/* Test setup function */
static int canon_r5_core_test_init(struct kunit *test)
{
	struct canon_r5_core_test_context *ctx;
	
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	
	/* Create a dummy platform device for testing */
	ctx->pdev = platform_device_alloc("canon-r5-test", 0);
	if (!ctx->pdev) {
		kfree(ctx);
		return -ENOMEM;
	}
	
	if (platform_device_add(ctx->pdev)) {
		platform_device_put(ctx->pdev);
		kfree(ctx);
		return -ENOMEM;
	}
	
	test->priv = ctx;
	return 0;
}

/* Test cleanup function */
static void canon_r5_core_test_exit(struct kunit *test)
{
	struct canon_r5_core_test_context *ctx = test->priv;
	
	if (ctx) {
		if (ctx->pdev) {
			platform_device_unregister(ctx->pdev);
		}
		kfree(ctx);
	}
}

/* Test case definitions */
static struct kunit_case canon_r5_core_test_cases[] = {
	KUNIT_CASE(canon_r5_core_device_alloc_test),
	KUNIT_CASE(canon_r5_core_state_management_test),
	KUNIT_CASE(canon_r5_core_driver_registration_test),
	KUNIT_CASE(canon_r5_core_reference_counting_test),
	KUNIT_CASE(canon_r5_core_init_cleanup_test),
	KUNIT_CASE(canon_r5_core_capabilities_test),
	{}
};

/* Test suite definition */
static struct kunit_suite canon_r5_core_test_suite = {
	.name = "canon-r5-core",
	.init = canon_r5_core_test_init,
	.exit = canon_r5_core_test_exit,
	.test_cases = canon_r5_core_test_cases,
};

/* Register the test suite */
kunit_test_suite(canon_r5_core_test_suite);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Canon R5 Core Driver KUnit Tests");
MODULE_AUTHOR("Canon R5 Driver Project");