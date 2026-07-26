/*
 * HexagonFS virtual directory operations
 *
 * Copyright (C) 2023-2025 The HexagonRPC Contributors
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "hexagonfs.h"

struct virt_dir_ctx {
	const struct hexagonfs_dirent *const *dirlist;
	char *root_path;
	int readdir_index;   /* position for readdir iteration */
};

static const struct hexagonfs_dirent *walk_dir(const struct hexagonfs_dirent *const *dir,
					 const char *segment)
{
	const struct hexagonfs_dirent *const *curr = dir;
	while (*curr != NULL) {
		if (!strcmp(segment, (*curr)->name))
			break;
		curr++;
	}
	return *curr;
}

static int virt_dir_from_dirent(const void *dirent_data, bool dir, void **fd_data)
{
	const struct virt_dir_dirent_data *dd = dirent_data;
	struct virt_dir_ctx *ctx;

	ctx = malloc(sizeof(*ctx));
	if (ctx == NULL)
		return -ENOMEM;

	ctx->dirlist = dd->dirlist;
	ctx->root_path = dd->root_path ? strdup(dd->root_path) : NULL;
	*fd_data = ctx;
	return 0;
}

static int virt_dir_openat(struct hexagonfs_fd *dir,
			   const char *segment,
			   bool expect_dir,
			   struct hexagonfs_fd **out)
{
	struct virt_dir_ctx *dir_ctx = dir->data;
	const struct hexagonfs_dirent *ent;
	struct hexagonfs_fd *fd;
	int ret;

	ent = walk_dir(dir_ctx->dirlist, segment);
	if (ent == NULL)
		return -ENOENT;

	fd = malloc(sizeof(struct hexagonfs_fd));
	if (fd == NULL)
		return -ENOMEM;

	fd->is_assigned = false;
	fd->up = dir;
	fd->ops = ent->ops;

	/*
	 * If the child is also a virt_dir, inject root_path through
	 * its dirent data by wrapping it.
	 */
	if (ent->ops == &hexagonfs_virt_dir_ops && dir_ctx->root_path) {
		struct virt_dir_dirent_data sub_dd = {
			.root_path = dir_ctx->root_path,
			.dirlist = (const struct hexagonfs_dirent *const *)ent->u.dir,
		};
		ret = ent->ops->from_dirent(&sub_dd, expect_dir, &fd->data);
	} else {
		ret = ent->ops->from_dirent(ent->u.ptr, expect_dir, &fd->data);
	}
	if (ret)
		goto err;

	*out = fd;
	return 0;

err:
	free(fd);
	return ret;
}

static void virt_dir_close(void *fd_data)
{
	struct virt_dir_ctx *ctx = fd_data;
	free(ctx->root_path);
	free(ctx);
}

static int virt_dir_stat(struct hexagonfs_fd *fd, struct stat *stats)
{
	memset(stats, 0, sizeof(*stats));
	stats->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	return 0;
}

static int make_phys(struct virt_dir_ctx *ctx, const char *name, char **out)
{
	if (!ctx->root_path)
		return -ENOSYS;
	size_t len = strlen(ctx->root_path) + 1 + strlen(name) + 1;
	*out = malloc(len);
	if (!*out)
		return -ENOMEM;
	sprintf(*out, "%s/%s", ctx->root_path, name);
	return 0;
}

static int virt_dir_mkdir(struct hexagonfs_fd *dir, const char *name, mode_t mode)
{
	char *p; int r = make_phys(dir->data, name, &p);
	if (r) return r;
	r = mkdir(p, mode) ? -errno : 0;
	free(p); return r;
}

static int virt_dir_rmdir(struct hexagonfs_fd *dir, const char *name)
{
	char *p; int r = make_phys(dir->data, name, &p);
	if (r) return r;
	r = rmdir(p) ? -errno : 0;
	free(p); return r;
}

static int virt_dir_unlink(struct hexagonfs_fd *dir, const char *name)
{
	char *p; int r = make_phys(dir->data, name, &p);
	if (r) return r;
	r = unlink(p) ? -errno : 0;
	free(p); return r;
}

static int virt_dir_readdir(struct hexagonfs_fd *fd, size_t size, char *out)
{
	struct virt_dir_ctx *ctx = fd->data;
	const struct hexagonfs_dirent *ent;

	if (ctx->dirlist == NULL) {
		out[0] = '\0';
		return 0;
	}

	/* find the child at current index, advance */
	ent = ctx->dirlist[ctx->readdir_index];
	if (ent == NULL) {
		out[0] = '\0';
		return 0;
	}

	strncpy(out, ent->name, size);
	out[size - 1] = '\0';
	ctx->readdir_index++;
	return 0;
}

struct hexagonfs_file_ops hexagonfs_virt_dir_ops = {
	.close = virt_dir_close,
	.from_dirent = virt_dir_from_dirent,
	.openat = virt_dir_openat,
	.readdir = virt_dir_readdir,
	.stat = virt_dir_stat,
	.mkdir = virt_dir_mkdir,
	.rmdir = virt_dir_rmdir,
	.unlink = virt_dir_unlink,
};
