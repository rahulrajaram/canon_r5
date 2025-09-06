/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite - Storage Driver Unit Tests
 * KUnit tests for storage and filesystem functionality
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <kunit/test.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/dcache.h>

#include "storage/canon-r5-storage.h"
#include "core/canon-r5.h"

/**
 * Test context structure for storage tests
 */
struct canon_r5_storage_test_ctx {
	struct platform_device *pdev;
	struct canon_r5_device *canon_dev;
	struct canon_r5_storage_device *storage_dev;
	struct canon_r5_storage_card test_card;
};

/* Test fixture setup */
static int canon_r5_storage_test_init(struct kunit *test)
{
	struct canon_r5_storage_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/* Create mock platform device */
	ctx->pdev = platform_device_alloc("canon-r5-test", 0);
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

	/* Create storage device */
	ctx->storage_dev = kunit_kzalloc(test, sizeof(*ctx->storage_dev), GFP_KERNEL);
	if (!ctx->storage_dev) {
		platform_device_unregister(ctx->pdev);
		return -ENOMEM;
	}

	ctx->storage_dev->canon_dev = ctx->canon_dev;
	mutex_init(&ctx->storage_dev->lock);

	/* Initialize test card data */
	ctx->test_card.slot_id = 0;
	ctx->test_card.type = CANON_R5_STORAGE_CF_EXPRESS;
	ctx->test_card.status = CANON_R5_STORAGE_STATUS_INSERTED;
	strcpy(ctx->test_card.label, "TEST_CARD");
	strcpy(ctx->test_card.serial_number, "TEST123456789");
	ctx->test_card.total_capacity = 128ULL * 1024 * 1024 * 1024; /* 128GB */
	ctx->test_card.free_space = 64ULL * 1024 * 1024 * 1024;     /* 64GB free */
	ctx->test_card.write_speed = 1700; /* MB/s */
	ctx->test_card.read_speed = 1800;  /* MB/s */
	strcpy(ctx->test_card.filesystem, "exFAT");
	ctx->test_card.cluster_size = 131072; /* 128KB */
	ctx->test_card.file_count = 1000;
	ctx->test_card.folder_count = 50;
	ctx->test_card.write_protected = false;
	ctx->test_card.needs_format = false;

	test->priv = ctx;
	return 0;
}

/* Test fixture teardown */
static void canon_r5_storage_test_exit(struct kunit *test)
{
	struct canon_r5_storage_test_ctx *ctx = test->priv;

	if (ctx && ctx->pdev)
		platform_device_unregister(ctx->pdev);
}

/* Test storage type validation */
static void canon_r5_storage_type_validation_test(struct kunit *test)
{
	/* Valid types */
	KUNIT_EXPECT_TRUE(test, canon_r5_storage_type_valid(CANON_R5_STORAGE_CF_EXPRESS));
	KUNIT_EXPECT_TRUE(test, canon_r5_storage_type_valid(CANON_R5_STORAGE_SD_CARD));
	KUNIT_EXPECT_TRUE(test, canon_r5_storage_type_valid(CANON_R5_STORAGE_INTERNAL));

	/* Invalid types */
	KUNIT_EXPECT_FALSE(test, canon_r5_storage_type_valid(CANON_R5_STORAGE_NONE));
	KUNIT_EXPECT_FALSE(test, canon_r5_storage_type_valid(CANON_R5_STORAGE_TYPE_COUNT));
	KUNIT_EXPECT_FALSE(test, canon_r5_storage_type_valid(999));
}

/* Test storage slot validation */
static void canon_r5_storage_slot_validation_test(struct kunit *test)
{
	/* Valid slots */
	KUNIT_EXPECT_TRUE(test, canon_r5_storage_slot_valid(0));
	KUNIT_EXPECT_TRUE(test, canon_r5_storage_slot_valid(1));

	/* Invalid slots */
	KUNIT_EXPECT_FALSE(test, canon_r5_storage_slot_valid(-1));
	KUNIT_EXPECT_FALSE(test, canon_r5_storage_slot_valid(CANON_R5_MAX_STORAGE_CARDS));
	KUNIT_EXPECT_FALSE(test, canon_r5_storage_slot_valid(999));
}

/* Test file type detection */
static void canon_r5_storage_file_type_detection_test(struct kunit *test)
{
	/* Image files */
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("IMG_0001.JPG"), CANON_R5_FILE_JPEG);
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("photo.jpeg"), CANON_R5_FILE_JPEG);
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("RAW_0001.CR3"), CANON_R5_FILE_RAW_CR3);
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("old_raw.cr2"), CANON_R5_FILE_RAW_CR2);
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("image.heic"), CANON_R5_FILE_HEIF);

	/* Video files */
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("MVI_0001.MOV"), CANON_R5_FILE_MOV);
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("video.mp4"), CANON_R5_FILE_MP4);

	/* Audio files */
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("audio.wav"), CANON_R5_FILE_WAV);

	/* Unknown files */
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("document.txt"), CANON_R5_FILE_UNKNOWN);
	KUNIT_EXPECT_EQ(test, canon_r5_storage_detect_file_type("noext"), CANON_R5_FILE_UNKNOWN);
}

/* Test storage card information */
static void canon_r5_storage_card_info_test(struct kunit *test)
{
	struct canon_r5_storage_test_ctx *ctx = test->priv;
	struct canon_r5_storage_card *card = &ctx->test_card;

	/* Verify card properties */
	KUNIT_EXPECT_EQ(test, card->slot_id, 0);
	KUNIT_EXPECT_EQ(test, card->type, CANON_R5_STORAGE_CF_EXPRESS);
	KUNIT_EXPECT_EQ(test, card->status, CANON_R5_STORAGE_STATUS_INSERTED);
	KUNIT_EXPECT_STREQ(test, card->label, "TEST_CARD");
	KUNIT_EXPECT_STREQ(test, card->serial_number, "TEST123456789");
	KUNIT_EXPECT_EQ(test, card->total_capacity, 128ULL * 1024 * 1024 * 1024);
	KUNIT_EXPECT_EQ(test, card->free_space, 64ULL * 1024 * 1024 * 1024);
	KUNIT_EXPECT_EQ(test, card->write_speed, 1700);
	KUNIT_EXPECT_EQ(test, card->read_speed, 1800);
	KUNIT_EXPECT_STREQ(test, card->filesystem, "exFAT");
	KUNIT_EXPECT_EQ(test, card->cluster_size, 131072);
	KUNIT_EXPECT_FALSE(test, card->write_protected);
	KUNIT_EXPECT_FALSE(test, card->needs_format);
}

/* Test free space calculation */
static void canon_r5_storage_free_space_test(struct kunit *test)
{
	struct canon_r5_storage_test_ctx *ctx = test->priv;
	u64 free_space;

	/* Setup card in storage device */
	ctx->storage_dev->cards[0] = ctx->test_card;
	ctx->storage_dev->active_card = 0;

	/* Test free space retrieval */
	free_space = canon_r5_storage_get_free_space(ctx->storage_dev, 0);
	KUNIT_EXPECT_EQ(test, free_space, 64ULL * 1024 * 1024 * 1024);

	/* Test invalid slot */
	free_space = canon_r5_storage_get_free_space(ctx->storage_dev, -1);
	KUNIT_EXPECT_EQ(test, free_space, 0);

	free_space = canon_r5_storage_get_free_space(ctx->storage_dev, CANON_R5_MAX_STORAGE_CARDS);
	KUNIT_EXPECT_EQ(test, free_space, 0);
}

/* Test write protection status */
static void canon_r5_storage_write_protection_test(struct kunit *test)
{
	struct canon_r5_storage_test_ctx *ctx = test->priv;

	/* Setup card in storage device */
	ctx->storage_dev->cards[0] = ctx->test_card;

	/* Test normal card (not write protected) */
	KUNIT_EXPECT_FALSE(test, canon_r5_storage_is_write_protected(ctx->storage_dev, 0));

	/* Test write protected card */
	ctx->storage_dev->cards[0].write_protected = true;
	KUNIT_EXPECT_TRUE(test, canon_r5_storage_is_write_protected(ctx->storage_dev, 0));

	/* Test invalid slot */
	KUNIT_EXPECT_TRUE(test, canon_r5_storage_is_write_protected(ctx->storage_dev, -1));
	KUNIT_EXPECT_TRUE(test, canon_r5_storage_is_write_protected(ctx->storage_dev, CANON_R5_MAX_STORAGE_CARDS));
}

/* Test file object creation and management */
static void canon_r5_storage_file_object_test(struct kunit *test)
{
	struct canon_r5_file_object *file;

	/* Create file object */
	file = kunit_kzalloc(test, sizeof(*file), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, file);

	/* Initialize file object */
	INIT_LIST_HEAD(&file->list);
	file->object_handle = 0x12345678;
	file->parent_handle = 0x87654321;
	strcpy(file->filename, "IMG_0001.CR3");
	file->file_type = CANON_R5_FILE_RAW_CR3;
	file->file_size = 45 * 1024 * 1024; /* 45MB RAW file */
	file->storage_id = 1;
	file->file_attributes = 0x20; /* Archive bit */

	/* Test metadata */
	file->metadata.image_width = 8192;
	file->metadata.image_height = 5464;
	file->metadata.iso_speed = 800;
	strcpy(file->metadata.camera_model, "Canon EOS R5");
	strcpy(file->metadata.lens_model, "RF24-105mm F4 L IS USM");

	/* Verify file object properties */
	KUNIT_EXPECT_EQ(test, file->object_handle, 0x12345678);
	KUNIT_EXPECT_EQ(test, file->parent_handle, 0x87654321);
	KUNIT_EXPECT_STREQ(test, file->filename, "IMG_0001.CR3");
	KUNIT_EXPECT_EQ(test, file->file_type, CANON_R5_FILE_RAW_CR3);
	KUNIT_EXPECT_EQ(test, file->file_size, 45 * 1024 * 1024);
	KUNIT_EXPECT_EQ(test, file->storage_id, 1);
	KUNIT_EXPECT_EQ(test, file->metadata.image_width, 8192);
	KUNIT_EXPECT_EQ(test, file->metadata.image_height, 5464);
	KUNIT_EXPECT_EQ(test, file->metadata.iso_speed, 800);
	KUNIT_EXPECT_STREQ(test, file->metadata.camera_model, "Canon EOS R5");
	KUNIT_EXPECT_STREQ(test, file->metadata.lens_model, "RF24-105mm F4 L IS USM");

	/* Initialize reference counting */
	kref_init(&file->ref_count);
	KUNIT_EXPECT_EQ(test, kref_read(&file->ref_count), 1);
}

/* Test directory entry management */
static void canon_r5_storage_directory_entry_test(struct kunit *test)
{
	struct canon_r5_dir_entry *entry;
	struct list_head dir_list;

	INIT_LIST_HEAD(&dir_list);

	/* Create directory entry */
	entry = kunit_kzalloc(test, sizeof(*entry), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entry);

	/* Initialize directory entry */
	INIT_LIST_HEAD(&entry->list);
	strcpy(entry->name, "DCIM");
	entry->object_handle = 0x00000001;
	entry->type = CANON_R5_FILE_FOLDER;
	entry->size = 0;
	entry->is_directory = true;

	/* Add to directory list */
	list_add(&entry->list, &dir_list);

	/* Verify entry properties */
	KUNIT_EXPECT_STREQ(test, entry->name, "DCIM");
	KUNIT_EXPECT_EQ(test, entry->object_handle, 0x00000001);
	KUNIT_EXPECT_EQ(test, entry->type, CANON_R5_FILE_FOLDER);
	KUNIT_EXPECT_EQ(test, entry->size, 0);
	KUNIT_EXPECT_TRUE(test, entry->is_directory);

	/* Verify list operations */
	KUNIT_EXPECT_FALSE(test, list_empty(&dir_list));
	KUNIT_EXPECT_PTR_EQ(test, list_first_entry(&dir_list, struct canon_r5_dir_entry, list), entry);
}

/* Test storage statistics */
static void canon_r5_storage_stats_test(struct kunit *test)
{
	struct canon_r5_storage_test_ctx *ctx = test->priv;
	struct canon_r5_storage_stats stats;

	/* Initialize statistics */
	memset(&ctx->storage_dev->stats, 0, sizeof(ctx->storage_dev->stats));
	ctx->storage_dev->stats.files_read = 100;
	ctx->storage_dev->stats.files_written = 25;
	ctx->storage_dev->stats.bytes_read = 1024 * 1024 * 1024; /* 1GB */
	ctx->storage_dev->stats.bytes_written = 256 * 1024 * 1024; /* 256MB */
	ctx->storage_dev->stats.cache_hits = 150;
	ctx->storage_dev->stats.cache_misses = 50;
	ctx->storage_dev->stats.ptp_operations = 200;
	ctx->storage_dev->stats.ptp_errors = 5;
	ctx->storage_dev->stats.avg_read_speed = 1500; /* KB/s */
	ctx->storage_dev->stats.avg_write_speed = 1200; /* KB/s */
	ctx->storage_dev->stats.avg_response_time = 15; /* microseconds */

	/* Get statistics */
	memset(&stats, 0, sizeof(stats));
	int ret = canon_r5_storage_get_stats(ctx->storage_dev, &stats);

	/* Verify statistics retrieval */
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, stats.files_read, 100);
	KUNIT_EXPECT_EQ(test, stats.files_written, 25);
	KUNIT_EXPECT_EQ(test, stats.bytes_read, 1024 * 1024 * 1024);
	KUNIT_EXPECT_EQ(test, stats.bytes_written, 256 * 1024 * 1024);
	KUNIT_EXPECT_EQ(test, stats.cache_hits, 150);
	KUNIT_EXPECT_EQ(test, stats.cache_misses, 50);
	KUNIT_EXPECT_EQ(test, stats.ptp_operations, 200);
	KUNIT_EXPECT_EQ(test, stats.ptp_errors, 5);
	KUNIT_EXPECT_EQ(test, stats.avg_read_speed, 1500);
	KUNIT_EXPECT_EQ(test, stats.avg_write_speed, 1200);
	KUNIT_EXPECT_EQ(test, stats.avg_response_time, 15);

	/* Test statistics reset */
	canon_r5_storage_reset_stats(ctx->storage_dev);
	ret = canon_r5_storage_get_stats(ctx->storage_dev, &stats);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, stats.files_read, 0);
	KUNIT_EXPECT_EQ(test, stats.files_written, 0);
	KUNIT_EXPECT_EQ(test, stats.bytes_read, 0);
	KUNIT_EXPECT_EQ(test, stats.bytes_written, 0);
}

/* Test storage type name conversion */
static void canon_r5_storage_type_names_test(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_type_name(CANON_R5_STORAGE_CF_EXPRESS), "CFexpress");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_type_name(CANON_R5_STORAGE_SD_CARD), "SD Card");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_type_name(CANON_R5_STORAGE_INTERNAL), "Internal");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_type_name(CANON_R5_STORAGE_NONE), "None");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_type_name(999), "Unknown");
}

/* Test storage status name conversion */
static void canon_r5_storage_status_names_test(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_status_name(CANON_R5_STORAGE_STATUS_EMPTY), "Empty");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_status_name(CANON_R5_STORAGE_STATUS_INSERTED), "Inserted");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_status_name(CANON_R5_STORAGE_STATUS_MOUNTED), "Mounted");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_status_name(CANON_R5_STORAGE_STATUS_ERROR), "Error");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_status_name(CANON_R5_STORAGE_STATUS_WRITE_PROTECTED), "Write Protected");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_status_name(CANON_R5_STORAGE_STATUS_FULL), "Full");
	KUNIT_EXPECT_STREQ(test, canon_r5_storage_status_name(999), "Unknown");
}

/* Test file type name conversion */
static void canon_r5_storage_file_type_names_test(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_JPEG), "JPEG");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_RAW_CR3), "Canon RAW v3");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_RAW_CR2), "Canon RAW v2");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_HEIF), "HEIF");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_MOV), "QuickTime Movie");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_MP4), "MPEG-4");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_WAV), "WAV Audio");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_FOLDER), "Folder");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(CANON_R5_FILE_UNKNOWN), "Unknown");
	KUNIT_EXPECT_STREQ(test, canon_r5_file_type_name(999), "Unknown");
}

/* KUnit test suite definition */
static struct kunit_case canon_r5_storage_test_cases[] = {
	KUNIT_CASE(canon_r5_storage_type_validation_test),
	KUNIT_CASE(canon_r5_storage_slot_validation_test),
	KUNIT_CASE(canon_r5_storage_file_type_detection_test),
	KUNIT_CASE(canon_r5_storage_card_info_test),
	KUNIT_CASE(canon_r5_storage_free_space_test),
	KUNIT_CASE(canon_r5_storage_write_protection_test),
	KUNIT_CASE(canon_r5_storage_file_object_test),
	KUNIT_CASE(canon_r5_storage_directory_entry_test),
	KUNIT_CASE(canon_r5_storage_stats_test),
	KUNIT_CASE(canon_r5_storage_type_names_test),
	KUNIT_CASE(canon_r5_storage_status_names_test),
	KUNIT_CASE(canon_r5_storage_file_type_names_test),
	{}
};

static struct kunit_suite canon_r5_storage_test_suite = {
	.name = "canon_r5_storage",
	.init = canon_r5_storage_test_init,
	.exit = canon_r5_storage_test_exit,
	.test_cases = canon_r5_storage_test_cases,
};

kunit_test_suite(canon_r5_storage_test_suite);

MODULE_DESCRIPTION("Canon R5 Storage Driver Unit Tests");
MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_LICENSE("GPL v2");