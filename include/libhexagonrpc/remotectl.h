/* remotectl.h — shared remotectl helpers */
#ifndef LIBHEXAGONRPC_REMOTECTL_H
#define LIBHEXAGONRPC_REMOTECTL_H

#include <libhexagonrpc/fastrpc.h>

int remotectl_open(int fd, const char *name, struct fastrpc_context **ctx);
int remotectl_close(struct fastrpc_context *ctx);

#endif
