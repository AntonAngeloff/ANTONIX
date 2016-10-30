/*
 * vfs.c
 *
 *  Created on: 7.07.2016 ã.
 *      Author: Anton Angelov
 */

#include "vfs.h"
#include "mm.h"
#include "string.h"
#include "hal.h"
#include "url_utils.h"
#include "vga.h"
#include <kstdio.h>

/* Root node of the vfs */
K_VFS_NODE *vfs_root;

/**
 * VFS file driver
 */
K_FS_DRIVER vfs_fs_driver = {
		.create = vfs_create,
		.open = vfs_open,
		.opendir = vfs_opendir,
		.mkdir = vfs_mkdir,
		.finalize = NULL
};

HRESULT vfs_init() {
	vfs_root = kcalloc(sizeof(K_VFS_NODE));

	/* Initialize root node */
	memset(vfs_root, 0, sizeof(K_VFS_NODE));
	vfs_root->desc.type = NODE_TYPE_DIRECTORY;

	return S_OK;
}

HRESULT vfsnode_find_child(K_VFS_NODE *n, const char *child_name, uint32_t *id_out)
{
	int32_t i;

	for (i=0; i<n->child_cnt; i++) {
		K_VFS_NODE *ch = n->children[i];

		if (strcmp(ch->desc.name, child_name) == 0) {
			*id_out = i;
			return S_OK;
		}
	}

	return E_NOTFOUND;
}

void vfsnode_addref(K_VFS_NODE *n)
{
	/* TODO: lock mutex to protect critical section */
	n->ref_count++;
}

uint32_t vfsnode_release(K_VFS_NODE *n)
{
	/* TODO: lock mutex to protect critical section */
	assert_msg(n->ref_count > 0, "VFS node's refcount dropped below zero.");
	return --(n->ref_count);
}

HRESULT vfsnode_add_child(K_VFS_NODE *n, char *node_name, uint32_t node_type, K_VFS_NODE **new_out)
{
	/* Check weather node with such name already exists */
	uint32_t id;
	if (vfsnode_find_child(n, node_name, &id) == S_OK) {
		/* Node with same name already exists */
		return E_INVALIDARG;
	}

	if (n->child_cnt >= n->child_capacity) {
		/* Grow the array */
		if (n->child_capacity == 0) {
			n->children = kmalloc(sizeof(void*) * 5);
			n->child_capacity = 5;
		} else {
			n->children = krealloc(n->children, sizeof(void*) * n->child_capacity * 2);
			n->child_capacity *= 2;
		}

		if (!n->children) {
			/* Out of memory */
			return E_FAIL;
		}
	}

	/* Create new node and populate it with data */
	K_VFS_NODE *new = kcalloc(sizeof(K_VFS_NODE));
	strcpy(new->desc.name, node_name);
	new->desc.type = node_type;
	new->desc.index = n->child_cnt;
	new->parent = n;

	mutex_create(&new->lock);

	/* Add the new node as child */
	n->children[n->child_cnt++] = new;

	*new_out = new;
	return S_OK;
}

void implode_str(char **components, int num, char *dst)
{
	int i, str_id = 0;

	for (i=0; i<num; i++) {
		dst[str_id++] = VFS_PATH_DELIMITER;

		char *comp = components[i];
		char *eoc = strchr(comp, VFS_PATH_DELIMITER);
		if (eoc == NULL) eoc = comp + strlen(comp);

		int len = (int)(eoc - comp);
		assert(len >= 0);

		if (len > 0) {
			memcpy(&dst[str_id], comp, len);
			str_id += len;
		}
	}

	dst[str_id++] = '\0';
}

HRESULT vfs_parse_url(char *dirname, K_VFS_NODE *vfs_root, K_VFS_NODE **out, uint32_t *is_crossing_mountpoint, char *base_path, char *mounted_path)
{
	/* All absolute path should start with path delimiter */
	if (dirname[0] != VFS_PATH_DELIMITER && strlen(dirname) != 0) {
		return E_INVALIDARG;
	}

	char delim_str[2] = {VFS_PATH_DELIMITER, '\0'};
	if (strcmp(dirname, delim_str) == 0 || strlen(dirname) == 0) {
		/* Caller is asking for root node */
		*out = vfs_root;

		if (is_crossing_mountpoint) {
			*is_crossing_mountpoint = 0;
		}

		return S_OK;
	}

	uint32_t comp_count = 0;
	int32_t i;
	int32_t len = strlen(dirname);
	K_VFS_NODE *current_pos = vfs_root;

	for (i=0; i<len; i++) {
		if (dirname[i] == VFS_PATH_DELIMITER) {
			comp_count ++;
		}
	}

	/* Create array to hold the names of each component */
	uint32_t comp_id = 0;
	char *components[comp_count];

	for (i=0; i<len; i++) {
		if (dirname[i] == VFS_PATH_DELIMITER) {
			components[comp_id++] = &dirname[i+1];
		}
	}

	/* Search across the file system */
	for (i=0; i<(int32_t)comp_count; i++) {
		char comp_name[256];

		/* Find length of the component name */
		const char *s = strchr(components[i], VFS_PATH_DELIMITER);

		if (!s) {
			len = strlen(components[i]);
		} else {
			len = (uint32_t)(s - components[i]);
		}

		/* Copy component name to local variable */
		memcpy(comp_name, components[i], len);
		comp_name[len] = '\0';

		uint32_t id;
		HRESULT hr;

		/* Find node with name 'comp_name' among node 'current_pos' children */
		hr = vfsnode_find_child(current_pos, comp_name, &id);
		if (FAILED(hr)) return hr;

		/* Found */
		current_pos = current_pos->children[id];

		if (current_pos->desc.type & NODE_TYPE_MOUNTPOINT) {
			/* We reached a mount-point, the nodes descending from _current-pos_
			 * are no longer belonging to the VFS and they are governed by another
			 * fs driver instead. In this case we'll return the current node (which
			 * is the mount-point node) and provide remainder path.
			 */
			if (is_crossing_mountpoint)
				*is_crossing_mountpoint = 1;

			if (base_path) {
				implode_str(&components[0], i+1, base_path);
			}

			if (mounted_path) {
				implode_str(&components[i+1], comp_count - i - 1, mounted_path);
			}

			*out = current_pos;
			return S_OK;
		}
	}

	if (is_crossing_mountpoint) {
		*is_crossing_mountpoint = 0;
	}

	/* If we reach here, then we parsed the url and found the target node */
	*out = current_pos;
	return S_OK;
}

/* Opens a directory inside the VFS and create directory handle called
 * K_DIR_STREAM.
 *
 * If 'dirname' is invalid or non-existent, it returns E_INVALIDARG.
 */
HRESULT vfs_opendir(K_FS_DRIVER *self, char *dirname, K_DIR_STREAM **out)
{
	K_VFS_NODE 	*node;
	uint32_t	is_external_fs;
	char		base_path[1024];
	char		ext_path[1024];

	UNUSED_ARG(self);

	/* Find VFS node by URL/filename
	 */
	HRESULT hr = vfs_parse_url(dirname, vfs_root, &node, &is_external_fs, base_path, ext_path);
	if (FAILED(hr)) return hr;

	/* Handle external fs redirection */
	if (SUCCEEDED(hr) && is_external_fs) {
		/* We should have received the mount-point node, so we have to redirect
		 * the remaining path to it's own fs driver.
		 */
		return node->desc.fs_driver->opendir(node->desc.fs_driver, ext_path, out);
	}

	/* Not a mount-point. Make sure node is actually a directory */
	if ((node->desc.type & NODE_TYPE_DIRECTORY) == 0) {
		return E_INVALIDARG;
	}

	/* Allocate K_DIR_STREAM */
	K_DIR_STREAM *dir = kmalloc(sizeof(K_DIR_STREAM));

	/* Allocate string to hold name of the directory being opened */
	dir->dirname = kmalloc(VFS_MAX_DIRNAME_LENGTH);
	strcpy(dir->dirname, node->desc.name);

	dir->len = node->child_cnt;
	dir->pos = 0;

	/* Allocate arrays */
	if (dir->len > 0) {
		dir->names = kmalloc(dir->len * sizeof(void*));
		dir->info = kmalloc(dir->len * sizeof(K_FS_NODE_INFO));
	}

	int i;

	/* Populate arrays */
	for (i=0; i<dir->len; i++) {
		/* Populate item name */
		dir->names[i] = kmalloc(VFS_MAX_DIRNAME_LENGTH);
		strcpy(dir->names[i], node->children[i]->desc.name);

		/* Populate info */
		dir->info[i].node_type = node->children[i]->desc.type;
		dir->info[i].size= node->children[i]->desc.size;
	}

	/* Assign directory ops */
	dir->readdir 	= vfs_readdir;
	dir->rewinddir 	= vfs_rewinddir;
	dir->closedir	= vfs_closedir;

	/* Success */
	*out = dir;
	return S_OK;
}

HRESULT vfs_readdir(K_DIR_STREAM *dirstr, char *filename, K_FS_NODE_INFO *info)
{
	assert(dirstr->pos <= dirstr->len);

	if (dirstr->pos == dirstr->len) {
		return E_ENDOFSTR;
	}

	strcpy(filename, dirstr->names[dirstr->pos]);

	if (info != NULL) {
		*info = dirstr->info[dirstr->pos];
	}

	dirstr->pos++;

	return S_OK;
}

HRESULT vfs_rewinddir(K_DIR_STREAM *dirstr)
{
	dirstr->pos = 0;
	return S_OK;
}

HRESULT vfs_closedir(K_DIR_STREAM **dirstr)
{
	K_DIR_STREAM *d = *dirstr;

	for (int i=0; i<d->len; i++) {
		kfree(d->names[i]);
	}

	if (d->len > 0) {
		kfree(d->names);
		kfree(d->info);
	}

	kfree(d->dirname);
	kfree(d);

	*dirstr = NULL;
	return S_OK;
}

HRESULT vfs_mkdir(K_FS_DRIVER *self, char *path, uint32_t mode)
{
	char dir_path[1024], dir_name[1024];
	UNUSED_ARG(self);

	url_get_dirname(path, dir_path);

	/* Find directory
	 * TODO: handle mountpoint redirect
	 */
	K_VFS_NODE *parent, *new;
	HRESULT hr = vfs_parse_url(dir_path, vfs_root, &parent, NULL, NULL, NULL);
	if (FAILED(hr)) return hr;

	/* TO DO:
	 *   - Make sure the parent directory has write permissions
	 */

	/* Get directory name from path */
	url_get_basename(path, dir_name);

	/* Add child node */
	hr = vfsnode_add_child(parent, dir_name, NODE_TYPE_DIRECTORY, &new);
	new->desc.permission = mode;
	new->desc.fs_driver = &vfs_fs_driver;

	return hr;
}

HRESULT vfs_file_read(K_STREAM *str, const size_t block_size, void *out_buf, size_t *bytes_read)
{
	K_VFS_NODE *vfs_node = str->priv_data;
	if (!vfs_node) {
		/* Unexpected. Maybe we receive a K_STREAM struct, allocated by another fs driver? */
		return 0;
	}

	/* Make sure stream is readable */
	if ((str->mode & FILE_OPEN_READ) == 0) {
		if (bytes_read) { *bytes_read = 0; }
		return E_FAIL;
	}

	uint32_t node_size = vfs_node->desc.size;

	/* Make some validations */
	if (str->pos > node_size) {
		HalKernelPanic("File pos is beyond it's size.");
	}

	size_t effective_block_size = (str->pos + block_size > node_size) ? node_size - str->pos : block_size;

	if (effective_block_size == 0) {
		/* End of file */
		if(bytes_read != NULL) {
			*bytes_read=0;
		}

		return E_ENDOFSTR;
	}

	/* Read from content */
	memcpy(out_buf, (uint8_t*)vfs_node->content + str->pos, effective_block_size);

	/* Move file position */
	str->pos += effective_block_size;

	if (bytes_read != NULL) {
		*bytes_read = effective_block_size;
	}

	return S_OK;
}

HRESULT vfs_file_write(K_STREAM *str, const int32_t block_size, void *in_buf, size_t *bytes_written)
{
	K_VFS_NODE *vfs_node = str->priv_data;
	if (!vfs_node) {
		/* Unexpected. Maybe we receive a K_STREAM struct, allocated by another fs driver? */
		return 0;
	}

	/* Make sure stream is writable */
	if ((str->mode & FILE_OPEN_WRITE) == 0) {
		if (bytes_written) { *bytes_written = 0; }
		return E_FAIL;
	}

	uint32_t node_size = vfs_node->desc.size;

	/* Make some validations */
	if (str->pos > node_size) {
		HalKernelPanic("vfs_file_write(): File pos is beyond it's size.");
	}

	/* Allocate content buffer, if it hasn't been allocated yet */
	if (vfs_node->content_capacity == 0) {
		vfs_node->content = kmalloc(block_size);
		if (vfs_node->content == NULL) {
			if (bytes_written) *bytes_written = 0;
			return E_OUTOFMEM;
		}

		vfs_node->content_capacity = block_size;
	}

	/* Grow content buffer, if necessary */
	if (str->pos + block_size > vfs_node->content_capacity) {
		/* The new capacity is the required capacity * 1.2 */
		uint32_t new_cap = str->pos + block_size;
		new_cap += new_cap * 2 / 10;

		/* Realloc content buffer */
		vfs_node->content = krealloc(vfs_node->content, new_cap);
		if (vfs_node->content == NULL) {
			if (bytes_written) *bytes_written = 0;
			return E_OUTOFMEM;
		}

		vfs_node->content_capacity = new_cap;
	}

	uint8_t *content_pos = (uint8_t*)vfs_node->content + str->pos;

	/* Write */
	memcpy(content_pos, in_buf, block_size);

	/* Move cursor */
	str->pos += block_size;
	if (str->pos > vfs_node->desc.size) {
		vfs_node->desc.size = str->pos;
	}

	if (bytes_written) {
		*bytes_written = block_size;
	}

	return S_OK;
}

HRESULT vfs_file_ioctl(K_STREAM *s, uint32_t code, void *arg)
{
	/* VFS should gracefully fail when being requested to perform
	 * ioctl() on a file.
	 */
	UNUSED_ARG(s);
	UNUSED_ARG(code);
	UNUSED_ARG(arg);

	return E_FAIL;
}

uint32_t vfs_file_seek(K_STREAM *s, int64_t pos, int8_t origin)
{
	int32_t new_pos;
	HRESULT hr = S_OK;

	mutex_lock(&s->lock);
	K_VFS_NODE *node = s->priv_data;

	/* Get file size */
	mutex_lock(&node->lock);
	int32_t node_size = (int32_t)node->desc.size;
	mutex_unlock(&node->lock);

	switch (origin) {
	case KSTREAM_ORIGIN_BEGINNING:
		new_pos = pos;
		break;

	case KSTREAM_ORIGIN_CURRENT:
		new_pos =  s->pos + pos;
		break;

	case KSTREAM_ORIGIN_END:
		new_pos = node_size - pos;
		break;

	default:
		hr = E_INVALIDARG;
		goto finally;
	}

	if (new_pos < 0 || new_pos > node_size) {
		hr = E_INVALIDARG;
		goto finally;
	}

	s->pos = new_pos;

finally:
	mutex_unlock(&s->lock);
	return hr;
}

uint32_t vfs_file_tell(K_STREAM *s)
{
	mutex_lock(&s->lock);
	uint32_t r = s->pos;
	mutex_unlock(&s->lock);

	return r;
}

HRESULT vfs_create(K_FS_DRIVER *self, char *filename, uint32_t perm)
{
	K_VFS_NODE 	*node;
	uint32_t 	is_external_fs;
	char		ext_path[1024];

	UNUSED_ARG(self);

	/* Find VFS node by URL/filename */
	HRESULT hr = vfs_parse_url(filename, vfs_root, &node, &is_external_fs, NULL, ext_path);

	if (SUCCEEDED(hr) && is_external_fs) {
		/* We should have received the mount-point node, so we have to redirect
		 * the remaining path to it's own fs driver.
		 */
		return node->desc.fs_driver->create(node->desc.fs_driver, ext_path, perm);
	}

	if (hr != E_NOTFOUND) {
		/* File already exists */
		return E_FAIL;
	}

	char dir_name[VFS_MAX_DIRNAME_LENGTH];
	char file_name[VFS_MAX_DIRNAME_LENGTH];

	url_get_dirname(filename, dir_name);
	url_get_basename(filename, file_name);

	/* Find new file's parent directory */
	hr = vfs_parse_url(dir_name, vfs_root, &node, &is_external_fs, NULL, NULL);
	if (FAILED(hr)) {
		/* Parent dir does not exist */
		return hr;
	}

	K_VFS_NODE *new;

	/* Add child node */
	hr = vfsnode_add_child(node, file_name, NODE_TYPE_FILE, &new);
	new->desc.permission = perm;
	new->desc.fs_driver = &vfs_fs_driver;

	/* Success */
	return hr;
}

HRESULT vfs_mount_device(K_DEVICE *dev, char *path)
{
	/* Validate device type */
	if (dev->type != NODE_TYPE_BLOCKDEVICE && dev->type != NODE_TYPE_CHARDEVICE) {
		return E_FAIL;
	}

	K_VFS_NODE 	*node;
	uint32_t 	is_external_fs;
	char		ext_path[1024];

	/* Find VFS node by URL/filename */
	HRESULT hr = vfs_parse_url(path, vfs_root, &node, &is_external_fs, NULL, ext_path);

	if (SUCCEEDED(hr) && is_external_fs) {
		/* Path is residing in external (mounted) file system. We can't mount
		 * devices there.
		 */
		return E_INVALIDARG;
	}

	if (hr != E_NOTFOUND) {
		/* Path is already in use */
		return E_INVALIDARG;
	}

	char dir_name[VFS_MAX_DIRNAME_LENGTH];
	char file_name[VFS_MAX_DIRNAME_LENGTH];

	url_get_dirname(path, dir_name);
	url_get_basename(path, file_name);

	/* Find new file's parent directory */
	hr = vfs_parse_url(dir_name, vfs_root, &node, &is_external_fs, NULL, NULL);
	if (FAILED(hr)) {
		k_printf("parent node (%s) not found.\n", dir_name);
		/* Parent directory does not exist */
		return hr;
	}

	K_VFS_NODE *new;

	/* Add child node */
	hr = vfsnode_add_child(node, file_name, dev->type, &new);
	new->desc.permission = NODE_MODE_ALL_RWE;
	new->desc.fs_driver = &vfs_fs_driver;

	/* Since content will not be used for devices, we can use it to
	 * store the K_DEVICE struct, which contain pointers to
	 * device functions.
	 */
	new->content_capacity = sizeof(K_DEVICE);
	new->content = kmalloc(new->content_capacity);
	memcpy(new->content, dev, new->content_capacity);

	/* Invoke initialization routine on driver
	 */
	if (dev->initialize != NULL) {
		hr = dev->initialize((K_DEVICE*)new->content);
		if (FAILED(hr)) HalKernelPanic("vfs_mount_device(): Device failed to initialize. Handling routine not implemented.");
		//TODO: check result
	}

	/* Success */
	return hr;
}

HRESULT vfs_unmount_device(char *path)
{
	K_VFS_NODE 	*node;
	uint32_t 	is_external_fs;

	/* Find VFS node by URL/filename */
	HRESULT hr = vfs_parse_url(path, vfs_root, &node, &is_external_fs, NULL, NULL);

	if (FAILED(hr) || (SUCCEEDED(hr) && is_external_fs)) {
		/* Designated path does not exist on VFS */
		return E_INVALIDARG;
	}

	if (node->desc.type != NODE_TYPE_BLOCKDEVICE && node->desc.type != NODE_TYPE_CHARDEVICE) {
		/* The node pointed by path is not a device */
		return E_FAIL;
	}

	K_DEVICE *dev = (K_DEVICE*)node->content;

	/* Invoke finalization routine on driver
	 */
	if (dev->finalize != NULL) {
		dev->finalize(dev);
		//TODO: check result
	}

	/* TODO */
	return E_NOTIMPL;
}

HRESULT vfs_mount_fs(char *mount_path, K_FS_CONSTRUCTOR fs_ctor, char *storage_drv)
{
	K_VFS_NODE 	*node;
	K_VFS_NODE 	*new;
	K_FS_DRIVER *fs_driver;
	uint32_t 	is_external_fs;
	char		ext_path[1024];

	if (mount_path == NULL || fs_ctor == NULL || storage_drv == NULL) {
		return E_POINTER;
	}

	/* Find VFS node by URL/filename */
	HRESULT hr = vfs_parse_url(mount_path, vfs_root, &node, &is_external_fs, NULL, ext_path);

	if (SUCCEEDED(hr) && is_external_fs) {
		/* Path is residing in external (mounted) file system. We can't mount
		 * devices there.
		 */
		return E_INVALIDARG;
	}

	if (hr != E_NOTFOUND) {
		/* Path is already in use */
		return E_INVALIDARG;
	}

	char dir_name[VFS_MAX_DIRNAME_LENGTH];
	char mp_name[VFS_MAX_DIRNAME_LENGTH];

	url_get_dirname(mount_path, dir_name);
	url_get_basename(mount_path, mp_name);

	/* Find new file's parent directory */
	hr = vfs_parse_url(dir_name, vfs_root, &node, &is_external_fs, NULL, NULL);
	if (FAILED(hr)) {
		/* Parent directory does not exist */
		return hr;
	}

	/* Create FS driver instance */
	fs_driver = kcalloc(sizeof(K_FS_DRIVER));

	hr = fs_ctor(fs_driver, storage_drv);
	if (FAILED(hr)) {
		/* Failed to create/initalize fs driver */
		kfree(fs_driver);
		return hr;
	}

	/* Add child node */
	hr = vfsnode_add_child(node, mp_name, NODE_TYPE_MOUNTPOINT, &new);
	new->desc.permission = NODE_MODE_ALL_RWE;
	new->desc.type = NODE_TYPE_MOUNTPOINT;
	new->desc.fs_driver = fs_driver;

	/* Success */
	return hr;
}

HRESULT vfs_unmount_fs(char *mount_path)
{
	K_VFS_NODE 	*node;
	uint32_t 	is_external_fs;

	/* Find VFS node by URL/filename */
	HRESULT hr = vfs_parse_url(mount_path, vfs_root, &node, &is_external_fs, NULL, NULL);

	if (FAILED(hr) || (SUCCEEDED(hr) && is_external_fs)) {
		/* Designated path does not exist on VFS */
		return E_INVALIDARG;
	}

	if ((node->desc.type & NODE_TYPE_MOUNTPOINT) == 0) {
		/* The node pointed by path is not a file system mount-point */
		return E_FAIL;
	}

	/* Invoke finalization routine on fs driver
	 */
	if (node->desc.fs_driver->finalize != NULL) {
		/* Invoke finlization function. If it fajls, there is not much
		 * we can do about it. We will just retun S_FALSE to indicate that
		 * operation returned successfully, but with side effects.
		 */
		hr = node->desc.fs_driver->finalize(node->desc.fs_driver);
		if (FAILED(hr)) hr = S_FALSE;

		/* Free instance memory */
		kfree(node->desc.fs_driver);
	}

	return hr;
}

HRESULT vfs_open(K_FS_DRIVER *self, char *filename, uint32_t fmode, K_STREAM **out)
{
	K_VFS_NODE *node;
	uint32_t is_external_fs;
	char	base_path[1024];
	char	ext_path[1024];

	UNUSED_ARG(self);

	/* Find VFS node by URL/filename */
	HRESULT hr = vfs_parse_url(filename, vfs_root, &node, &is_external_fs, base_path, ext_path);
	if (FAILED(hr)) return hr;

	if (is_external_fs) {
		/* We should have received the mount-point node, so we have to redirect
		 * the remaining path to it's own fs driver.
		 */
		return node->desc.fs_driver->open(node->desc.fs_driver, ext_path, fmode, out);
	}

	/* Validate permissions.
	 * We have to rewrite the following code later when we implement permissions.
	 */
	if (node->desc.permission != NODE_MODE_ALL_RWE) {
		return E_ACCESSDENIED;
	}

	/* Make sure this is a file and not a directory */
	if ((node->desc.type & NODE_TYPE_DIRECTORY) != 0) {
		return E_FAIL;
	}

	/* Start opening file. First add reference */
	vfsnode_addref(node);

	/* Allocate kernel stream structure */
	K_STREAM *str = kcalloc(sizeof(K_STREAM));
	if (!str) goto fail;

	/* Populate */
	str->filename = kmalloc(strlen(filename) + 1);
	strcpy(str->filename, filename);

	mutex_create(&str->lock);
	str->mode = fmode;
	str->pos = 0;
	str->priv_data = node;

	if (node->desc.type == NODE_TYPE_FILE) {
		/* Populate methods (for file) */
		str->read = vfs_file_read;
		str->seek = vfs_file_seek; //NULL; //TODO
		str->tell = vfs_file_tell; //NULL; //TODO
		str->write = vfs_file_write;
		str->ioctl = vfs_file_ioctl;
		str->close = vfs_close;
	}else if (node->desc.type == NODE_TYPE_BLOCKDEVICE || node->desc.type == NODE_TYPE_CHARDEVICE) {
		/* Populate methods (for devices) */
		K_DEVICE *dev = node->content;
		str->read = dev->read;
		str->seek = dev->seek;
		str->tell = dev->tell;
		str->write = dev->write;
		str->ioctl = dev->ioctl;
		str->close = vfs_close;

		/* Issue a DEVIO_OPEN command, to let the device know it is being opened. */
		hr = str->ioctl(str, DEVIO_OPEN, NULL);
		if (FAILED(hr)) HalKernelPanic("Failed to open device.");
	}else {
		/* WT? */
		HalKernelPanic("Unknown node type.");
	}

	*out = str;
	return S_OK;

fail:
	vfsnode_release(node);
	return E_FAIL;
}

HRESULT vfs_close(K_STREAM **str)
{
	K_STREAM *s = *str;
	K_VFS_NODE *n = s->priv_data;

	if (n->desc.type == NODE_TYPE_BLOCKDEVICE || n->desc.type == NODE_TYPE_CHARDEVICE) {
		HRESULT hr = s->ioctl(s, DEVIO_CLOSE, NULL);
		if (FAILED(hr)) HalKernelPanic("Failed to close device.");
	}

	kfree(s->filename);
	kfree(s);

	vfsnode_release(n);

	*str = NULL;
	return S_OK;
}

void vfs_selftest_list_dir(char *dir)
{
	HRESULT hr;
	K_DIR_STREAM *d;

	vga_printf("listing directories for '%s'\n", dir);
	hr = k_opendir(dir, &d);
	if (FAILED(hr)) {
		vga_printf("failed to open directory\n");
		return;
	}

	char dir_name[VFS_MAX_DIRNAME_LENGTH];
	int i=0;

	while (SUCCEEDED(vfs_readdir(d, &dir_name[0], NULL))) {
		vga_printf("[%s] ", dir_name);
		i++;
	}
	vga_printf("\nEND-OF-DIR (total %d)\n", i);
	vfs_closedir(&d);
}

K_FS_DRIVER *vfs_get_driver()
{
	return &vfs_fs_driver;
}

HRESULT vfs_selftest() {
	HRESULT hr;

//	vga_printf()
	hr = k_mkdir("/newdir", NODE_MODE_ALL_RWE);
	if (FAILED(hr)) { vga_printf("vfs_selftest(): failed. hr=%x\n", hr); return hr; }

	hr = k_mkdir("/newdir1", NODE_MODE_ALL_RWE);
	if (FAILED(hr)) { vga_printf("vfs_selftest(): failed. hr=%x\n", hr); return hr; }

	hr = k_mkdir("/newdir2", NODE_MODE_ALL_RWE);
	if (FAILED(hr)) { vga_printf("vfs_selftest(): failed. hr=%x\n", hr); return hr; }

	hr = k_mkdir("/newdir2/subdir1", NODE_MODE_ALL_RWE);
	if (FAILED(hr)) { vga_printf("vfs_selftest(): failed. hr=%x\n", hr); return hr; }

	hr = k_mkdir("/newdir2/subdir2", NODE_MODE_ALL_RWE);
	if (FAILED(hr)) { vga_printf("vfs_selftest(): failed. hr=%x\n", hr); return hr; }

	hr = k_mkdir("/newdir2/subdir3", NODE_MODE_ALL_RWE);
	if (FAILED(hr)) { vga_printf("vfs_selftest(): failed. hr=%x\n", hr); return hr; }


	vfs_selftest_list_dir("/");
	vfs_selftest_list_dir("/newdir");
	vfs_selftest_list_dir("/newdir1");
	vfs_selftest_list_dir("/newdir2");

	return S_OK;
}
