/*
 * FastRPC memory mapping interface implementation
 * Copyright (C) 2024-2025 The HexagonRPC Contributors
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <misc/fastrpc.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "aee_error.h"
#include "apps_mem.h"
#include "interfaces/apps_mem.def"
#include "listener.h"

#define ADSP_MMAP_ADD_PAGES   0x1000

struct apps_mem_ctx {
	int fd;
};

static uint32_t apps_mem_request_map(void *data,
				     const struct fastrpc_io_buffer *inbufs,
				     struct fastrpc_io_buffer *outbufs)
{
	struct apps_mem_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	uint32_t *first_out = outbufs[0].p;
	struct fastrpc_req_mmap req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.fd = -1;
	req.flags = first_in[2];
	req.size = first_in[4];

	ret = ioctl(ctx->fd, FASTRPC_IOCTL_MMAP, &req);
	if (ret == -1) {
		perror("Memory map failed");
		return AEE_EFAILED;
	} else if (ret) {
		fprintf(stderr, "Memory map failed: %s\n", aee_strerror[ret]);
		return ret;
	}

	first_out[0] = 0;
	first_out[1] = (uint32_t)(req.vaddrout & 0xFFFFFFFF);
	return 0;
}

static uint32_t apps_mem_request_unmap(void *data,
				       const struct fastrpc_io_buffer *inbufs,
				       struct fastrpc_io_buffer *outbufs)
{
	struct apps_mem_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	struct fastrpc_req_munmap req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.vaddrout = first_in[0];
	req.size = first_in[1];

	ret = ioctl(ctx->fd, FASTRPC_IOCTL_MUNMAP, &req);
	if (ret == -1) {
		perror("Memory unmap failed");
		return AEE_EFAILED;
	} else if (ret) {
		fprintf(stderr, "Memory unmap failed: %s\n", aee_strerror[ret]);
		return ret;
	}

	return 0;
}

static uint32_t apps_mem_request_map64(void *data,
				       const struct fastrpc_io_buffer *inbufs,
				       struct fastrpc_io_buffer *outbufs)
{
	struct apps_mem_ctx *ctx = data;
	const struct {
		uint32_t heap_id;
		uint32_t lflags;
		uint32_t rflags;
		uint32_t padding;
		uint64_t vin;
		uint64_t len;
	} *first_in = inbufs[0].p;
	struct {
		uint64_t vapps;
		uint64_t vadsp;
	} *first_out = outbufs[0].p;
	struct fastrpc_req_mmap req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.fd = -1;
	req.flags = first_in->rflags;
	req.size = first_in->len;

	ret = ioctl(ctx->fd, FASTRPC_IOCTL_MMAP, &req);
	if (ret == -1) {
		perror("Memory map64 failed");
		return AEE_EFAILED;
	} else if (ret) {
		fprintf(stderr, "Memory map64 failed: %s\n", aee_strerror[ret]);
		return ret;
	}

	first_out->vapps = 0;
	first_out->vadsp = req.vaddrout & 0xFFFFFFFF;
	return 0;
}

static uint32_t apps_mem_request_unmap64(void *data,
					 const struct fastrpc_io_buffer *inbufs,
					 struct fastrpc_io_buffer *outbufs)
{
	struct apps_mem_ctx *ctx = data;
	const uint64_t *first_in = inbufs[0].p;
	struct fastrpc_req_munmap req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.vaddrout = first_in[0];
	req.size = first_in[1];

	ret = ioctl(ctx->fd, FASTRPC_IOCTL_MUNMAP, &req);
	if (ret == -1) {
		perror("Memory unmap64 failed");
		return AEE_EFAILED;
	} else if (ret) {
		fprintf(stderr, "Memory unmap64 failed: %s\n", aee_strerror[ret]);
		return ret;
	}

	return 0;
}

static uint32_t apps_mem_share_map(void *data,
				   const struct fastrpc_io_buffer *inbufs,
				   struct fastrpc_io_buffer *outbufs)
{
	struct apps_mem_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t size;
	} *first_in = inbufs[0].p;
	struct {
		uint64_t vapps;
		uint64_t vadsp;
	} *first_out = outbufs[0].p;
	struct fastrpc_req_mmap req;
	void *buf;
	int ret;

	buf = mmap(NULL, first_in->size, PROT_READ | PROT_WRITE,
		   MAP_SHARED, first_in->fd, 0);
	if (buf == MAP_FAILED) {
		perror("share_map mmap failed");
		return AEE_EFAILED;
	}

	memset(&req, 0, sizeof(req));
	req.fd = first_in->fd;
	req.flags = 0;
	req.size = first_in->size;

	ret = ioctl(ctx->fd, FASTRPC_IOCTL_MMAP, &req);
	if (ret == -1) {
		perror("share_map ioctl failed");
		munmap(buf, first_in->size);
		return AEE_EFAILED;
	} else if (ret) {
		fprintf(stderr, "share_map failed: %s\n", aee_strerror[ret]);
		munmap(buf, first_in->size);
		return ret;
	}

	first_out->vapps = (uint64_t)(uintptr_t)buf;
	first_out->vadsp = req.vaddrout;
	return 0;
}

static uint32_t apps_mem_share_unmap(void *data,
				     const struct fastrpc_io_buffer *inbufs,
				     struct fastrpc_io_buffer *outbufs)
{
	struct apps_mem_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	/* prim_in[3]: vadsp_lo, vadsp_hi, size = 12 bytes */
	struct fastrpc_req_munmap req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.vaddrout = first_in[0] | ((uint64_t)first_in[1] << 32);
	req.size = first_in[2];

	ret = ioctl(ctx->fd, FASTRPC_IOCTL_MUNMAP, &req);
	if (ret == -1) {
		perror("share_unmap failed");
		return AEE_EFAILED;
	} else if (ret) {
		fprintf(stderr, "share_unmap failed: %s\n", aee_strerror[ret]);
		return ret;
	}

	return 0;
}

static uint32_t apps_mem_dma_handle_map(void *data,
					const struct fastrpc_io_buffer *inbufs,
					struct fastrpc_io_buffer *outbufs)
{
	struct apps_mem_ctx *ctx = data;
	const struct {
		uint32_t fd;
		uint32_t offset;
		uint32_t size;
	} *first_in = inbufs[0].p;
	struct fastrpc_mem_map req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.fd = first_in->fd;
	req.offset = first_in->offset;
	req.flags = FASTRPC_MAP_FD;
	req.length = first_in->size;

	ret = ioctl(ctx->fd, FASTRPC_IOCTL_MEM_MAP, &req);
	if (ret == -1) {
		perror("dma_handle_map failed");
		return AEE_EFAILED;
	} else if (ret) {
		fprintf(stderr, "dma_handle_map failed: %s\n", aee_strerror[ret]);
		return ret;
	}

	return 0;
}

static uint32_t apps_mem_dma_handle_unmap(void *data,
					  const struct fastrpc_io_buffer *inbufs,
					  struct fastrpc_io_buffer *outbufs)
{
	struct apps_mem_ctx *ctx = data;
	const uint32_t *first_in = inbufs[0].p;
	struct fastrpc_mem_unmap req;
	int ret;

	memset(&req, 0, sizeof(req));
	req.vesion = 0;
	req.fd = first_in[0];
	req.length = first_in[1];

	ret = ioctl(ctx->fd, FASTRPC_IOCTL_MEM_UNMAP, &req);
	if (ret == -1) {
		perror("dma_handle_unmap failed");
		return AEE_EFAILED;
	} else if (ret) {
		fprintf(stderr, "dma_handle_unmap failed: %s\n", aee_strerror[ret]);
		return ret;
	}

	return 0;
}

/* init/deinit unchanged */
struct fastrpc_interface *fastrpc_apps_mem_init(int fd)
{
	struct fastrpc_interface *iface;
	struct apps_mem_ctx *ctx;

	iface = malloc(sizeof(*iface));
	if (iface == NULL)
		return NULL;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL)
		goto err_free_iface;

	memcpy(iface, &apps_mem_interface, sizeof(*iface));
	ctx->fd = fd;
	iface->data = ctx;
	return iface;

err_free_iface:
	free(iface);
	return NULL;
}

void fastrpc_apps_mem_deinit(struct fastrpc_interface *iface)
{
	free(iface->data);
	free(iface);
}

static const struct fastrpc_function_impl apps_mem_procs[] = {
	{ .def = &apps_mem_request_map_def, .impl = apps_mem_request_map, },
	{ .def = &apps_mem_request_unmap_def, .impl = apps_mem_request_unmap, },
	{ .def = &apps_mem_request_map64_def, .impl = apps_mem_request_map64, },
	{ .def = &apps_mem_request_unmap64_def, .impl = apps_mem_request_unmap64, },
	{ .def = &apps_mem_share_map_def, .impl = apps_mem_share_map, },
	{ .def = &apps_mem_share_unmap_def, .impl = apps_mem_share_unmap, },
	{ .def = &apps_mem_dma_handle_map_def, .impl = apps_mem_dma_handle_map, },
	{ .def = &apps_mem_dma_handle_unmap_def, .impl = apps_mem_dma_handle_unmap, },
};

const struct fastrpc_interface apps_mem_interface = {
	.name = "apps_mem",
	.n_procs = 8,
	.procs = apps_mem_procs,
};
