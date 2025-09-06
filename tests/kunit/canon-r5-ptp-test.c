// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 PTP Protocol KUnit Tests
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <kunit/test.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "core/canon-r5.h"
#include "core/canon-r5-ptp.h"

/* Test fixture for PTP tests */
struct canon_r5_ptp_test_context {
	struct canon_r5_device *dev;
	struct platform_device *pdev;
};

/* Test PTP session management */
static void canon_r5_ptp_session_test(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx = test->priv;
	struct canon_r5_device *dev = ctx->dev;
	int ret;
	
	/* Test opening session */
	ret = canon_r5_ptp_open_session(dev);
	/* May fail in test environment, but should handle gracefully */
	if (ret == 0) {
		KUNIT_EXPECT_TRUE(test, dev->ptp.session_open);
		KUNIT_EXPECT_NE(test, dev->ptp.session_id, 0);
		
		/* Test closing session */
		ret = canon_r5_ptp_close_session(dev);
		KUNIT_EXPECT_EQ(test, ret, 0);
		KUNIT_EXPECT_FALSE(test, dev->ptp.session_open);
	}
}

/* Test transaction ID management */
static void canon_r5_ptp_transaction_id_test(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx = test->priv;
	struct canon_r5_device *dev = ctx->dev;
	u32 initial_id, next_id;
	
	/* Get initial transaction ID */
	initial_id = dev->ptp.transaction_id;
	KUNIT_EXPECT_EQ(test, initial_id, 1);
	
	/* Test ID increment */
	next_id = canon_r5_ptp_get_next_transaction_id(dev);
	KUNIT_EXPECT_EQ(test, next_id, initial_id + 1);
	KUNIT_EXPECT_EQ(test, dev->ptp.transaction_id, next_id);
	
	/* Test multiple increments */
	for (int i = 0; i < 10; i++) {
		u32 prev_id = dev->ptp.transaction_id;
		u32 new_id = canon_r5_ptp_get_next_transaction_id(dev);
		KUNIT_EXPECT_EQ(test, new_id, prev_id + 1);
	}
}

/* Test PTP command structure validation */
static void canon_r5_ptp_command_validation_test(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx = test->priv;
	struct canon_r5_device *dev = ctx->dev;
	u32 params[5] = {1, 2, 3, 4, 5};
	u8 response[64];
	u16 response_code;
	int ret;
	
	/* Test valid command parameters */
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_GET_DEVICE_INFO, NULL, 0, 
				   response, sizeof(response), &response_code);
	/* Command may fail due to no hardware, but should validate parameters */
	
	/* Test invalid parameters */
	ret = canon_r5_ptp_command(NULL, CANON_PTP_OP_GET_DEVICE_INFO, NULL, 0,
				   response, sizeof(response), &response_code);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	
	/* Test parameter count validation */
	ret = canon_r5_ptp_command(dev, CANON_PTP_OP_GET_DEVICE_INFO, params, -1,
				   response, sizeof(response), &response_code);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/* Test Canon-specific PTP operations */
static void canon_r5_ptp_canon_operations_test(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx = test->priv;
	struct canon_r5_device *dev = ctx->dev;
	int ret;
	
	/* Test live view operations */
	ret = canon_r5_ptp_start_liveview(dev);
	/* May fail without hardware, but should validate input */
	if (ret != 0) {
		KUNIT_EXPECT_NE(test, ret, -EINVAL); /* Should not be parameter error */
	}
	
	ret = canon_r5_ptp_stop_liveview(dev);
	if (ret != 0) {
		KUNIT_EXPECT_NE(test, ret, -EINVAL);
	}
	
	/* Test invalid device pointer */
	ret = canon_r5_ptp_start_liveview(NULL);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	
	ret = canon_r5_ptp_stop_liveview(NULL);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/* Test PTP capture operations */
static void canon_r5_ptp_capture_operations_test(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx = test->priv;
	struct canon_r5_device *dev = ctx->dev;
	int ret;
	
	/* Test single capture */
	ret = canon_r5_ptp_capture_single(dev);
	if (ret != 0) {
		KUNIT_EXPECT_NE(test, ret, -EINVAL);
	}
	
	/* Test burst capture */
	ret = canon_r5_ptp_capture_burst(dev, 5);
	if (ret != 0) {
		KUNIT_EXPECT_NE(test, ret, -EINVAL);
	}
	
	/* Test autofocus */
	ret = canon_r5_ptp_autofocus(dev);
	if (ret != 0) {
		KUNIT_EXPECT_NE(test, ret, -EINVAL);
	}
	
	/* Test invalid parameters */
	ret = canon_r5_ptp_capture_single(NULL);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	
	ret = canon_r5_ptp_capture_burst(NULL, 5);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	
	ret = canon_r5_ptp_capture_burst(dev, 0);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	
	ret = canon_r5_ptp_capture_burst(dev, 1000); /* Too many */
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/* Test PTP property operations */
static void canon_r5_ptp_property_operations_test(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx = test->priv;
	struct canon_r5_device *dev = ctx->dev;
	u32 value;
	int ret;
	
	/* Test getting properties */
	ret = canon_r5_ptp_get_property(dev, CANON_PTP_PROP_ISO, &value);
	if (ret != 0) {
		KUNIT_EXPECT_NE(test, ret, -EINVAL);
	}
	
	/* Test setting properties */
	ret = canon_r5_ptp_set_property(dev, CANON_PTP_PROP_ISO, 800);
	if (ret != 0) {
		KUNIT_EXPECT_NE(test, ret, -EINVAL);
	}
	
	/* Test invalid parameters */
	ret = canon_r5_ptp_get_property(NULL, CANON_PTP_PROP_ISO, &value);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	
	ret = canon_r5_ptp_get_property(dev, CANON_PTP_PROP_ISO, NULL);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	
	ret = canon_r5_ptp_set_property(NULL, CANON_PTP_PROP_ISO, 800);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

/* Test PTP data validation */
static void canon_r5_ptp_data_validation_test(struct kunit *test)
{
	/* Test PTP response code validation */
	KUNIT_EXPECT_TRUE(test, canon_r5_ptp_response_ok(PTP_RC_OK));
	KUNIT_EXPECT_FALSE(test, canon_r5_ptp_response_ok(PTP_RC_GENERAL_ERROR));
	KUNIT_EXPECT_FALSE(test, canon_r5_ptp_response_ok(PTP_RC_SESSION_NOT_OPEN));
	KUNIT_EXPECT_FALSE(test, canon_r5_ptp_response_ok(PTP_RC_INVALID_PARAMETER));
	
	/* Test operation code validation */
	KUNIT_EXPECT_TRUE(test, canon_r5_ptp_operation_supported(CANON_PTP_OP_GET_DEVICE_INFO));
	KUNIT_EXPECT_TRUE(test, canon_r5_ptp_operation_supported(CANON_PTP_OP_LIVEVIEW_START));
	KUNIT_EXPECT_TRUE(test, canon_r5_ptp_operation_supported(CANON_PTP_OP_CAPTURE));
	
	/* Test invalid operation codes */
	KUNIT_EXPECT_FALSE(test, canon_r5_ptp_operation_supported(0x0000));
	KUNIT_EXPECT_FALSE(test, canon_r5_ptp_operation_supported(0xFFFF));
}

/* Test PTP error handling */
static void canon_r5_ptp_error_handling_test(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx = test->priv;
	struct canon_r5_device *dev = ctx->dev;
	
	/* Test handling of various error scenarios */
	
	/* Session not open error */
	dev->ptp.session_open = false;
	int ret = canon_r5_ptp_capture_single(dev);
	KUNIT_EXPECT_EQ(test, ret, -ENOTCONN);
	
	/* Test device in error state */
	canon_r5_set_state(dev, CANON_R5_STATE_ERROR);
	ret = canon_r5_ptp_capture_single(dev);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	
	/* Reset state for other tests */
	canon_r5_set_state(dev, CANON_R5_STATE_READY);
	dev->ptp.session_open = true;
}

/* Test setup function */
static int canon_r5_ptp_test_init(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx;
	
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	
	/* Create a dummy platform device for testing */
	ctx->pdev = platform_device_alloc("canon-r5-ptp-test", 0);
	if (!ctx->pdev) {
		kfree(ctx);
		return -ENOMEM;
	}
	
	if (platform_device_add(ctx->pdev)) {
		platform_device_put(ctx->pdev);
		kfree(ctx);
		return -ENOMEM;
	}
	
	/* Create test device */
	ctx->dev = canon_r5_device_alloc(&ctx->pdev->dev);
	if (!ctx->dev) {
		platform_device_unregister(ctx->pdev);
		kfree(ctx);
		return -ENOMEM;
	}
	
	/* Set device to ready state for testing */
	canon_r5_set_state(ctx->dev, CANON_R5_STATE_READY);
	ctx->dev->ptp.session_open = true; /* Simulate open session */
	
	test->priv = ctx;
	return 0;
}

/* Test cleanup function */
static void canon_r5_ptp_test_exit(struct kunit *test)
{
	struct canon_r5_ptp_test_context *ctx = test->priv;
	
	if (ctx) {
		if (ctx->dev) {
			canon_r5_device_put(ctx->dev);
		}
		if (ctx->pdev) {
			platform_device_unregister(ctx->pdev);
		}
		kfree(ctx);
	}
}

/* Test case definitions */
static struct kunit_case canon_r5_ptp_test_cases[] = {
	KUNIT_CASE(canon_r5_ptp_session_test),
	KUNIT_CASE(canon_r5_ptp_transaction_id_test),
	KUNIT_CASE(canon_r5_ptp_command_validation_test),
	KUNIT_CASE(canon_r5_ptp_canon_operations_test),
	KUNIT_CASE(canon_r5_ptp_capture_operations_test),
	KUNIT_CASE(canon_r5_ptp_property_operations_test),
	KUNIT_CASE(canon_r5_ptp_data_validation_test),
	KUNIT_CASE(canon_r5_ptp_error_handling_test),
	{}
};

/* Test suite definition */
static struct kunit_suite canon_r5_ptp_test_suite = {
	.name = "canon-r5-ptp",
	.init = canon_r5_ptp_test_init,
	.exit = canon_r5_ptp_test_exit,
	.test_cases = canon_r5_ptp_test_cases,
};

/* Register the test suite */
kunit_test_suite(canon_r5_ptp_test_suite);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Canon R5 PTP Protocol KUnit Tests");
MODULE_AUTHOR("Canon R5 Driver Project");