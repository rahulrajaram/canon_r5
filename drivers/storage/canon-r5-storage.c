// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * Storage driver implementation (MTP/PTP filesystem)
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/uaccess.h>
#include <linux/time.h>

#include "../../include/core/canon-r5.h"
#include "../../include/core/canon-r5-ptp.h"
#include "../../include/storage/canon-r5-storage.h"

/* Module information */
MODULE_AUTHOR("Canon R5 Driver Project");
MODULE_DESCRIPTION("Canon R5 Storage Driver (MTP/PTP Filesystem)");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");

/* Filesystem constants */
#define CANON_R5_FS_NAME		"canon_r5_fs"
#define CANON_R5_FS_MAGIC		0x43355235  /* "C5R5" */
#define CANON_R5_CACHE_MAX_SIZE		(64 * 1024 * 1024)  /* 64MB */
#define CANON_R5_CACHE_TIMEOUT		(300 * HZ)  /* 5 minutes */

/* Helper functions for validation and naming */
bool canon_r5_storage_type_valid(enum canon_r5_storage_type type)
{
	return type > CANON_R5_STORAGE_NONE && type < CANON_R5_STORAGE_TYPE_COUNT;
}

bool canon_r5_storage_slot_valid(int slot)
{
	return slot >= 0 && slot < CANON_R5_MAX_STORAGE_CARDS;
}

bool canon_r5_file_type_valid(enum canon_r5_file_type type)
{
	return type >= CANON_R5_FILE_UNKNOWN && type < CANON_R5_FILE_TYPE_COUNT;
}

const char *canon_r5_storage_type_name(enum canon_r5_storage_type type)
{
	static const char *names[] = {
		"None",
		"CFexpress Type B",
		"SD Card",
		"Internal Storage"
	};
	
	if (canon_r5_storage_type_valid(type))
		return names[type];
	return "Unknown";
}

const char *canon_r5_storage_status_name(enum canon_r5_storage_status status)
{
	static const char *names[] = {
		"Empty",
		"Inserted",
		"Mounted",
		"Error",
		"Write Protected",
		"Full"
	};
	
	if (status < CANON_R5_STORAGE_STATUS_COUNT)
		return names[status];
	return "Unknown";
}

const char *canon_r5_file_type_name(enum canon_r5_file_type type)
{
	static const char *names[] = {
		"Unknown",
		"JPEG",
		"RAW CR3",
		"RAW CR2", 
		"HEIF",
		"MOV",
		"MP4",
		"WAV",
		"Folder"
	};
	
	if (canon_r5_file_type_valid(type))
		return names[type];
	return "Unknown";
}

enum canon_r5_file_type canon_r5_storage_detect_file_type(const char *filename)
{
	const char *ext = strrchr(filename, '.');
	if (!ext)
		return CANON_R5_FILE_UNKNOWN;
		
	ext++; /* Skip the dot */
	
	if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0)
		return CANON_R5_FILE_JPEG;
	else if (strcasecmp(ext, "cr3") == 0)
		return CANON_R5_FILE_RAW_CR3;
	else if (strcasecmp(ext, "cr2") == 0)
		return CANON_R5_FILE_RAW_CR2;
	else if (strcasecmp(ext, "heic") == 0 || strcasecmp(ext, "heif") == 0)
		return CANON_R5_FILE_HEIF;
	else if (strcasecmp(ext, "mov") == 0)
		return CANON_R5_FILE_MOV;
	else if (strcasecmp(ext, "mp4") == 0)
		return CANON_R5_FILE_MP4;
	else if (strcasecmp(ext, "wav") == 0)
		return CANON_R5_FILE_WAV;
	
	return CANON_R5_FILE_UNKNOWN;
}

/* File object management */
static void canon_r5_file_object_release(struct kref *kref)
{
	struct canon_r5_file_object *file = container_of(kref, struct canon_r5_file_object, ref_count);
	
	if (file->cache_data) {
		vfree(file->cache_data);
		file->cache_data = NULL;
	}
	
	kfree(file);
}

struct canon_r5_file_object *canon_r5_storage_get_file(struct canon_r5_storage_device *storage,
						       u32 object_handle)
{
	struct canon_r5_file_object *file;
	struct rb_node *node;
	unsigned long flags;
	
	if (!storage || !storage->fs_info)
		return NULL;
		
	spin_lock_irqsave(&storage->fs_info->file_lock, flags);
	
	node = storage->fs_info->file_tree.rb_node;
	while (node) {
		file = rb_entry(node, struct canon_r5_file_object, rb_node);
		
		if (object_handle < file->object_handle)
			node = node->rb_left;
		else if (object_handle > file->object_handle)
			node = node->rb_right;
		else {
			kref_get(&file->ref_count);
			spin_unlock_irqrestore(&storage->fs_info->file_lock, flags);
			return file;
		}
	}
	
	spin_unlock_irqrestore(&storage->fs_info->file_lock, flags);
	return NULL;
}

void canon_r5_storage_put_file(struct canon_r5_file_object *file)
{
	if (file)
		kref_put(&file->ref_count, canon_r5_file_object_release);
}

/* PTP storage command stubs */
int canon_r5_ptp_get_storage_ids(struct canon_r5_device *dev, u32 *storage_ids, int max_ids)
{
	u8 response[64];
	u16 response_code = 0;
	int ret;
	
	ret = canon_r5_ptp_command(dev, 0x1004, NULL, 0, response, sizeof(response), &response_code);
	if (ret)
		return ret;
		
	if (response_code == 0x2001 && max_ids >= 2) {
		/* Simulate two storage IDs for CF and SD slots */
		storage_ids[0] = 0x00010001; /* CF slot */
		storage_ids[1] = 0x00020001; /* SD slot */
		return 2;
	}
	
	return ret;
}

int canon_r5_ptp_get_storage_info(struct canon_r5_device *dev, u32 storage_id,
				  struct canon_r5_storage_card *info)
{
	u8 response[128];
	u16 response_code = 0;
	int ret;
	
	ret = canon_r5_ptp_command(dev, 0x1005, &storage_id, 1, response, sizeof(response), &response_code);
	if (ret)
		return ret;
		
	if (response_code == 0x2001) {
		/* Parse storage information from response */
		memset(info, 0, sizeof(*info));
		info->slot_id = (storage_id == 0x00010001) ? 0 : 1;
		info->type = (storage_id == 0x00010001) ? CANON_R5_STORAGE_CF_EXPRESS : CANON_R5_STORAGE_SD_CARD;
		info->status = CANON_R5_STORAGE_STATUS_MOUNTED;
		info->total_capacity = 128ULL * 1024 * 1024 * 1024; /* 128GB default */
		info->free_space = 64ULL * 1024 * 1024 * 1024; /* 64GB free */
		strncpy(info->label, "CANON_R5", sizeof(info->label) - 1);
		strncpy(info->filesystem, "exFAT", sizeof(info->filesystem) - 1);
	}
	
	return ret;
}

int canon_r5_ptp_get_object_handles(struct canon_r5_device *dev, u32 storage_id,
				    u32 parent_handle, u32 *handles, int max_handles)
{
	u32 params[3] = { storage_id, 0x00000000, parent_handle }; /* All file formats */
	u8 response[1024];
	u16 response_code = 0;
	int ret;
	
	ret = canon_r5_ptp_command(dev, 0x1007, params, 3, response, sizeof(response), &response_code);
	if (ret)
		return ret;
		
	if (response_code == 0x2001) {
		/* Parse object handles from response - simulate some files */
		int count = min(max_handles, 10);
		for (int i = 0; i < count; i++) {
			handles[i] = 0x00010000 + i + 1; /* Generate handle IDs */
		}
		return count;
	}
	
	return 0;
}

int canon_r5_ptp_get_object_info(struct canon_r5_device *dev, u32 object_handle,
				 struct canon_r5_file_object *info)
{
	u8 response[256];
	u16 response_code = 0;
	int ret;
	
	ret = canon_r5_ptp_command(dev, 0x1008, &object_handle, 1, response, sizeof(response), &response_code);
	if (ret)
		return ret;
		
	if (response_code == 0x2001) {
		/* Parse object information from response */
		memset(info, 0, sizeof(*info));
		info->object_handle = object_handle;
		info->parent_handle = 0x00000000; /* Root directory */
		snprintf(info->filename, sizeof(info->filename), "IMG_%04u.CR3", object_handle & 0xFFFF);
		info->file_type = CANON_R5_FILE_RAW_CR3;
		info->file_size = 50 * 1024 * 1024; /* 50MB default */
		info->creation_time = ktime_get();
		info->modification_time = info->creation_time;
		info->storage_id = (object_handle >> 16) & 0xFF;
		kref_init(&info->ref_count);
	}
	
	return ret;
}

int canon_r5_ptp_get_object_data(struct canon_r5_device *dev, u32 object_handle,
				 void *buffer, size_t size, size_t offset,
				 size_t *bytes_read)
{
	u8 response[1024];
	u16 response_code = 0;
	int ret;
	
	/* For this stub implementation, we'll simulate reading file data */
	ret = canon_r5_ptp_command(dev, 0x1009, &object_handle, 1, response, sizeof(response), &response_code);
	if (ret)
		return ret;
		
	if (response_code == 0x2001) {
		/* Simulate file data by filling buffer with pattern */
		size_t to_read = min(size, sizeof(response));
		memset(buffer, 0x55, to_read); /* Fill with pattern */
		*bytes_read = to_read;
	}
	
	return ret;
}

int canon_r5_ptp_send_object_data(struct canon_r5_device *dev, const char *filename,
				  const void *buffer, size_t size, u32 parent_handle,
				  u32 *new_handle)
{
	u32 params[2] = { parent_handle, (u32)size };
	u8 response[64];
	u16 response_code = 0;
	int ret;
	
	/* This is a stub - real implementation would send the file data */
	ret = canon_r5_ptp_command(dev, 0x100C, params, 2, response, sizeof(response), &response_code);
	if (ret)
		return ret;
		
	if (response_code == 0x2001) {
		*new_handle = 0x00010000 + (u32)ktime_get_ns(); /* Generate new handle */
	}
	
	return ret;
}

int canon_r5_ptp_delete_object(struct canon_r5_device *dev, u32 object_handle)
{
	u16 response_code = 0;
	return canon_r5_ptp_command(dev, 0x100A, &object_handle, 1, NULL, 0, &response_code);
}

int canon_r5_ptp_format_storage(struct canon_r5_device *dev, u32 storage_id)
{
	u16 response_code = 0;
	return canon_r5_ptp_command(dev, 0x100F, &storage_id, 1, NULL, 0, &response_code);
}

/* Work functions */
void canon_r5_storage_refresh_work(struct work_struct *work)
{
	struct canon_r5_storage_device *storage = container_of(work, struct canon_r5_storage_device, ptp.refresh_work);
	
	canon_r5_storage_dbg(storage, "Refreshing storage information");
	
	mutex_lock(&storage->lock);
	
	/* Refresh storage card information */
	for (int i = 0; i < CANON_R5_MAX_STORAGE_CARDS; i++) {
		if (storage->cards[i].status == CANON_R5_STORAGE_STATUS_MOUNTED) {
			u32 storage_id = (i == 0) ? 0x00010001 : 0x00020001;
			canon_r5_ptp_get_storage_info(storage->canon_dev, storage_id, &storage->cards[i]);
		}
	}
	
	storage->ptp.last_refresh = ktime_get();
	
	mutex_unlock(&storage->lock);
}

void canon_r5_storage_card_event_work(struct work_struct *work)
{
	struct canon_r5_storage_device *storage = container_of(work, struct canon_r5_storage_device, events.card_event_work);
	int slot = storage->events.event_card_slot;
	enum canon_r5_storage_status status = storage->events.event_status;
	
	canon_r5_storage_info(storage, "Storage card event: slot %d, status %s", 
			      slot, canon_r5_storage_status_name(status));
	
	mutex_lock(&storage->lock);
	
	if (canon_r5_storage_slot_valid(slot)) {
		storage->cards[slot].status = status;
		
		if (status == CANON_R5_STORAGE_STATUS_INSERTED) {
			/* Auto-mount inserted cards */
			canon_r5_storage_mount_card(storage, slot);
		} else if (status == CANON_R5_STORAGE_STATUS_EMPTY) {
			/* Clean up when card is removed */
			canon_r5_storage_unmount_card(storage, slot);
		}
	}
	
	mutex_unlock(&storage->lock);
}

void canon_r5_storage_cache_cleanup_work(struct work_struct *work)
{
	struct canon_r5_fs_info *fs_info = container_of(work, struct canon_r5_fs_info, cache.cleanup_work);
	struct canon_r5_file_object *file, *tmp;
	ktime_t now = ktime_get();
	ktime_t timeout = ktime_sub(now, ns_to_ktime(CANON_R5_CACHE_TIMEOUT));
	
	mutex_lock(&fs_info->lock);
	
	list_for_each_entry_safe(file, tmp, &fs_info->cache.lru_list, list) {
		if (file->cached && ktime_before(file->modification_time, timeout)) {
			if (file->cache_data) {
				vfree(file->cache_data);
				file->cache_data = NULL;
				file->cache_size = 0;
				file->cached = false;
				fs_info->cache.total_size -= file->cache_size;
			}
		}
	}
	
	mutex_unlock(&fs_info->lock);
}

void canon_r5_storage_sync_work(struct work_struct *work)
{
	struct canon_r5_storage *storage = container_of(work, struct canon_r5_storage, background.sync_work.work);
	
	/* Periodic sync and maintenance operations */
	canon_r5_storage_dbg(&storage->device, "Performing background sync");
	
	/* Schedule next sync in 30 seconds */
	queue_delayed_work(storage->background.wq, &storage->background.sync_work, 30 * HZ);
}

/* Storage card management */
int canon_r5_storage_scan_cards(struct canon_r5_storage_device *storage)
{
	u32 storage_ids[8];
	int count, i;
	int ret;
	
	if (!storage)
		return -EINVAL;
		
	mutex_lock(&storage->lock);
	
	/* Get available storage IDs from camera */
	count = canon_r5_ptp_get_storage_ids(storage->canon_dev, storage_ids, ARRAY_SIZE(storage_ids));
	if (count < 0) {
		ret = count;
		goto unlock;
	}
	
	/* Update storage card information */
	for (i = 0; i < count && i < CANON_R5_MAX_STORAGE_CARDS; i++) {
		ret = canon_r5_ptp_get_storage_info(storage->canon_dev, storage_ids[i], &storage->cards[i]);
		if (ret) {
			canon_r5_storage_warn(storage, "Failed to get storage info for slot %d: %d", i, ret);
			continue;
		}
		
		storage->cards[i].last_access = ktime_get();
		canon_r5_storage_info(storage, "Found storage card: slot %d, type %s, capacity %llu MB",
				      i, canon_r5_storage_type_name(storage->cards[i].type),
				      storage->cards[i].total_capacity / (1024 * 1024));
	}
	
	ret = count;
	
unlock:
	mutex_unlock(&storage->lock);
	return ret;
}

int canon_r5_storage_mount_card(struct canon_r5_storage_device *storage, int slot)
{
	int ret;
	
	if (!storage || !canon_r5_storage_slot_valid(slot))
		return -EINVAL;
		
	mutex_lock(&storage->lock);
	
	if (storage->cards[slot].status != CANON_R5_STORAGE_STATUS_INSERTED) {
		ret = -ENODEV;
		goto unlock;
	}
	
	storage->cards[slot].status = CANON_R5_STORAGE_STATUS_MOUNTED;
	storage->cards[slot].last_access = ktime_get();
	
	if (storage->active_card < 0)
		storage->active_card = slot;
		
	canon_r5_storage_info(storage, "Mounted storage card in slot %d", slot);
	ret = 0;
	
unlock:
	mutex_unlock(&storage->lock);
	return ret;
}

int canon_r5_storage_unmount_card(struct canon_r5_storage_device *storage, int slot)
{
	if (!storage || !canon_r5_storage_slot_valid(slot))
		return -EINVAL;
		
	mutex_lock(&storage->lock);
	
	storage->cards[slot].status = CANON_R5_STORAGE_STATUS_EMPTY;
	memset(&storage->cards[slot], 0, sizeof(storage->cards[slot]));
	
	if (storage->active_card == slot)
		storage->active_card = -1;
		
	canon_r5_storage_info(storage, "Unmounted storage card from slot %d", slot);
	
	mutex_unlock(&storage->lock);
	return 0;
}

int canon_r5_storage_format_card(struct canon_r5_storage_device *storage, int slot)
{
	u32 storage_id;
	int ret;
	
	if (!storage || !canon_r5_storage_slot_valid(slot))
		return -EINVAL;
		
	mutex_lock(&storage->lock);
	
	if (storage->cards[slot].status != CANON_R5_STORAGE_STATUS_MOUNTED) {
		ret = -ENODEV;
		goto unlock;
	}
	
	storage_id = (slot == 0) ? 0x00010001 : 0x00020001;
	ret = canon_r5_ptp_format_storage(storage->canon_dev, storage_id);
	
	if (!ret) {
		/* Reset card information after format */
		storage->cards[slot].free_space = storage->cards[slot].total_capacity;
		storage->cards[slot].file_count = 0;
		storage->cards[slot].folder_count = 0;
		storage->cards[slot].needs_format = false;
		
		canon_r5_storage_info(storage, "Formatted storage card in slot %d", slot);
	}
	
unlock:
	mutex_unlock(&storage->lock);
	return ret;
}

/* File operations */
int canon_r5_storage_read_file(struct canon_r5_storage_device *storage,
			       struct canon_r5_file_object *file,
			       void *buffer, size_t size, loff_t offset,
			       size_t *bytes_read)
{
	int ret;
	
	if (!storage || !file || !buffer || !bytes_read)
		return -EINVAL;
		
	/* Check if file is cached */
	if (file->cached && file->cache_data && offset < file->cache_size) {
		size_t to_copy = min(size, file->cache_size - (size_t)offset);
		memcpy(buffer, (char *)file->cache_data + offset, to_copy);
		*bytes_read = to_copy;
		storage->stats.cache_hits++;
		return 0;
	}
	
	/* Read from camera via PTP */
	ret = canon_r5_ptp_get_object_data(storage->canon_dev, file->object_handle,
					   buffer, size, offset, bytes_read);
	if (!ret) {
		storage->stats.files_read++;
		storage->stats.bytes_read += *bytes_read;
		storage->stats.last_operation = ktime_get();
		storage->stats.cache_misses++;
	}
	
	return ret;
}

int canon_r5_storage_write_file(struct canon_r5_storage_device *storage,
				const char *filename, const void *buffer,
				size_t size, struct canon_r5_file_object **new_file)
{
	u32 new_handle;
	int ret;
	
	if (!storage || !filename || !buffer || size == 0)
		return -EINVAL;
		
	if (storage->active_card < 0)
		return -ENODEV;
		
	ret = canon_r5_ptp_send_object_data(storage->canon_dev, filename, buffer, size, 0, &new_handle);
	if (ret)
		return ret;
		
	/* Create new file object */
	if (new_file) {
		*new_file = kzalloc(sizeof(**new_file), GFP_KERNEL);
		if (*new_file) {
			(*new_file)->object_handle = new_handle;
			strncpy((*new_file)->filename, filename, sizeof((*new_file)->filename) - 1);
			(*new_file)->file_type = canon_r5_storage_detect_file_type(filename);
			(*new_file)->file_size = size;
			(*new_file)->creation_time = ktime_get();
			(*new_file)->modification_time = (*new_file)->creation_time;
			kref_init(&(*new_file)->ref_count);
		}
	}
	
	storage->stats.files_written++;
	storage->stats.bytes_written += size;
	storage->stats.last_operation = ktime_get();
	
	return 0;
}

int canon_r5_storage_delete_file(struct canon_r5_storage_device *storage,
				 struct canon_r5_file_object *file)
{
	int ret;
	
	if (!storage || !file)
		return -EINVAL;
		
	ret = canon_r5_ptp_delete_object(storage->canon_dev, file->object_handle);
	if (!ret) {
		/* Remove from filesystem cache if present */
		if (storage->fs_info) {
			struct rb_node *node;
			unsigned long flags;
			
			spin_lock_irqsave(&storage->fs_info->file_lock, flags);
			node = &file->rb_node;
			if (!RB_EMPTY_NODE(node)) {
				rb_erase(node, &storage->fs_info->file_tree);
				RB_CLEAR_NODE(node);
			}
			list_del_init(&file->list);
			spin_unlock_irqrestore(&storage->fs_info->file_lock, flags);
		}
		
		storage->stats.last_operation = ktime_get();
	}
	
	return ret;
}

/* Directory operations */
int canon_r5_storage_list_directory(struct canon_r5_storage_device *storage,
				    u32 parent_handle,
				    struct list_head *entries)
{
	u32 handles[256];
	int count, i, ret;
	
	if (!storage || !entries)
		return -EINVAL;
		
	if (storage->active_card < 0)
		return -ENODEV;
		
	INIT_LIST_HEAD(entries);
	
	u32 storage_id = (storage->active_card == 0) ? 0x00010001 : 0x00020001;
	count = canon_r5_ptp_get_object_handles(storage->canon_dev, storage_id, parent_handle,
						handles, ARRAY_SIZE(handles));
	if (count < 0)
		return count;
		
	/* Create directory entries */
	for (i = 0; i < count; i++) {
		struct canon_r5_dir_entry *entry;
		struct canon_r5_file_object file_info;
		
		ret = canon_r5_ptp_get_object_info(storage->canon_dev, handles[i], &file_info);
		if (ret)
			continue;
			
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			continue;
			
		entry->object_handle = handles[i];
		strncpy(entry->name, file_info.filename, sizeof(entry->name) - 1);
		entry->type = file_info.file_type;
		entry->size = file_info.file_size;
		entry->mtime = file_info.modification_time;
		entry->is_directory = (file_info.file_type == CANON_R5_FILE_FOLDER);
		
		list_add_tail(&entry->list, entries);
	}
	
	return count;
}

/* Statistics */
int canon_r5_storage_get_stats(struct canon_r5_storage_device *storage,
			       struct canon_r5_storage_stats *stats)
{
	if (!storage || !stats)
		return -EINVAL;
		
	mutex_lock(&storage->lock);
	*stats = storage->stats;
	mutex_unlock(&storage->lock);
	
	return 0;
}

void canon_r5_storage_reset_stats(struct canon_r5_storage_device *storage)
{
	if (!storage)
		return;
		
	mutex_lock(&storage->lock);
	memset(&storage->stats, 0, sizeof(storage->stats));
	mutex_unlock(&storage->lock);
}

/* Utility functions */
u64 canon_r5_storage_get_free_space(struct canon_r5_storage_device *storage, int slot)
{
	if (!storage || !canon_r5_storage_slot_valid(slot))
		return 0;
		
	return storage->cards[slot].free_space;
}

bool canon_r5_storage_is_write_protected(struct canon_r5_storage_device *storage, int slot)
{
	if (!storage || !canon_r5_storage_slot_valid(slot))
		return true;
		
	return storage->cards[slot].write_protected ||
	       storage->cards[slot].status == CANON_R5_STORAGE_STATUS_WRITE_PROTECTED;
}

/* Driver initialization */
int canon_r5_storage_init(struct canon_r5_device *dev)
{
	struct canon_r5_storage *priv;
	struct canon_r5_storage_device *storage;
	int ret;
	
	if (!dev) {
		pr_err("Canon R5 Storage: Invalid device\n");
		return -EINVAL;
	}
	
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
		
	storage = &priv->device;
	storage->canon_dev = dev;
	
	/* Initialize device state */
	mutex_init(&storage->lock);
	storage->initialized = false;
	storage->mounted = false;
	storage->active_card = -1;
	
	/* Initialize storage cards */
	for (int i = 0; i < CANON_R5_MAX_STORAGE_CARDS; i++) {
		storage->cards[i].slot_id = i;
		storage->cards[i].status = CANON_R5_STORAGE_STATUS_EMPTY;
	}
	
	/* Initialize PTP operations */
	mutex_init(&storage->ptp.lock);
	INIT_WORK(&storage->ptp.refresh_work, canon_r5_storage_refresh_work);
	storage->ptp.refresh_wq = alloc_workqueue("canon_r5_storage_refresh", WQ_MEM_RECLAIM, 0);
	if (!storage->ptp.refresh_wq) {
		ret = -ENOMEM;
		goto error_cleanup;
	}
	
	/* Initialize event handling */
	INIT_WORK(&storage->events.card_event_work, canon_r5_storage_card_event_work);
	
	/* Initialize background operations */
	priv->background.wq = alloc_workqueue("canon_r5_storage_bg", WQ_MEM_RECLAIM, 0);
	if (!priv->background.wq) {
		ret = -ENOMEM;
		goto error_refresh_wq;
	}
	
	INIT_DELAYED_WORK(&priv->background.sync_work, canon_r5_storage_sync_work);
	
	/* Initialize mount management */
	INIT_LIST_HEAD(&priv->mounts.mount_list);
	mutex_init(&priv->mounts.lock);
	priv->mounts.mount_count = 0;
	
	/* Register with core driver */
	ret = canon_r5_register_storage_driver(dev, storage);
	if (ret) {
		dev_err(dev->dev, "Failed to register storage driver: %d\n", ret);
		goto error_bg_wq;
	}
	
	/* Scan for storage cards */
	ret = canon_r5_storage_scan_cards(storage);
	if (ret > 0) {
		dev_info(dev->dev, "Found %d storage device(s)\n", ret);
		ret = 0; /* Success */
	}
	
	/* Start background sync */
	queue_delayed_work(priv->background.wq, &priv->background.sync_work, 10 * HZ);
	
	storage->initialized = true;
	dev_info(dev->dev, "Canon R5 storage driver initialized successfully\n");
	
	return 0;
	
error_bg_wq:
	destroy_workqueue(priv->background.wq);
error_refresh_wq:
	destroy_workqueue(storage->ptp.refresh_wq);
error_cleanup:
	kfree(priv);
	return ret;
}

void canon_r5_storage_cleanup(struct canon_r5_device *dev)
{
	struct canon_r5_storage_device *storage;
	struct canon_r5_storage *priv;
	
	if (!dev)
		return;
		
	storage = canon_r5_get_storage_driver(dev);
	if (!storage)
		return;
		
	priv = container_of(storage, struct canon_r5_storage, device);
	
	dev_info(dev->dev, "Cleaning up Canon R5 storage driver\n");
	
	/* Stop background operations */
	if (priv->background.wq) {
		cancel_delayed_work_sync(&priv->background.sync_work);
		destroy_workqueue(priv->background.wq);
	}
	
	/* Stop refresh operations */
	if (storage->ptp.refresh_wq) {
		cancel_work_sync(&storage->ptp.refresh_work);
		destroy_workqueue(storage->ptp.refresh_wq);
	}
	
	/* Stop event handling */
	cancel_work_sync(&storage->events.card_event_work);
	
	/* Unmount all cards */
	for (int i = 0; i < CANON_R5_MAX_STORAGE_CARDS; i++) {
		canon_r5_storage_unmount_card(storage, i);
	}
	
	canon_r5_unregister_storage_driver(dev);
	kfree(priv);
}

/* Module functions */
static int __init canon_r5_storage_module_init(void)
{
	pr_info("Canon R5 Storage Driver v%s loaded\n", "1.0.0");
	return 0;
}

static void __exit canon_r5_storage_module_exit(void)
{
	pr_info("Canon R5 Storage Driver unloaded\n");
}

module_init(canon_r5_storage_module_init);
module_exit(canon_r5_storage_module_exit);

/* Export symbols for use by core driver */
EXPORT_SYMBOL_GPL(canon_r5_storage_init);
EXPORT_SYMBOL_GPL(canon_r5_storage_cleanup);
EXPORT_SYMBOL_GPL(canon_r5_storage_scan_cards);
EXPORT_SYMBOL_GPL(canon_r5_storage_mount_card);
EXPORT_SYMBOL_GPL(canon_r5_storage_unmount_card);
EXPORT_SYMBOL_GPL(canon_r5_storage_format_card);
EXPORT_SYMBOL_GPL(canon_r5_storage_get_file);
EXPORT_SYMBOL_GPL(canon_r5_storage_put_file);
EXPORT_SYMBOL_GPL(canon_r5_storage_read_file);
EXPORT_SYMBOL_GPL(canon_r5_storage_write_file);
EXPORT_SYMBOL_GPL(canon_r5_storage_delete_file);
EXPORT_SYMBOL_GPL(canon_r5_storage_list_directory);
EXPORT_SYMBOL_GPL(canon_r5_storage_get_stats);
EXPORT_SYMBOL_GPL(canon_r5_storage_reset_stats);