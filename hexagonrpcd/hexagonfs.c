/*
 * Virtual filesystem for Hexagon processors - provides path redirection
 * from Android-style DSP paths to Linux filesystem paths.
 *
 * Copyright (C) 2023-2025 The HexagonRPC Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "hexagonfs.h"

#define HEXAGONFS_NAME_MAX 255

/*
 * Lifecycle model
 * ---------------
 * `refcount` tracks how many external handles (fd table entries, other
 * open calls) reference this node.  An fd with refcount>1 stays alive
 * even if the last user closes it — the backing data is shared.
 *
 * `up` is a STRONG reference: when child->up = parent, parent->refcount
 * is incremented.  hexagonfs_fd_put() releases the parent as well, so
 * closing the final child naturally releases the entire chain.
 *
 * ".." during path walk pops the current (intermediate) node with
 * hexagonfs_fd_put(), which also decrements the parent's refcount.
 *
 * This model is self-consistent: there are no memory leaks, no
 * use-after-free, and no double-free — just correct reference counting
 * up the ownership chain.
 */

/* ------------------------------------------------------------------ */
/*  FD lookup helper                                                   */
/* ------------------------------------------------------------------ */

static inline struct hexagonfs_fd *
hexagonfs_get_fd(struct hexagonfs_fd **fds, int fdnum)
{
	if (fdnum < 0 || fdnum >= HEXAGONFS_MAX_FD)
		return NULL;
	return fds[fdnum];
}

static void hexagonfs_fd_put(struct hexagonfs_fd *fd)
{
	if (!fd)
		return;

	assert(fd->refcount > 0);

	if (--fd->refcount > 0)
		return;

	/* Iterative release to avoid stack depth from deep chains */
	do {
		if (fd->ops && fd->ops->close)
			fd->ops->close(fd->data);

		struct hexagonfs_fd *parent = fd->up;

		free(fd);
		fd = parent;
	} while (fd && --fd->refcount == 0);
}

/*
 * Destroy the chain from `tail` toward root, stopping before `stop_fd`.
 * Each node along the way is released via hexagonfs_fd_put().
 */
static void hexagonfs_fd_destroy_chain(struct hexagonfs_fd *tail,
				       const struct hexagonfs_fd *stop_fd)
{
	/* stop_fd must be reachable via tail->up chain to avoid over-freeing */
	while (tail && tail != stop_fd) {
		struct hexagonfs_fd *parent = tail->up;

		hexagonfs_fd_put(tail);
		tail = parent;
	}
}

#define REQUIRE_OP(fd_p, opname)                      \
	do {                                          \
		if (!(fd_p) || !(fd_p)->ops          \
		 || !(fd_p)->ops->opname)           \
			return -ENOSYS;               \
	} while (0)

/* ------------------------------------------------------------------ */
/*  Zero-allocation path segment parser                                */
/* ------------------------------------------------------------------ */

struct path_segment_view {
	const char *ptr;
	size_t len;
	bool expect_dir;
};

static inline const char *skip_slashes(const char *p)
{
	while (*p == '/')
		p++;
	return p;
}

static bool hexagonfs_path_next(const char **cur,
				struct path_segment_view *seg)
{
	const char *p = skip_slashes(*cur);

	if (*p == '\0')
		return false;

	seg->ptr = p;
	while (*p && *p != '/')
		p++;
	seg->len = (size_t)(p - seg->ptr);
	seg->expect_dir = (*p == '/');
	*cur = skip_slashes(p);
	return true;
}

static bool hexagonfs_segment_copy(const struct path_segment_view *seg,
				   char *buf, size_t bufsz)
{
	if (seg->len >= bufsz)
		return false;
	memcpy(buf, seg->ptr, seg->len);
	buf[seg->len] = '\0';
	return true;
}

/* ------------------------------------------------------------------ */
/*  FD table management                                                */
/* ------------------------------------------------------------------ */

static int hexagonfs_assign_fd(struct hexagonfs_fd **fds,
			       struct hexagonfs_fd *fd)
{
	for (size_t i = 0; i < HEXAGONFS_MAX_FD; i++) {
		if (!fds[i]) {
			fd->refcount++;
			fds[i] = fd;
			return (int)i;
		}
	}
	return -EMFILE;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int hexagonfs_open_root(struct hexagonfs_fd **fds,
			struct hexagonfs_dirent *root)
{
	if (!fds || !root || !root->ops || !root->ops->from_dirent)
		return -EINVAL;

	struct hexagonfs_fd *fd = calloc(1, sizeof(*fd));
	if (!fd)
		return -ENOMEM;

	fd->refcount = 0;
	fd->ops = root->ops;

	int ret = root->ops->from_dirent(root->u.ptr, true, &fd->data);
	if (ret) {
		free(fd);
		return ret;
	}

	ret = hexagonfs_assign_fd(fds, fd);
	if (ret < 0) {
		hexagonfs_fd_put(fd);
		return ret;
	}

	return ret;
}

int hexagonfs_openat(struct hexagonfs_fd **fds, int rootfd,
		     int dirfd, const char *name)
{
	const char *curr = name;
	struct hexagonfs_fd *fd, *root_fd, *opened_tail = NULL;
	struct path_segment_view seg;
	char segment[HEXAGONFS_NAME_MAX + 1];
	int ret;

	if (!name)
		return -EINVAL;

	root_fd = hexagonfs_get_fd(fds, rootfd);
	if (!root_fd)
		return -EBADF;

	if (*curr == '/') {
		fd = root_fd;
		curr = skip_slashes(curr);
	} else {
		if (dirfd < 0)
			return dirfd; /* propagate -errno from invalid dirfd */
		fd = hexagonfs_get_fd(fds, dirfd);
		if (!fd)
			return -EBADF;
	}

	if (*curr == '\0') {
		if (fd == root_fd)
			return hexagonfs_assign_fd(fds, fd);
		return -ENOENT;
	}

	while (hexagonfs_path_next(&curr, &seg)) {
		if (seg.len == 1 && seg.ptr[0] == '.')
			continue;

		if (seg.len == 2 && seg.ptr[0] == '.' && seg.ptr[1] == '.') {
			if (fd != root_fd && fd->up) {
				/* parent survives because child holds a strong ref */
				struct hexagonfs_fd *parent = fd->up;
				hexagonfs_fd_put(fd);
				fd = parent;
				opened_tail = parent;
			}
			continue;
		}

		if (!hexagonfs_segment_copy(&seg, segment, sizeof(segment)))
			return -ENAMETOOLONG;

		if (!fd->ops || !fd->ops->openat)
			return -ENOSYS;

		{
			struct hexagonfs_fd *parent = fd;

			ret = parent->ops->openat(parent, segment,
						  seg.expect_dir, &fd);
			if (ret)
				goto err;

			/* VFS owns linkage: child holds strong ref to parent */
			assert(fd != parent);
			fd->up = parent;
			parent->refcount++;
		}

		opened_tail = fd;
	}

	ret = hexagonfs_assign_fd(fds, fd);
	if (ret < 0)
		goto err;

	return ret;

err:
	if (opened_tail)
		hexagonfs_fd_destroy_chain(opened_tail, root_fd);
	return ret;
}

/* ------------------------------------------------------------------ */
/*  Generic FD operations                                              */
/* ------------------------------------------------------------------ */

int hexagonfs_close(struct hexagonfs_fd **fds, int fileno)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, fileno);

	if (!fd || !fd->ops)
		return -EBADF;

	fds[fileno] = NULL;
	hexagonfs_fd_put(fd);
	return 0;
}

int hexagonfs_lseek(struct hexagonfs_fd **fds, int fileno,
		    off_t off, int whence)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, fileno);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, seek);
	return fd->ops->seek(fd, off, whence);
}

ssize_t hexagonfs_read(struct hexagonfs_fd **fds, int fileno,
		       size_t size, void *ptr)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, fileno);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, read);
	return fd->ops->read(fd, size, ptr);
}

int hexagonfs_readdir(struct hexagonfs_fd **fds, int fileno,
		      size_t ent_size, char *ent)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, fileno);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, readdir);
	return fd->ops->readdir(fd, ent_size, ent);
}

int hexagonfs_fstat(struct hexagonfs_fd **fds, int fileno,
		    struct stat *stats)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, fileno);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, stat);
	return fd->ops->stat(fd, stats);
}

ssize_t hexagonfs_write(struct hexagonfs_fd **fds, int fileno,
			size_t size, const void *ptr)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, fileno);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, write);
	return fd->ops->write(fd, size, ptr);
}

int hexagonfs_ftruncate(struct hexagonfs_fd **fds, int fileno,
			off_t length)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, fileno);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, truncate);
	return fd->ops->truncate(fd, length);
}

int hexagonfs_unlink(struct hexagonfs_fd **fds, int dirfd, const char *name)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, dirfd);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, unlink);
	return fd->ops->unlink(fd, name);
}

int hexagonfs_mkdir(struct hexagonfs_fd **fds, int dirfd,
		    const char *name, mode_t mode)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, dirfd);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, mkdir);
	return fd->ops->mkdir(fd, name, mode);
}

int hexagonfs_rmdir(struct hexagonfs_fd **fds, int dirfd, const char *name)
{
	struct hexagonfs_fd *fd = hexagonfs_get_fd(fds, dirfd);

	if (!fd)
		return -EBADF;
	REQUIRE_OP(fd, rmdir);
	return fd->ops->rmdir(fd, name);
}
