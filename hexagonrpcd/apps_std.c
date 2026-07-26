/*
 * FastRPC operating system interface implementation
 *
 * Copyright (C) 2023-2025 The HexagonRPC Contributors
 *
 * This file is part of HexagonRPC.
 *
 * HexagonRPC is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aee_error.h"
#include "interfaces/apps_std.def"
#include "hexagonfs.h"
#include "iobuffer.h"
#include "listener.h"

struct apps_std_ctx {
	int rootfd;
	int adsp_avs_cfg_dirfd;
	int adsp_library_dirfd;
	struct hexagonfs_fd *fds[HEXAGONFS_MAX_FD];
	bool fd_eof[HEXAGONFS_MAX_FD];
	bool fd_err[HEXAGONFS_MAX_FD];
};

static const int apps_std_whence_table[] = {
	SEEK_SET,
	SEEK_CUR,
	SEEK_END,
};
/* Validate a FastRPC buffer contains a NUL-terminated string */
static inline bool valid_cstring(const struct fastrpc_io_buffer *buf)
{
	return buf && buf->p && buf->s > 0 && buf->s < (size_t)-1 / 2
	       && ((const char *)buf->p)[buf->s - 1] == 0;
}

/* Validate inbuf/outbuf minimum size */

static inline bool output_ok(const struct fastrpc_io_buffer *buf, size_t min_sz)
{
	return buf && buf->p && buf->s >= min_sz;
}

static inline bool buffer_ok(const struct fastrpc_io_buffer *buf, size_t min_sz)
{
	return buf && buf->p && buf->s >= min_sz;
}


static uint32_t apps_std_fopen(void *data,
			       const struct fastrpc_io_buffer *inbufs,
			       struct fastrpc_io_buffer *outbufs)
{
	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	struct apps_std_ctx *ctx = data;
	uint32_t *out = outbufs[0].p;
	int fd;

	if (!output_ok(&outbufs[0], sizeof(*out))) return AEE_EBADPARM;

	/* inbufs[0] = prim: [name_len, mode_len]
	 * inbufs[1] = name (NUL-terminated)
	 * inbufs[2] = mode (NUL-terminated) */

	if (!valid_cstring(&inbufs[1])
	 || !valid_cstring(&inbufs[2]))
		return AEE_EBADPARM;

	/* Try searching from the library path first, then root */
	fd = hexagonfs_openat(ctx->fds, ctx->rootfd,
			      ctx->adsp_library_dirfd, inbufs[1].p);
	if (fd < 0) {
		fd = hexagonfs_openat(ctx->fds, ctx->rootfd,
				      ctx->rootfd, inbufs[1].p);
	}
	if (fd < 0) {
		fprintf(stderr, "Could not open %s: %s\n",
			(const char *)inbufs[1].p, strerror(-fd));
		return AEE_EFAILED;
	}

#ifdef HEXAGONRPC_VERBOSE
	ctx->fd_eof[fd] = false;
	ctx->fd_err[fd] = false;
	printf("fopen(%s) -> %d\n", (const char *)inbufs[1].p, fd);
#endif

	*out = fd;
	return 0;
}

/*
 * This is a placeholder function used to complete any I/O operations.
 * File descriptors do not have a flush operation because their reads and
 * writes are blocking.
 */
static uint32_t apps_std_fflush(void *data,
				const struct fastrpc_io_buffer *inbufs,
				struct fastrpc_io_buffer *outbufs)
{
#ifdef HEXAGONRPC_VERBOSE
	uint32_t *fd = inbufs[0].p;

	printf("ignore fflush(%u)\n", *fd);
#endif

	memset(outbufs[0].p, 0, outbufs[0].s);

	return 0;
}

static uint32_t apps_std_fclose(void *data,
				const struct fastrpc_io_buffer *inbufs,
				struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	uint32_t fd = *first_in;
	int ret;

	if (fd < HEXAGONFS_MAX_FD) {
		ctx->fd_eof[fd] = false;
		ctx->fd_err[fd] = false;
	}

	ret = hexagonfs_close(ctx->fds, fd);
	if (ret) {
		fprintf(stderr, "Could not close: %s\n", strerror(-ret));
		return AEE_EFAILED;
	}

#ifdef HEXAGONRPC_VERBOSE
	printf("close(%u)\n", fd);
#endif

	return 0;
}

static uint32_t apps_std_fread(void *data,
			       const struct fastrpc_io_buffer *inbufs,
			       struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t buf_size;
	} *first_in = inbufs[0].p;
	struct {
		uint32_t written;
		uint32_t is_eof;
	} *first_out = outbufs[0].p;
	uint32_t fd = first_in->fd;
	if (fd >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	ssize_t ret;

	ret = hexagonfs_read(ctx->fds, fd,
			     first_in->buf_size, outbufs[1].p);
	if (ret < 0) {
		fprintf(stderr, "Could not read file: %s\n", strerror(-ret));
		ctx->fd_err[fd] = true;
		return AEE_EFAILED;
	}

#ifdef HEXAGONRPC_VERBOSE
	printf("read(%u, %u) -> %ld\n", fd,
					first_in->buf_size,
					ret);
#endif

	if (ret < (ssize_t)first_in->buf_size) {
		ctx->fd_eof[fd] = true;
	} else {
		/* If we read exactly buf_size, we might-or-might-not be at EOF.
		 * Reset the cached flag; a subsequent read will tell us. */
		ctx->fd_eof[fd] = false;
	}

	first_out->written = ret;
	first_out->is_eof = ctx->fd_eof[fd];

	return 0;
}

static uint32_t apps_std_fseek(void *data,
			       const struct fastrpc_io_buffer *inbufs,
			       struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t pos;
		uint32_t whence;
	} *first_in = inbufs[0].p;
	int ret;
	int whence;

	if (first_in->whence >= 3)
		return AEE_EBADPARM;
	whence = apps_std_whence_table[first_in->whence];

	if (first_in->fd >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	ret = hexagonfs_lseek(ctx->fds, first_in->fd, first_in->pos, whence);
	if (ret) {
		fprintf(stderr, "Could not seek stream: %s\n", strerror(-ret));
		return AEE_EFAILED;
	}

#ifdef HEXAGONRPC_VERBOSE
	printf("lseek(%u, %d, %d)\n", first_in->fd,
				      first_in->pos,
				      first_in->whence);
#endif

	return 0;
}

static uint32_t apps_std_fopen_with_env(void *data,
					const struct fastrpc_io_buffer *inbufs,
					struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	uint32_t *out = outbufs[0].p;
	int dirfd, fd;

	/* inbufs[1] = envvar, inbufs[3] = name, inbufs[4] = mode */
	if (inbufs[3].s == 0 || ((const char *)inbufs[3].p)[0] == 0)
		return AEE_EBADPARM;

	// The name and environment variable must also be NULL-terminated
	if (!valid_cstring(&inbufs[1])
	 || !valid_cstring(&inbufs[3])
	 || !valid_cstring(&inbufs[4]))
		return AEE_EBADPARM;

	if (!strcmp(inbufs[1].p, "ADSP_LIBRARY_PATH"))
		dirfd = ctx->adsp_library_dirfd;
	else if (!strcmp(inbufs[1].p, "ADSP_AVS_CFG_PATH"))
		dirfd = ctx->adsp_avs_cfg_dirfd;
	else {
		fprintf(stderr, "Unknown search path: %s\n",
			(const char *)inbufs[1].p);
		return AEE_EBADPARM;
	}

	if (dirfd < 0)
		return AEE_EFAILED;

	fd = hexagonfs_openat(ctx->fds, ctx->rootfd, dirfd, inbufs[3].p);
	if (fd < 0) {
		fprintf(stderr, "File not found: %s\n",
			(const char *)inbufs[3].p);
		return AEE_EFAILED;
	}

#ifdef HEXAGONRPC_VERBOSE
	printf("openat($%s, %s) -> %d\n", (const char *) inbufs[1].p,
					      (const char *) inbufs[3].p,
					      fd);
#endif

	*out = fd;

	return 0;
}

static uint32_t apps_std_flen(void *data,
			      const struct fastrpc_io_buffer *inbufs,
			      struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	if (!buffer_ok(&inbufs[0], sizeof(*first_in))) return AEE_EBADPARM;
	uint64_t *len = outbufs[0].p;
	struct stat st;
	int ret;

	if (*first_in >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	ret = hexagonfs_fstat(ctx->fds, *first_in, &st);
	if (ret) {
		fprintf(stderr, "Could not flen fd %u: %s\n",
			*first_in, strerror(-ret));
		return AEE_EFAILED;
	}

	*len = st.st_size;

#ifdef HEXAGONRPC_VERBOSE
	printf("flen(%u) -> %lu\n", *first_in, *len);
#endif

	return 0;
}

static uint32_t apps_std_ftell(void *data,
			       const struct fastrpc_io_buffer *inbufs,
			       struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	uint32_t *pos = outbufs[0].p;
	off_t ret;

	if (*first_in >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	ret = hexagonfs_lseek(ctx->fds, *first_in, 0, SEEK_CUR);
	if (ret < 0) {
		fprintf(stderr, "Could not ftell fd %u: %s\n",
			*first_in, strerror(-ret));
		return AEE_EFAILED;
	}

	*pos = ret;

#ifdef HEXAGONRPC_VERBOSE
	printf("ftell(%u) -> %u\n", *first_in, *pos);
#endif

	return 0;
}

static uint32_t apps_std_rewind(void *data,
				const struct fastrpc_io_buffer *inbufs,
				struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	int ret;

	if (*first_in >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;

	ret = hexagonfs_lseek(ctx->fds, *first_in, 0, SEEK_SET);
	if (ret) {
		fprintf(stderr, "Could not rewind fd %u: %s\n",
			*first_in, strerror(-ret));
		return AEE_EFAILED;
	}

	ctx->fd_eof[*first_in] = false;
	ctx->fd_err[*first_in] = false;

#ifdef HEXAGONRPC_VERBOSE
	printf("rewind(%u)\n", *first_in);
#endif

	return 0;
}

static uint32_t apps_std_feof(void *data,
			      const struct fastrpc_io_buffer *inbufs,
			      struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	if (!buffer_ok(&inbufs[0], sizeof(*first_in))) return AEE_EBADPARM;
	uint32_t *b_eof = outbufs[0].p;

	if (*first_in >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	*b_eof = ctx->fd_eof[*first_in] ? 1 : 0;

	return 0;
}

static uint32_t apps_std_ferror(void *data,
				const struct fastrpc_io_buffer *inbufs,
				struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	if (!buffer_ok(&inbufs[0], sizeof(*first_in))) return AEE_EBADPARM;
	uint32_t *err = outbufs[0].p;

	if (*first_in >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	*err = ctx->fd_err[*first_in] ? 1 : 0;

	return 0;
}

static uint32_t apps_std_clearerr(void *data,
				  const struct fastrpc_io_buffer *inbufs,
				  struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;

	if (*first_in >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	ctx->fd_eof[*first_in] = false;
	ctx->fd_err[*first_in] = false;

	return 0;
}

static uint32_t apps_std_print_string(void *data,
				      const struct fastrpc_io_buffer *inbufs,
				      struct fastrpc_io_buffer *outbufs)
{
	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	printf("DSP: %s\n", (const char *)inbufs[1].p);

	return 0;
}

static uint32_t apps_std_fileExists(void *data,
				    const struct fastrpc_io_buffer *inbufs,
				    struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	uint32_t *exists = outbufs[0].p;
	struct stat st;
	int fd, ret;

	if (inbufs[1].s == 0 || ((const char *)inbufs[1].p)[0] == 0)
		return AEE_EBADPARM;

	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	fd = hexagonfs_openat(ctx->fds, ctx->rootfd,
			      ctx->adsp_library_dirfd, inbufs[1].p);
	if (fd < 0) {
		*exists = 0;
		return 0;
	}

	ret = hexagonfs_fstat(ctx->fds, fd, &st);
	hexagonfs_close(ctx->fds, fd);

	*exists = (ret == 0) ? 1 : 0;

	return 0;
}

static uint32_t apps_std_fwrite(void *data,
				const struct fastrpc_io_buffer *inbufs,
				struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t buf_len;
	} *first_in = inbufs[0].p;
	struct {
		uint32_t written;
		uint32_t is_eof;
	} *first_out = outbufs[0].p;
	ssize_t ret;

	if (first_in->fd >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	if (!buffer_ok(&inbufs[0], sizeof(*first_in))) return AEE_EBADPARM;
	ret = hexagonfs_write(ctx->fds, first_in->fd,
			      first_in->buf_len, inbufs[1].p);
	if (ret < 0) {
		ctx->fd_err[first_in->fd] = true;
		fprintf(stderr, "Could not write to fd %u: %s\n",
			first_in->fd, strerror(-ret));
		return AEE_EFAILED;
	}

#ifdef HEXAGONRPC_VERBOSE
	printf("write(%u, %u) -> %ld\n", first_in->fd,
					first_in->buf_len, ret);
#endif

	first_out->written = ret;
	first_out->is_eof = 0;

	return 0;
}

static uint32_t apps_std_fremove(void *data,
				 const struct fastrpc_io_buffer *inbufs,
				 struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	int ret;

	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	ret = hexagonfs_unlink(ctx->fds, ctx->rootfd, inbufs[1].p);
	if (ret) {
		fprintf(stderr, "Could not remove %s: %s\n",
			(const char *)inbufs[1].p, strerror(-ret));
		return AEE_EFAILED;
	}

	return 0;
}

static uint32_t apps_std_mkdir(void *data,
			       const struct fastrpc_io_buffer *inbufs,
			       struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t mode;
		uint32_t name_len;
	} *first_in = inbufs[0].p;
	int ret;

	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	ret = hexagonfs_mkdir(ctx->fds, ctx->rootfd,
			      inbufs[1].p, first_in->mode);
	if (ret) {
		fprintf(stderr, "Could not mkdir %s: %s\n",
			(const char *)inbufs[1].p, strerror(-ret));
		return AEE_EFAILED;
	}

	return 0;
}

static uint32_t apps_std_rmdir(void *data,
			       const struct fastrpc_io_buffer *inbufs,
			       struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;

	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	int ret = hexagonfs_rmdir(ctx->fds, ctx->rootfd, inbufs[1].p);
	if (ret) {
		fprintf(stderr, "Could not rmdir %s: %s\n",
			(const char *)inbufs[1].p, strerror(-ret));
		return AEE_EFAILED;
	}

	return 0;
}

static uint32_t apps_std_ftrunc(void *data,
				const struct fastrpc_io_buffer *inbufs,
				struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t offset;
	} *first_in = inbufs[0].p;
	int ret;

	if (first_in->fd >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
	if (!buffer_ok(&inbufs[0], sizeof(*first_in))) return AEE_EBADPARM;
	ret = hexagonfs_ftruncate(ctx->fds, first_in->fd, first_in->offset);
	if (ret) {
		fprintf(stderr, "Could not ftrunc fd %u: %s\n",
			first_in->fd, strerror(-ret));
		return AEE_EFAILED;
	}

	return 0;
}

static uint32_t apps_std_fsync(void *data,
			       const struct fastrpc_io_buffer *inbufs,
			       struct fastrpc_io_buffer *outbufs)
{
	/* POSIX write-through semantics: data is already on its way.
	 * A no-op is safe here. */
	return 0;
}

static uint32_t apps_std_fdopen_decrypt(void *data,
					const struct fastrpc_io_buffer *inbufs,
					struct fastrpc_io_buffer *outbufs)
{
	/* No decryption needed on Linux; return the same fd */
	const uint32_t *first_in = inbufs[0].p;
	uint32_t *psout = outbufs[0].p;

	*psout = *first_in;
	return 0;
}

static uint32_t apps_std_frename(void *data,
				 const struct fastrpc_io_buffer *inbufs,
				 struct fastrpc_io_buffer *outbufs)
{
	if (!valid_cstring(&inbufs[1])
	 || !valid_cstring(&inbufs[2]))
		return AEE_EBADPARM;

	if (rename((const char *)inbufs[1].p, (const char *)inbufs[2].p))
		return AEE_EFAILED;

	return 0;
}

static uint32_t apps_std_fopen_fd(void *data,
				  const struct fastrpc_io_buffer *inbufs,
				  struct fastrpc_io_buffer *outbufs)
{
	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	struct apps_std_ctx *ctx = data;
	/* inbufs[0] = prim: [name_len, mode_len]
	 * inbufs[1] = name, inbufs[2] = mode
	 * outbufs[0] = prim: [fd, len] */
	uint32_t *out = outbufs[0].p;
	struct stat st;
	int fd;

	if (!valid_cstring(&inbufs[1])
	 || !valid_cstring(&inbufs[2]))
		return AEE_EBADPARM;

	fd = hexagonfs_openat(ctx->fds, ctx->rootfd,
			      ctx->adsp_library_dirfd, inbufs[1].p);
	if (fd < 0) {
		fd = hexagonfs_openat(ctx->fds, ctx->rootfd,
				      ctx->rootfd, inbufs[1].p);
	}
	if (fd < 0) {
		fprintf(stderr, "Could not fopen_fd %s: %s\n",
			(const char *)inbufs[1].p, strerror(-fd));
		return AEE_EFAILED;
	}

	if (hexagonfs_fstat(ctx->fds, fd, &st)) {
		hexagonfs_close(ctx->fds, fd);
		return AEE_EFAILED;
	}

	ctx->fd_eof[fd] = false;
	ctx->fd_err[fd] = false;
	out[0] = fd;
	out[1] = (uint32_t)st.st_size;

	return 0;
}

static uint32_t apps_std_fclose_fd(void *data,
				   const struct fastrpc_io_buffer *inbufs,
				   struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;

	return hexagonfs_close(ctx->fds, *first_in) ? AEE_EFAILED : 0;
}

static uint32_t apps_std_fopen_with_env_fd(void *data,
					   const struct fastrpc_io_buffer *inbufs,
					   struct fastrpc_io_buffer *outbufs)
{
	if (inbufs[1].s == 0 || ((const char *)inbufs[1].p)[0] == 0)
		return AEE_EBADPARM;
	if (inbufs[3].s == 0 || ((const char *)inbufs[3].p)[0] == 0)
		return AEE_EBADPARM;

	struct apps_std_ctx *ctx = data;
	/* inbufs[0] = prim: [envvar_len, delim_len, name_len, mode_len]
	 * inbufs[1] = envvar, inbufs[2] = delim
	 * inbufs[3] = name, inbufs[4] = mode
	 * outbufs[0] = prim: [fd, len] */
	uint32_t *out = outbufs[0].p;
	struct stat st;
	int dirfd, fd;

	if (!valid_cstring(&inbufs[3])
	 || !valid_cstring(&inbufs[4]))
		return AEE_EBADPARM;

	if (!strcmp(inbufs[1].p, "ADSP_LIBRARY_PATH"))
		dirfd = ctx->adsp_library_dirfd;
	else if (!strcmp(inbufs[1].p, "ADSP_AVS_CFG_PATH"))
		dirfd = ctx->adsp_avs_cfg_dirfd;
	else
		return AEE_EUNSUPPORTED;

	if (dirfd < 0)
		return AEE_EFAILED;

	fd = hexagonfs_openat(ctx->fds, ctx->rootfd, dirfd, inbufs[3].p);
	if (fd < 0)
		return AEE_EFAILED;

	if (hexagonfs_fstat(ctx->fds, fd, &st)) {
		hexagonfs_close(ctx->fds, fd);
		return AEE_EFAILED;
	}

	ctx->fd_eof[fd] = false;
	ctx->fd_err[fd] = false;
	out[0] = fd;
	out[1] = (uint32_t)st.st_size;

	return 0;
}

static uint32_t apps_std_get_search_paths_with_env(void *data,
						   const struct fastrpc_io_buffer *inbufs,
						   struct fastrpc_io_buffer *outbufs)
{
	/*
	 * Returns an empty search path list (numPaths=0, maxPathLen=0).
	 *
	 * Full slim-type sequence encoding is not yet implemented.
	 * HexagonFS transparently remaps Android paths to Linux paths,
	 * so the DSP does not need to discover search paths at runtime
	 * — fopen_with_env already resolves via the configured directories.
	 *
	 * Wire format: prim outbuf = [numPaths:uint32, maxPathLen:uint16 + 2B pad]
	 */
	uint32_t *prim_out = outbufs[0].p;
	prim_out[0] = 0; /* numPaths */
	prim_out[1] = 0; /* maxPathLen (uint16) */
	return 0;
}

static uint32_t apps_std_fgets(void *data,
			       const struct fastrpc_io_buffer *inbufs,
			       struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t buf_size;
	} *first_in = inbufs[0].p;
	uint32_t *b_eof = outbufs[0].p;
	char *buf = outbufs[1].p;
	ssize_t total = 0;
	int ret;
	char c;

	/* Read up to buf_size bytes, stop at newline or EOF */
	while (total < (ssize_t)first_in->buf_size - 1) {
		if (first_in->fd >= HEXAGONFS_MAX_FD) return AEE_EBADPARM;
		ret = hexagonfs_read(ctx->fds, first_in->fd, 1, &c);
		if (ret < 0) {
			ctx->fd_err[first_in->fd] = true;
			return AEE_EFAILED;
		}
		if (ret == 0) {
			ctx->fd_eof[first_in->fd] = true;
			break;
		}
		buf[total++] = c;
		if (c == '\n')
			break;
	}

	buf[total] = '\0';
	*b_eof = ctx->fd_eof[first_in->fd] ? 1 : 0;

#ifdef HEXAGONRPC_VERBOSE
	printf("fgets(%u, %u) -> %ld \"%s\"\n",
	       first_in->fd, first_in->buf_size, (long)total, buf);
#endif

	return 0;
}

static uint32_t apps_std_freopen(void *data,
				 const struct fastrpc_io_buffer *inbufs,
				 struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t sin;
		uint32_t name_len;
		uint32_t mode_len;
	} *first_in = inbufs[0].p;
	uint32_t *psout = outbufs[0].p;
	if (!output_ok(&outbufs[0], sizeof(*psout))) return AEE_EBADPARM;

	if (!valid_cstring(&inbufs[1])
	 || !valid_cstring(&inbufs[2]))
		return AEE_EBADPARM;

	/* Close old fd, open new one */
	hexagonfs_close(ctx->fds, first_in->sin);

	int fd = hexagonfs_openat(ctx->fds, ctx->rootfd,
				  ctx->adsp_library_dirfd, inbufs[1].p);
	if (fd < 0) {
		fd = hexagonfs_openat(ctx->fds, ctx->rootfd,
				      ctx->rootfd, inbufs[1].p);
	}
	if (fd < 0)
		return AEE_EFAILED;

	*psout = fd;
	return 0;
}

static uint32_t apps_std_fgetpos(void *data,
				 const struct fastrpc_io_buffer *inbufs,
				 struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t pos_max;
	} *first_in = inbufs[0].p;
	uint32_t *pos_len_req = outbufs[0].p;
	off_t pos;

	pos = hexagonfs_lseek(ctx->fds, first_in->fd, 0, SEEK_CUR);
	if (pos < 0)
		return AEE_EFAILED;

	/* Store the position as fpos_t in the output buffer */
	*pos_len_req = sizeof(off_t);
	if (*pos_len_req > first_in->pos_max)
		*pos_len_req = first_in->pos_max;

	if (*pos_len_req > outbufs[1].s)
		*pos_len_req = outbufs[1].s;

	memcpy(outbufs[1].p, &pos, *pos_len_req);

	return 0;
}

static uint32_t apps_std_fsetpos(void *data,
				 const struct fastrpc_io_buffer *inbufs,
				 struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t pos_len;
	} *first_in = inbufs[0].p;
	off_t pos = 0;

	if (first_in->pos_len > sizeof(pos))
		return AEE_EBADPARM;

	memcpy(&pos, inbufs[1].p, first_in->pos_len);

	int ret = hexagonfs_lseek(ctx->fds, first_in->fd, pos, SEEK_SET);
	if (ret)
		return AEE_EFAILED;

	return 0;
}

static uint32_t apps_std_getenv(void *data,
				const struct fastrpc_io_buffer *inbufs,
				struct fastrpc_io_buffer *outbufs)
{
	/* inbufs[0] = prim: [name_len, val_max_size]
	 * inbufs[1] = name (NUL-terminated)
	 * outbufs[0] = prim: [val_len_req]
	 * outbufs[1] = val data */
	const struct {
		uint32_t name_len;
		uint32_t val_size;
	} *first_in = inbufs[0].p;
	const char *name = inbufs[1].p;
	uint32_t *val_len_req = outbufs[0].p;
	char *val = outbufs[1].p;
	const char *env_val;

	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	env_val = getenv(name);
	if (env_val == NULL) {
		*val_len_req = 0;
		return 0;
	}

	*val_len_req = strlen(env_val) + 1;
	snprintf(val, first_in->val_size, "%s", env_val);

	return 0;
}

static uint32_t apps_std_setenv(void *data,
				const struct fastrpc_io_buffer *inbufs,
				struct fastrpc_io_buffer *outbufs)
{
	/* inbufs[0] = prim: [override, name_len, val_len]
	 * inbufs[1] = name (NUL-terminated)
	 * inbufs[2] = val (NUL-terminated) */
	const struct {
		uint32_t override;
		uint32_t name_len;
		uint32_t val_len;
	} *first_in = inbufs[0].p;
	if (!buffer_ok(&inbufs[0], 12)) return AEE_EBADPARM;
	const char *name = inbufs[1].p;
	const char *val = inbufs[2].p;

	if (!valid_cstring(&inbufs[1])
	 || !valid_cstring(&inbufs[2]))
		return AEE_EBADPARM;

	if (setenv(name, val, first_in->override))
		return AEE_EFAILED;

	return 0;
}

static uint32_t apps_std_unsetenv(void *data,
				  const struct fastrpc_io_buffer *inbufs,
				  struct fastrpc_io_buffer *outbufs)
{
	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	unsetenv((const char *)inbufs[1].p);

	return 0;
}

static uint32_t apps_std_opendir(void *data,
				 const struct fastrpc_io_buffer *inbufs,
				 struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	uint64_t *dir_out = outbufs[0].p;
	int ret;

	// The name must be NULL-terminated
	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	ret = hexagonfs_openat(ctx->fds, ctx->rootfd, ctx->rootfd, inbufs[1].p);
	if (ret < 0) {
		fprintf(stderr, "Could not open %s: %s\n",
				(const char *) inbufs[1].p,
				strerror(-ret));
		return AEE_EFAILED;
	}

#ifdef HEXAGONRPC_VERBOSE
	printf("opendir(%s) -> %d\n", (const char *) inbufs[1].p, ret);
#endif

	*dir_out = ret;

	return 0;
}

static uint32_t apps_std_closedir(void *data,
				  const struct fastrpc_io_buffer *inbufs,
				  struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint64_t *dir = inbufs[0].p;
	int ret;

	ret = hexagonfs_close(ctx->fds, *dir);
	if (ret)
		return AEE_EFAILED;

#ifdef HEXAGONRPC_VERBOSE
	printf("closedir(%ld)\n", *dir);
#endif

	return 0;
}

static uint32_t apps_std_readdir(void *data,
				 const struct fastrpc_io_buffer *inbufs,
				 struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	const uint64_t *dir = inbufs[0].p;
	struct {
		uint32_t inode;
		char name[255];
		uint32_t is_eof;
	} *first_out = outbufs[0].p;
	int ret;

	ret = hexagonfs_readdir(ctx->fds, *dir, 255, first_out->name);
	if (ret < 0) {
		fprintf(stderr, "Could not read from directory: %s\n",
				strerror(-ret));
		return AEE_EFAILED;
	}

#ifdef HEXAGONRPC_VERBOSE
	printf("readdir(%ld) -> %s\n", *dir, first_out->name);
#endif

	first_out->inode = 0;
	first_out->is_eof = (*first_out->name == '\0');

	return 0;
}

static uint32_t apps_std_stat(void *data,
			      const struct fastrpc_io_buffer *inbufs,
			      struct fastrpc_io_buffer *outbufs)
{
	struct apps_std_ctx *ctx = data;
	struct {
		uint64_t tsz; // Unknown purpose
		uint64_t dev;
		uint64_t ino;
		uint32_t mode;
		uint32_t nlink;
		uint64_t rdev;
		uint64_t size;
		int64_t atime;
		int64_t atimensec;
		int64_t mtime;
		int64_t mtimensec;
		int64_t ctime;
		int64_t ctimensec;
	} *first_out = outbufs[0].p;
	if (!output_ok(&outbufs[0], sizeof(*first_out))) return AEE_EBADPARM;
	const char *pathname = inbufs[1].p;
	struct stat stats;
	int fd, ret;

	if (!valid_cstring(&inbufs[1]))
		return AEE_EBADPARM;

	fd = hexagonfs_openat(ctx->fds, ctx->rootfd, ctx->adsp_library_dirfd, pathname);
	if (fd < 0) {
		fprintf(stderr, "Could not open %s: %s\n",
				pathname, strerror(-fd));
		return AEE_EFAILED;
	}

	ret = hexagonfs_fstat(ctx->fds, fd, &stats);
	if (ret) {
		fprintf(stderr, "Could not stat %s: %s\n",
				pathname, strerror(-fd));
		hexagonfs_close(ctx->fds, fd);
		return AEE_EFAILED;
	}

	hexagonfs_close(ctx->fds, fd);

#ifdef HEXAGONRPC_VERBOSE
	printf("stat(%s)\n", pathname);
#endif

	first_out->tsz = 0;

	first_out->dev = stats.st_dev;
	first_out->ino = stats.st_ino;
	first_out->mode = stats.st_mode;
	first_out->nlink = stats.st_nlink;
	first_out->rdev = stats.st_rdev;
	first_out->size = stats.st_size;
	first_out->atime = (int64_t) stats.st_atim.tv_sec;
	first_out->atimensec = (int64_t) stats.st_atim.tv_nsec;
	first_out->mtime = (int64_t) stats.st_mtim.tv_sec;
	first_out->mtimensec = (int64_t) stats.st_mtim.tv_nsec;
	first_out->ctime = (int64_t) stats.st_ctim.tv_sec;
	first_out->ctimensec = (int64_t) stats.st_ctim.tv_nsec;

	return 0;
}

struct fastrpc_interface *fastrpc_apps_std_init(struct hexagonfs_dirent *root)
{
	struct fastrpc_interface *iface;
	struct apps_std_ctx *ctx;

	iface = malloc(sizeof(struct fastrpc_interface));
	if (iface == NULL)
		return NULL;

	ctx = calloc(1, sizeof(struct apps_std_ctx));
	if (ctx == NULL)
		goto err_free_iface;

	memcpy(iface, &apps_std_interface, sizeof(struct fastrpc_interface));

	ctx->rootfd = hexagonfs_open_root(ctx->fds, root);
	if (ctx->rootfd < 0)
		goto err_free_ctx;

	ctx->adsp_avs_cfg_dirfd = hexagonfs_openat(ctx->fds,
						   ctx->rootfd,
						   ctx->rootfd,
						   "/vendor/etc/acdbdata/");
	ctx->adsp_library_dirfd = hexagonfs_openat(ctx->fds,
						   ctx->rootfd,
						   ctx->rootfd,
						   "/usr/lib/qcom/adsp/");

	/*
	 * Missing virtual paths are not fatal — the DSP may never use them.
	 * The fopen family silently falls back to rootfd when dirfd < 0.
	 */
#ifdef HEXAGONRPC_VERBOSE
	if (ctx->adsp_avs_cfg_dirfd < 0)
		fprintf(stderr, "Note: /vendor/etc/acdbdata/ not in config\n");
	if (ctx->adsp_library_dirfd < 0)
		fprintf(stderr, "Note: /usr/lib/qcom/adsp/ not in config\n");
#endif

	iface->data = ctx;

	return iface;

err_free_ctx:
	if (ctx->adsp_library_dirfd >= 0)
		hexagonfs_close(ctx->fds, ctx->adsp_library_dirfd);
	if (ctx->adsp_avs_cfg_dirfd >= 0)
		hexagonfs_close(ctx->fds, ctx->adsp_avs_cfg_dirfd);
	if (ctx->rootfd >= 0)
		hexagonfs_close(ctx->fds, ctx->rootfd);
	free(ctx);
err_free_iface:
	free(iface);

	return NULL;
}

void fastrpc_apps_std_deinit(struct fastrpc_interface *iface)
{
	struct apps_std_ctx *ctx = iface->data;
	int i;

	for (i = 0; i < HEXAGONFS_MAX_FD; i++) {
		if (ctx->fds[i] != NULL)
			hexagonfs_close(ctx->fds, i);
	}

	free(iface->data);
	free(iface);
}

static const struct fastrpc_function_impl apps_std_procs[] = {
	{
		.def = &apps_std_fopen_def,
		.impl = apps_std_fopen,
	},
	{
		.def = &apps_std_freopen_def,
		.impl = apps_std_freopen,
	},
	{
		.def = &apps_std_fflush_def,
		.impl = apps_std_fflush,
	},
	{
		.def = &apps_std_fclose_def,
		.impl = apps_std_fclose,
	},
	{
		.def = &apps_std_fread_def,
		.impl = apps_std_fread,
	},
	{
		.def = &apps_std_fwrite_def,
		.impl = apps_std_fwrite,
	},
	{
		.def = &apps_std_fgetpos_def,
		.impl = apps_std_fgetpos,
	},
	{
		.def = &apps_std_fsetpos_def,
		.impl = apps_std_fsetpos,
	},
	{
		.def = &apps_std_ftell_def,
		.impl = apps_std_ftell,
	},
	{
		.def = &apps_std_fseek_def,
		.impl = apps_std_fseek,
	},
	{
		.def = &apps_std_flen_def,
		.impl = apps_std_flen,
	},
	{
		.def = &apps_std_rewind_def,
		.impl = apps_std_rewind,
	},
	{
		.def = &apps_std_feof_def,
		.impl = apps_std_feof,
	},
	{
		.def = &apps_std_ferror_def,
		.impl = apps_std_ferror,
	},
	{
		.def = &apps_std_clearerr_def,
		.impl = apps_std_clearerr,
	},
	{
		.def = &apps_std_print_string_def,
		.impl = apps_std_print_string,
	},
	{
		.def = &apps_std_getenv_def,
		.impl = apps_std_getenv,
	},
	{
		.def = &apps_std_setenv_def,
		.impl = apps_std_setenv,
	},
	{
		.def = &apps_std_unsetenv_def,
		.impl = apps_std_unsetenv,
	},
	{
		.def = &apps_std_fopen_with_env_def,
		.impl = apps_std_fopen_with_env,
	},
	{
		.def = &apps_std_fgets_def,
		.impl = apps_std_fgets,
	},
	{
		.def = &apps_std_get_search_paths_with_env_def,
		.impl = apps_std_get_search_paths_with_env,
	},
	{
		.def = &apps_std_fileExists_def,
		.impl = apps_std_fileExists,
	},
	{
		.def = &apps_std_fsync_def,
		.impl = apps_std_fsync,
	},
	{
		.def = &apps_std_fremove_def,
		.impl = apps_std_fremove,
	},
	{
		.def = &apps_std_fdopen_decrypt_def,
		.impl = apps_std_fdopen_decrypt,
	},
	{
		.def = &apps_std_opendir_def,
		.impl = apps_std_opendir,
	},
	{
		.def = &apps_std_closedir_def,
		.impl = apps_std_closedir,
	},
	{
		.def = &apps_std_readdir_def,
		.impl = apps_std_readdir,
	},
	{
		.def = &apps_std_mkdir_def,
		.impl = apps_std_mkdir,
	},
	{
		.def = &apps_std_rmdir_def,
		.impl = apps_std_rmdir,
	},
	{
		.def = &apps_std_stat_def,
		.impl = apps_std_stat,
	},
	{
		.def = &apps_std_ftrunc_def,
		.impl = apps_std_ftrunc,
	},
	{
		.def = &apps_std_frename_def,
		.impl = apps_std_frename,
	},
	{
		.def = &apps_std_fopen_fd_def,
		.impl = apps_std_fopen_fd,
	},
	{
		.def = &apps_std_fclose_fd_def,
		.impl = apps_std_fclose_fd,
	},
	{
		.def = &apps_std_fopen_with_env_fd_def,
		.impl = apps_std_fopen_with_env_fd,
	},
};

const struct fastrpc_interface apps_std_interface = {
	.name = "apps_std",
	.n_procs = sizeof(apps_std_procs) / sizeof(apps_std_procs[0]),
	.procs = apps_std_procs,
};
