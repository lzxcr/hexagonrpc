/* rpcmem_linux.h — Linux DMA-BUF heap allocator */
#ifndef RPCMEM_LINUX_H
#define RPCMEM_LINUX_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define RPCMEM_HEAP_DEFAULT 0
#define RPCMEM_HEAP_SYSTEM  1

struct rpcmem_buf {
	int fd;
	void *ptr;
	size_t size;
};

int rpcmem_alloc(int heap_id, uint32_t flags, size_t size, struct rpcmem_buf **out);
void rpcmem_free(struct rpcmem_buf *buf);
int rpcmem_to_fd(struct rpcmem_buf *buf);
void *rpcmem_ptr(struct rpcmem_buf *buf);

#endif
