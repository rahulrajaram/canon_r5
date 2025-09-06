/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Canon R5 Linux Driver Suite
 * Storage driver definitions (MTP/PTP filesystem)
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#ifndef __CANON_R5_STORAGE_H__
#define __CANON_R5_STORAGE_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>

/* Forward declarations */
struct canon_r5_device;
struct canon_r5_storage_device;

/* Maximum storage cards supported by R5 */
#define CANON_R5_MAX_STORAGE_CARDS	2

/* Storage card types */
enum canon_r5_storage_type {
	CANON_R5_STORAGE_NONE = 0,
	CANON_R5_STORAGE_CF_EXPRESS,	/* CFexpress Type B */
	CANON_R5_STORAGE_SD_CARD,	/* SD/SDHC/SDXC */
	CANON_R5_STORAGE_INTERNAL,	/* Internal storage */
	CANON_R5_STORAGE_TYPE_COUNT
};

/* Storage card status */
enum canon_r5_storage_status {
	CANON_R5_STORAGE_STATUS_EMPTY = 0,
	CANON_R5_STORAGE_STATUS_INSERTED,
	CANON_R5_STORAGE_STATUS_MOUNTED,
	CANON_R5_STORAGE_STATUS_ERROR,
	CANON_R5_STORAGE_STATUS_WRITE_PROTECTED,
	CANON_R5_STORAGE_STATUS_FULL,
	CANON_R5_STORAGE_STATUS_COUNT
};

/* File types supported by Canon R5 */
enum canon_r5_file_type {
	CANON_R5_FILE_UNKNOWN = 0,
	CANON_R5_FILE_JPEG,
	CANON_R5_FILE_RAW_CR3,
	CANON_R5_FILE_RAW_CR2,
	CANON_R5_FILE_HEIF,
	CANON_R5_FILE_MOV,
	CANON_R5_FILE_MP4,
	CANON_R5_FILE_WAV,
	CANON_R5_FILE_FOLDER,
	CANON_R5_FILE_TYPE_COUNT
};

/* Storage card information */
struct canon_r5_storage_card {
	int slot_id;
	enum canon_r5_storage_type type;
	enum canon_r5_storage_status status;
	
	/* Card specifications */
	char label[64];
	char serial_number[32];
	u64 total_capacity;		/* Total capacity in bytes */
	u64 free_space;			/* Free space in bytes */
	u32 write_speed;		/* Write speed in MB/s */
	u32 read_speed;			/* Read speed in MB/s */
	
	/* File system info */
	char filesystem[16];		/* FAT32, exFAT, etc. */
	u32 cluster_size;
	
	/* Status tracking */
	ktime_t last_access;
	u32 file_count;
	u32 folder_count;
	bool write_protected;
	bool needs_format;
};

/* File object information */
struct canon_r5_file_object {
	struct list_head list;
	struct rb_node rb_node;
	
	/* PTP object handle and parent */
	u32 object_handle;
	u32 parent_handle;
	
	/* File information */
	char filename[256];
	enum canon_r5_file_type file_type;
	u64 file_size;
	ktime_t creation_time;
	ktime_t modification_time;
	
	/* Storage location */
	int storage_id;
	u32 file_attributes;
	
	/* Metadata */
	struct {
		u32 image_width;
		u32 image_height;
		u32 video_duration;	/* in seconds */
		u32 video_bitrate;
		u16 iso_speed;
		char camera_model[64];
		char lens_model[64];
	} metadata;
	
	/* Reference counting and caching */
	struct kref ref_count;
	bool cached;
	void *cache_data;
	size_t cache_size;
};

/* Directory entry for filesystem */
struct canon_r5_dir_entry {
	struct list_head list;
	char name[256];
	u32 object_handle;
	enum canon_r5_file_type type;
	u64 size;
	ktime_t mtime;
	bool is_directory;
};

/* Filesystem superblock data */
struct canon_r5_fs_info {
	struct canon_r5_storage_device *storage;
	struct mutex lock;
	
	/* Root directory */
	struct rb_root file_tree;
	struct list_head file_list;
	spinlock_t file_lock;
	
	/* Directory cache */
	struct {
		struct list_head entries;
		u32 parent_handle;
		ktime_t cache_time;
		bool valid;
		struct mutex lock;
	} dir_cache;
	
	/* File cache management */
	struct {
		struct list_head lru_list;
		size_t total_size;
		size_t max_size;
		struct work_struct cleanup_work;
		struct workqueue_struct *cleanup_wq;
	} cache;
};

/* Storage device statistics */
struct canon_r5_storage_stats {
	u64 files_read;
	u64 files_written;
	u64 bytes_read;
	u64 bytes_written;
	u32 cache_hits;
	u32 cache_misses;
	u32 ptp_operations;
	u32 ptp_errors;
	ktime_t last_operation;
	
	/* Performance metrics */
	u32 avg_read_speed;	/* KB/s */
	u32 avg_write_speed;	/* KB/s */
	u32 avg_response_time;	/* microseconds */
};

/* Storage device structure */
struct canon_r5_storage_device {
	struct canon_r5_device *canon_dev;
	
	/* Device state */
	struct mutex lock;
	bool initialized;
	bool mounted;
	
	/* Storage cards */
	struct canon_r5_storage_card cards[CANON_R5_MAX_STORAGE_CARDS];
	int active_card;
	
	/* Filesystem interface */
	struct super_block *sb;
	struct canon_r5_fs_info *fs_info;
	
	/* PTP storage operations */
	struct {
		struct mutex lock;
		struct work_struct refresh_work;
		struct workqueue_struct *refresh_wq;
		ktime_t last_refresh;
	} ptp;
	
	/* Statistics */
	struct canon_r5_storage_stats stats;
	
	/* Event handling */
	struct {
		struct work_struct card_event_work;
		int event_card_slot;
		enum canon_r5_storage_status event_status;
	} events;
};

/* Storage driver private data */
struct canon_r5_storage {
	struct canon_r5_storage_device device;
	
	/* Filesystem registration */
	struct file_system_type fs_type;
	bool fs_registered;
	
	/* Mount management */
	struct {
		struct list_head mount_list;
		struct mutex lock;
		int mount_count;
	} mounts;
	
	/* Background operations */
	struct {
		struct workqueue_struct *wq;
		struct delayed_work sync_work;
		struct work_struct scan_work;
	} background;
};

/* API functions */
int canon_r5_storage_init(struct canon_r5_device *dev);
void canon_r5_storage_cleanup(struct canon_r5_device *dev);

/* Storage card management */
int canon_r5_storage_scan_cards(struct canon_r5_storage_device *storage);
int canon_r5_storage_mount_card(struct canon_r5_storage_device *storage, int slot);
int canon_r5_storage_unmount_card(struct canon_r5_storage_device *storage, int slot);
int canon_r5_storage_format_card(struct canon_r5_storage_device *storage, int slot);

/* File operations */
struct canon_r5_file_object *canon_r5_storage_get_file(struct canon_r5_storage_device *storage,
						       u32 object_handle);
void canon_r5_storage_put_file(struct canon_r5_file_object *file);
int canon_r5_storage_read_file(struct canon_r5_storage_device *storage,
			       struct canon_r5_file_object *file,
			       void *buffer, size_t size, loff_t offset,
			       size_t *bytes_read);
int canon_r5_storage_write_file(struct canon_r5_storage_device *storage,
				const char *filename, const void *buffer,
				size_t size, struct canon_r5_file_object **new_file);
int canon_r5_storage_delete_file(struct canon_r5_storage_device *storage,
				 struct canon_r5_file_object *file);

/* Directory operations */
int canon_r5_storage_list_directory(struct canon_r5_storage_device *storage,
				    u32 parent_handle,
				    struct list_head *entries);
int canon_r5_storage_create_directory(struct canon_r5_storage_device *storage,
				      const char *dirname, u32 parent_handle,
				      u32 *new_handle);

/* Filesystem operations */
extern const struct super_operations canon_r5_storage_super_ops;
extern const struct inode_operations canon_r5_storage_dir_inode_ops;
extern const struct inode_operations canon_r5_storage_file_inode_ops;
extern const struct file_operations canon_r5_storage_dir_file_ops;
extern const struct file_operations canon_r5_storage_file_ops;
extern const struct address_space_operations canon_r5_storage_aops;

/* Cache management */
int canon_r5_storage_cache_file(struct canon_r5_storage_device *storage,
			       struct canon_r5_file_object *file);
void canon_r5_storage_cache_cleanup(struct canon_r5_storage_device *storage);
void canon_r5_storage_cache_invalidate(struct canon_r5_storage_device *storage);

/* Statistics */
int canon_r5_storage_get_stats(struct canon_r5_storage_device *storage,
			       struct canon_r5_storage_stats *stats);
void canon_r5_storage_reset_stats(struct canon_r5_storage_device *storage);

/* Internal functions */
void canon_r5_storage_refresh_work(struct work_struct *work);
void canon_r5_storage_card_event_work(struct work_struct *work);
void canon_r5_storage_cache_cleanup_work(struct work_struct *work);
void canon_r5_storage_sync_work(struct work_struct *work);

/* PTP storage commands */
int canon_r5_ptp_get_storage_ids(struct canon_r5_device *dev, u32 *storage_ids, int max_ids);
int canon_r5_ptp_get_storage_info(struct canon_r5_device *dev, u32 storage_id,
				  struct canon_r5_storage_card *info);
int canon_r5_ptp_get_object_handles(struct canon_r5_device *dev, u32 storage_id,
				    u32 parent_handle, u32 *handles, int max_handles);
int canon_r5_ptp_get_object_info(struct canon_r5_device *dev, u32 object_handle,
				 struct canon_r5_file_object *info);
int canon_r5_ptp_get_object_data(struct canon_r5_device *dev, u32 object_handle,
				 void *buffer, size_t size, size_t offset,
				 size_t *bytes_read);
int canon_r5_ptp_send_object_data(struct canon_r5_device *dev, const char *filename,
				  const void *buffer, size_t size, u32 parent_handle,
				  u32 *new_handle);
int canon_r5_ptp_delete_object(struct canon_r5_device *dev, u32 object_handle);
int canon_r5_ptp_format_storage(struct canon_r5_device *dev, u32 storage_id);

/* Debugging */
#define canon_r5_storage_dbg(storage, fmt, ...) \
	dev_dbg((storage)->canon_dev->dev, "[STORAGE] " fmt, ##__VA_ARGS__)

#define canon_r5_storage_info(storage, fmt, ...) \
	dev_info((storage)->canon_dev->dev, "[STORAGE] " fmt, ##__VA_ARGS__)

#define canon_r5_storage_warn(storage, fmt, ...) \
	dev_warn((storage)->canon_dev->dev, "[STORAGE] " fmt, ##__VA_ARGS__)

#define canon_r5_storage_err(storage, fmt, ...) \
	dev_err((storage)->canon_dev->dev, "[STORAGE] " fmt, ##__VA_ARGS__)

/* Helper functions */
const char *canon_r5_storage_type_name(enum canon_r5_storage_type type);
const char *canon_r5_storage_status_name(enum canon_r5_storage_status status);
const char *canon_r5_file_type_name(enum canon_r5_file_type type);

/* Validation functions */
bool canon_r5_storage_type_valid(enum canon_r5_storage_type type);
bool canon_r5_storage_slot_valid(int slot);
bool canon_r5_file_type_valid(enum canon_r5_file_type type);

/* Utility functions */
enum canon_r5_file_type canon_r5_storage_detect_file_type(const char *filename);
u64 canon_r5_storage_get_free_space(struct canon_r5_storage_device *storage, int slot);
bool canon_r5_storage_is_write_protected(struct canon_r5_storage_device *storage, int slot);

/* Mount/unmount interface */
int canon_r5_storage_register_filesystem(struct canon_r5_storage *storage);
void canon_r5_storage_unregister_filesystem(struct canon_r5_storage *storage);

#endif /* __CANON_R5_STORAGE_H__ */