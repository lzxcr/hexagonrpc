/* rpcmem_linux.c — DMA-BUF heap allocator via /dev/dma_heap/system */
#include "rpcmem_linux.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef DMA_HEAP_IOCTL_ALLOC
struct dma_heap_allocation_data {
	uint64_t len;
	uint32_t fd;
	uint32_t fd_flags;
	uint64_t heap_flags;
};
#define DMA_HEAP_IOC_MAGIC 0x48
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0, struct dma_heap_allocation_data)
#endif

int rpcmem_alloc(int heap_id, uint32_t flags, size_t size, struct rpcmem_buf **out) {
	struct dma_heap_allocation_data alloc = {
		.len = size, .fd_flags = O_RDWR | O_CLOEXEC };
	struct rpcmem_buf *buf;
	int heap_fd, saved_errno;

	if (!out || size == 0)
		return -EINVAL;

	buf = calloc(1, sizeof(*buf));
	if (!buf) return -ENOMEM;

	buf->fd = -1;
	buf->ptr = NULL;

	heap_fd = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
	if (heap_fd < 0) {
		/* Fallback: try /dev/dma_heap/ for named heaps */
		char heap_path[64];
		snprintf(heap_path, sizeof(heap_path),
			 "/dev/dma_heap/%s",
			 heap_id == RPCMEM_HEAP_DEFAULT ? "system" : "system");
		heap_fd = open(heap_path, O_RDWR | O_CLOEXEC);
		if (heap_fd < 0) {
			saved_errno = errno;
			free(buf);
			return -saved_errno;
		}
	}

	if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
		saved_errno = errno;
		close(heap_fd); free(buf);
		return -saved_errno;
	}
	close(heap_fd);

	buf->fd = alloc.fd;
	buf->size = size;
	buf->ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, buf->fd, 0);
	if (buf->ptr == MAP_FAILED) {
		saved_errno = errno;
		close(buf->fd); free(buf);
		return -saved_errno;
	}
	*out = buf;
	return 0;
}

void rpcmem_free(struct rpcmem_buf *buf) {
	if (!buf) return;
	if (buf->ptr && buf->ptr != MAP_FAILED) munmap(buf->ptr, buf->size);
	if (buf->fd >= 0) close(buf->fd);
	free(buf);
}

int rpcmem_to_fd(struct rpcmem_buf *buf) { return buf ? buf->fd : -1; }
void *rpcmem_ptr(struct rpcmem_buf *buf) { return buf ? buf->ptr : NULL; }
