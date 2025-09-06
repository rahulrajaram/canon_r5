// SPDX-License-Identifier: GPL-2.0
/*
 * Canon R5 Linux Driver Suite
 * Filesystem interface implementation
 *
 * Copyright (C) 2025 Canon R5 Driver Project
 */

#include <linux/module.h>
#include <linux/kernel.h>
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
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/version.h>

#include "../../include/core/canon-r5.h"
#include "../../include/storage/canon-r5-storage.h"

/* Filesystem constants */
#define CANON_R5_FS_NAME		"canon_r5_fs"
#define CANON_R5_FS_MAGIC		0x43355235  /* "C5R5" */
#define CANON_R5_CACHE_MAX_SIZE		(64 * 1024 * 1024)  /* 64MB */

/* Filesystem mount options */
enum {
	Opt_slot,
	Opt_readonly,
	Opt_cache_size,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_slot, "slot=%u"},
	{Opt_readonly, "ro"},
	{Opt_cache_size, "cache_size=%u"},
	{Opt_err, NULL}
};

/* Filesystem operations forward declarations */
static struct inode *canon_r5_fs_alloc_inode(struct super_block *sb);
static void canon_r5_fs_destroy_inode(struct inode *inode);
static int canon_r5_fs_statfs(struct dentry *dentry, struct kstatfs *buf);
static int canon_r5_fs_show_options(struct seq_file *m, struct dentry *root);

/* File operations forward declarations */
static int canon_r5_fs_readdir(struct file *file, struct dir_context *ctx);
static loff_t canon_r5_fs_dir_llseek(struct file *file, loff_t offset, int whence);

static ssize_t canon_r5_fs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static ssize_t canon_r5_fs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);
static loff_t canon_r5_fs_file_llseek(struct file *file, loff_t offset, int whence);

/* Inode operations forward declarations */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,3,0)
#define CR5_IDMAP struct mnt_idmap
#else
#define CR5_IDMAP struct user_namespace
#endif
static struct dentry *canon_r5_fs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);
static int canon_r5_fs_create(CR5_IDMAP *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
static int canon_r5_fs_unlink(struct inode *dir, struct dentry *dentry);
static int canon_r5_fs_mkdir(CR5_IDMAP *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode);
static int canon_r5_fs_rmdir(struct inode *dir, struct dentry *dentry);

/* Address space operations forward declarations */
static int canon_r5_fs_read_folio(struct file *file, struct folio *folio);
static int canon_r5_fs_write_begin(struct file *file, struct address_space *mapping,
				   loff_t pos, unsigned len, struct page **pagep, void **fsdata);
static int canon_r5_fs_write_end(struct file *file, struct address_space *mapping,
				 loff_t pos, unsigned len, unsigned copied,
				 struct page *page, void *fsdata);

/* Filesystem inode structure */
struct canon_r5_inode_info {
	struct canon_r5_file_object *file_obj;
	u32 object_handle;
	struct inode vfs_inode;
};

/* Helper to get inode info */
static struct canon_r5_inode_info *CANON_R5_I(struct inode *inode)
{
	return container_of(inode, struct canon_r5_inode_info, vfs_inode);
}

/* Super operations */
const struct super_operations canon_r5_storage_super_ops = {
	.alloc_inode	= canon_r5_fs_alloc_inode,
	.destroy_inode	= canon_r5_fs_destroy_inode,
	.statfs		= canon_r5_fs_statfs,
	.show_options	= canon_r5_fs_show_options,
};

/* Directory inode operations */
const struct inode_operations canon_r5_storage_dir_inode_ops = {
	.lookup		= canon_r5_fs_lookup,
	.create		= canon_r5_fs_create,
	.unlink		= canon_r5_fs_unlink,
	.mkdir		= canon_r5_fs_mkdir,
	.rmdir		= canon_r5_fs_rmdir,
};

/* File inode operations */
const struct inode_operations canon_r5_storage_file_inode_ops = {
};

/* Directory file operations */
const struct file_operations canon_r5_storage_dir_file_ops = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.llseek		= canon_r5_fs_dir_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= canon_r5_fs_readdir,
};

/* Regular file operations */
const struct file_operations canon_r5_storage_file_ops = {
	.llseek		= canon_r5_fs_file_llseek,
	.read		= canon_r5_fs_read,
	.write		= canon_r5_fs_write,
	.mmap		= generic_file_readonly_mmap,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,6,0)
	.splice_read	= generic_file_splice_read,
#endif
};

/* Address space operations */
const struct address_space_operations canon_r5_storage_aops = {
	.read_folio	= canon_r5_fs_read_folio,
	.write_begin	= canon_r5_fs_write_begin,
	.write_end	= canon_r5_fs_write_end,
};

/* Filesystem operations implementation */
static struct inode *canon_r5_fs_alloc_inode(struct super_block *sb __attribute__((unused)))
{
	struct canon_r5_inode_info *info;
	
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;
		
	info->file_obj = NULL;
	info->object_handle = 0;
	
	return &info->vfs_inode;
}

static void canon_r5_fs_destroy_inode(struct inode *inode)
{
	struct canon_r5_inode_info *info = CANON_R5_I(inode);
	
	if (info->file_obj)
		canon_r5_storage_put_file(info->file_obj);
		
	kfree(info);
}

static int canon_r5_fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct canon_r5_fs_info *fs_info = dentry->d_sb->s_fs_info;
	struct canon_r5_storage_device *storage = fs_info->storage;
	int slot = storage->active_card;
	
	buf->f_type = CANON_R5_FS_MAGIC;
	buf->f_bsize = PAGE_SIZE;
	buf->f_namelen = 255;
	
	if (slot >= 0 && slot < CANON_R5_MAX_STORAGE_CARDS) {
		buf->f_blocks = storage->cards[slot].total_capacity / PAGE_SIZE;
		buf->f_bfree = storage->cards[slot].free_space / PAGE_SIZE;
		buf->f_bavail = buf->f_bfree;
		buf->f_files = storage->cards[slot].file_count;
		buf->f_ffree = 999999; /* Large number for available inodes */
	} else {
		buf->f_blocks = 0;
		buf->f_bfree = 0;
		buf->f_bavail = 0;
		buf->f_files = 0;
		buf->f_ffree = 0;
	}
	
	return 0;
}

static int canon_r5_fs_show_options(struct seq_file *m, struct dentry *root)
{
	struct canon_r5_fs_info *fs_info = root->d_sb->s_fs_info;
	struct canon_r5_storage_device *storage = fs_info->storage;
	
	seq_printf(m, ",slot=%d", storage->active_card);
	
	if (fs_info->cache.max_size != CANON_R5_CACHE_MAX_SIZE)
		seq_printf(m, ",cache_size=%zu", fs_info->cache.max_size);
		
	return 0;
}

/* Directory operations */
static int canon_r5_fs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct canon_r5_fs_info *fs_info = inode->i_sb->s_fs_info;
	struct canon_r5_storage_device *storage = fs_info->storage;
	struct canon_r5_inode_info *info = CANON_R5_I(inode);
	struct list_head entries;
	struct canon_r5_dir_entry *entry, *tmp;
	int ret;
	
	if (ctx->pos == 0) {
		if (!dir_emit_dot(file, ctx))
			return 0;
		ctx->pos = 1;
	}
	
	if (ctx->pos == 1) {
		if (!dir_emit_dotdot(file, ctx))
			return 0;
		ctx->pos = 2;
	}
	
	/* Get directory entries from storage */
	ret = canon_r5_storage_list_directory(storage, info->object_handle, &entries);
	if (ret < 0)
		return ret;
		
	/* Emit directory entries */
	list_for_each_entry_safe(entry, tmp, &entries, list) {
		if (ctx->pos < 2) {
			ctx->pos = 2;
			continue;
		}
		
		if (!dir_emit(ctx, entry->name, strlen(entry->name),
			      entry->object_handle,
			      entry->is_directory ? DT_DIR : DT_REG)) {
			break;
		}
		
		ctx->pos++;
		list_del(&entry->list);
		kfree(entry);
	}
	
	/* Clean up remaining entries */
	list_for_each_entry_safe(entry, tmp, &entries, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	
	return 0;
}

static loff_t canon_r5_fs_dir_llseek(struct file *file, loff_t offset, int whence)
{
	return generic_file_llseek(file, offset, whence);
}

/* File operations */
static ssize_t canon_r5_fs_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct canon_r5_fs_info *fs_info = inode->i_sb->s_fs_info;
	struct canon_r5_storage_device *storage = fs_info->storage;
	struct canon_r5_inode_info *info = CANON_R5_I(inode);
	void *kernel_buf;
	size_t bytes_read = 0;
	ssize_t ret;
	
	if (!info->file_obj)
		return -ENOENT;
		
	if (*ppos >= info->file_obj->file_size)
		return 0;
		
	count = min_t(size_t, count, info->file_obj->file_size - *ppos);
	
	kernel_buf = vmalloc(count);
	if (!kernel_buf)
		return -ENOMEM;
		
	ret = canon_r5_storage_read_file(storage, info->file_obj, kernel_buf, count, *ppos, &bytes_read);
	if (ret) {
		vfree(kernel_buf);
		return ret;
	}
	
	if (copy_to_user(buf, kernel_buf, bytes_read)) {
		vfree(kernel_buf);
		return -EFAULT;
	}
	
	vfree(kernel_buf);
	*ppos += bytes_read;
	
	return bytes_read;
}

static ssize_t canon_r5_fs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct canon_r5_fs_info *fs_info = inode->i_sb->s_fs_info;
	struct canon_r5_storage_device *storage = fs_info->storage;
	void *kernel_buf;
	struct canon_r5_file_object *new_file = NULL;
	ssize_t ret;
	
	if (canon_r5_storage_is_write_protected(storage, storage->active_card))
		return -EROFS;
		
	kernel_buf = vmalloc(count);
	if (!kernel_buf)
		return -ENOMEM;
		
	if (copy_from_user(kernel_buf, buf, count)) {
		vfree(kernel_buf);
		return -EFAULT;
	}
	
	ret = canon_r5_storage_write_file(storage, file->f_path.dentry->d_name.name,
					  kernel_buf, count, &new_file);
	if (ret) {
		vfree(kernel_buf);
		return ret;
	}
	
	vfree(kernel_buf);
	
	/* Update inode size */
	if (new_file) {
		struct canon_r5_inode_info *info = CANON_R5_I(inode);
		if (info->file_obj)
			canon_r5_storage_put_file(info->file_obj);
		info->file_obj = new_file;
		info->object_handle = new_file->object_handle;
		inode->i_size = new_file->file_size;
	}
	
	*ppos += count;
	return count;
}

static loff_t canon_r5_fs_file_llseek(struct file *file, loff_t offset, int whence)
{
	return generic_file_llseek(file, offset, whence);
}

/* Inode operations */
static struct dentry *canon_r5_fs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags __attribute__((unused)))
{
	struct canon_r5_fs_info *fs_info = dir->i_sb->s_fs_info;
	struct canon_r5_storage_device *storage = fs_info->storage;
	struct canon_r5_inode_info *dir_info = CANON_R5_I(dir);
	struct list_head entries;
	struct canon_r5_dir_entry *entry, *tmp;
	struct inode *inode = NULL;
	int ret;
	
	/* Search for the requested file/directory */
	ret = canon_r5_storage_list_directory(storage, dir_info->object_handle, &entries);
	if (ret < 0)
		return ERR_PTR(ret);
		
	list_for_each_entry_safe(entry, tmp, &entries, list) {
		if (strcmp(entry->name, dentry->d_name.name) == 0) {
			/* Found the entry - create an inode */
			inode = new_inode(dir->i_sb);
			if (inode) {
				struct canon_r5_inode_info *info = CANON_R5_I(inode);
				
				info->object_handle = entry->object_handle;
				info->file_obj = canon_r5_storage_get_file(storage, entry->object_handle);
				
				inode->i_ino = entry->object_handle;
				inode->i_size = entry->size;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,11,0)
				inode->i_mtime = ns_to_timespec64(ktime_to_ns(entry->mtime));
				inode->i_atime = inode->i_mtime;
				inode->i_ctime = inode->i_mtime;
#endif
				
				if (entry->is_directory) {
					inode->i_mode = S_IFDIR | 0755;
					inode->i_op = &canon_r5_storage_dir_inode_ops;
					inode->i_fop = &canon_r5_storage_dir_file_ops;
					set_nlink(inode, 2);
				} else {
					inode->i_mode = S_IFREG | 0644;
					inode->i_op = &canon_r5_storage_file_inode_ops;
					inode->i_fop = &canon_r5_storage_file_ops;
					inode->i_mapping->a_ops = &canon_r5_storage_aops;
					set_nlink(inode, 1);
				}
			}
			break;
		}
		
		list_del(&entry->list);
		kfree(entry);
	}
	
	/* Clean up remaining entries */
	list_for_each_entry_safe(entry, tmp, &entries, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	
	return d_splice_alias(inode, dentry);
}

static int canon_r5_fs_create(CR5_IDMAP *mnt_userns __attribute__((unused)), struct inode *dir __attribute__((unused)), struct dentry *dentry __attribute__((unused)), umode_t mode __attribute__((unused)), bool excl __attribute__((unused)))
{
	/* File creation through PTP is complex and typically done by the camera */
	return -EPERM;
}

static int canon_r5_fs_unlink(struct inode *dir __attribute__((unused)), struct dentry *dentry)
{
	struct canon_r5_fs_info *fs_info = dir->i_sb->s_fs_info;
	struct canon_r5_storage_device *storage = fs_info->storage;
	struct inode *inode = d_inode(dentry);
	struct canon_r5_inode_info *info = CANON_R5_I(inode);
	
	if (!info->file_obj)
		return -ENOENT;
		
	return canon_r5_storage_delete_file(storage, info->file_obj);
}

static int canon_r5_fs_mkdir(CR5_IDMAP *mnt_userns __attribute__((unused)), struct inode *dir __attribute__((unused)), struct dentry *dentry __attribute__((unused)), umode_t mode __attribute__((unused)))
{
	/* Directory creation through PTP would need to be implemented */
	return -EPERM;
}

static int canon_r5_fs_rmdir(struct inode *dir __attribute__((unused)), struct dentry *dentry __attribute__((unused)))
{
	/* Directory removal through PTP would need to be implemented */
	return -EPERM;
}

/* Address space operations */
static int canon_r5_fs_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode = file_inode(file);
	struct canon_r5_fs_info *fs_info = inode->i_sb->s_fs_info;
	struct canon_r5_storage_device *storage = fs_info->storage;
	struct canon_r5_inode_info *info = CANON_R5_I(inode);
	struct page *page = &folio->page;
	void *kaddr;
	size_t bytes_read = 0;
	loff_t offset = page_offset(page);
	int ret;
	
	if (!info->file_obj) {
		SetPageError(page);
		unlock_page(page);
		return -ENOENT;
	}
	
	kaddr = kmap(page);
	if (!kaddr) {
		SetPageError(page);
		unlock_page(page);
		return -ENOMEM;
	}
	
	ret = canon_r5_storage_read_file(storage, info->file_obj, kaddr, PAGE_SIZE, offset, &bytes_read);
	if (ret) {
		kunmap(page);
		SetPageError(page);
		unlock_page(page);
		return ret;
	}
	
	if (bytes_read < PAGE_SIZE)
		memset(kaddr + bytes_read, 0, PAGE_SIZE - bytes_read);
		
	kunmap(page);
	SetPageUptodate(page);
	unlock_page(page);
	
	return 0;
}

static int canon_r5_fs_write_begin(struct file *file __attribute__((unused)), struct address_space *mapping,
				   loff_t pos, unsigned len, struct page **pagep, void **fsdata __attribute__((unused)))
{
	return simple_write_begin(file, mapping, pos, len, pagep, fsdata);
}

static int canon_r5_fs_write_end(struct file *file __attribute__((unused)), struct address_space *mapping,
				 loff_t pos, unsigned len, unsigned copied,
				 struct page *page, void *fsdata __attribute__((unused)))
{
	/* Simple write end implementation */
	SetPageUptodate(page);
	if (!PageUptodate(page))
		SetPageError(page);
	if (pos + copied > mapping->host->i_size)
		i_size_write(mapping->host, pos + copied);
	set_page_dirty(page);
	unlock_page(page);
	put_page(page);
	return copied;
}

/* Filesystem registration */
static int canon_r5_fs_fill_super(struct super_block *sb, void *data, int silent __attribute__((unused)))
{
	struct canon_r5_fs_info *fs_info;
	struct inode *root_inode;
	char *options = data;
	char *p;
	int slot = 0;
	
	/* Parse mount options */
	if (options) {
		while ((p = strsep(&options, ",")) != NULL) {
			substring_t args[MAX_OPT_ARGS];
			int token, option;
			
			if (!*p)
				continue;
				
			token = match_token(p, tokens, args);
			switch (token) {
			case Opt_slot:
				if (match_int(&args[0], &option) || option < 0 || option >= CANON_R5_MAX_STORAGE_CARDS)
					return -EINVAL;
				slot = option;
				break;
			case Opt_readonly:
				sb->s_flags |= SB_RDONLY;
				break;
			case Opt_cache_size:
				/* Cache size option - not implemented yet */
				break;
			default:
				return -EINVAL;
			}
		}
	}
	
	/* Allocate filesystem info */
	fs_info = kzalloc(sizeof(*fs_info), GFP_KERNEL);
	if (!fs_info)
		return -ENOMEM;
		
	/* Initialize filesystem info */
	mutex_init(&fs_info->lock);
	fs_info->file_tree = RB_ROOT;
	INIT_LIST_HEAD(&fs_info->file_list);
	spin_lock_init(&fs_info->file_lock);
	
	/* Initialize directory cache */
	INIT_LIST_HEAD(&fs_info->dir_cache.entries);
	mutex_init(&fs_info->dir_cache.lock);
	fs_info->dir_cache.valid = false;
	
	/* Initialize file cache */
	INIT_LIST_HEAD(&fs_info->cache.lru_list);
	fs_info->cache.max_size = CANON_R5_CACHE_MAX_SIZE;
	fs_info->cache.total_size = 0;
	fs_info->cache.cleanup_wq = alloc_workqueue("canon_r5_fs_cache", WQ_MEM_RECLAIM, 0);
	if (!fs_info->cache.cleanup_wq) {
		kfree(fs_info);
		return -ENOMEM;
	}
	INIT_WORK(&fs_info->cache.cleanup_work, canon_r5_storage_cache_cleanup_work);
	
	/* Set up superblock */
	sb->s_magic = CANON_R5_FS_MAGIC;
	sb->s_op = &canon_r5_storage_super_ops;
	sb->s_time_gran = 1;
	sb->s_fs_info = fs_info;
	
	/* Create root inode */
	root_inode = new_inode(sb);
	if (!root_inode) {
		destroy_workqueue(fs_info->cache.cleanup_wq);
		kfree(fs_info);
		return -ENOMEM;
	}
	
	root_inode->i_ino = 1;
	root_inode->i_mode = S_IFDIR | 0755;
	root_inode->i_op = &canon_r5_storage_dir_inode_ops;
	root_inode->i_fop = &canon_r5_storage_dir_file_ops;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,11,0)
	root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode);
#endif
	set_nlink(root_inode, 2);
	
	/* Set root object handle to 0 (root directory in PTP) */
	CANON_R5_I(root_inode)->object_handle = 0;
	CANON_R5_I(root_inode)->file_obj = NULL;
	
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		destroy_workqueue(fs_info->cache.cleanup_wq);
		kfree(fs_info);
		return -ENOMEM;
	}
	
	return 0;
}

static struct dentry *canon_r5_fs_mount(struct file_system_type *fs_type,
					int flags, const char *dev_name __attribute__((unused)),
					void *data)
{
	return mount_nodev(fs_type, flags, data, canon_r5_fs_fill_super);
}

static void canon_r5_fs_kill_sb(struct super_block *sb)
{
	struct canon_r5_fs_info *fs_info = sb->s_fs_info;
	
	if (fs_info) {
		if (fs_info->cache.cleanup_wq) {
			cancel_work_sync(&fs_info->cache.cleanup_work);
			destroy_workqueue(fs_info->cache.cleanup_wq);
		}
		kfree(fs_info);
	}
	
	kill_anon_super(sb);
}

/* Filesystem registration */
int canon_r5_storage_register_filesystem(struct canon_r5_storage *storage)
{
	int ret;
	
	storage->fs_type.name = CANON_R5_FS_NAME;
	storage->fs_type.mount = canon_r5_fs_mount;
	storage->fs_type.kill_sb = canon_r5_fs_kill_sb;
	storage->fs_type.fs_flags = FS_REQUIRES_DEV;
	
	ret = register_filesystem(&storage->fs_type);
	if (!ret)
		storage->fs_registered = true;
		
	return ret;
}

void canon_r5_storage_unregister_filesystem(struct canon_r5_storage *storage)
{
	if (storage->fs_registered) {
		unregister_filesystem(&storage->fs_type);
		storage->fs_registered = false;
	}
}
