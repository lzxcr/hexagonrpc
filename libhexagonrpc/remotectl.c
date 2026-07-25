/* remotectl.c — shared remotectl open/close helpers */
#include <libhexagonrpc/remotectl.h>
#include <libhexagonrpc/interfaces/remotectl.def>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int remotectl_open(int fd, const char *name, struct fastrpc_context **ctx)
{
	uint32_t handle, dlret;
	char err[256];
	int ret;

	ret = fastrpc2(&remotectl_open_def, fd, REMOTECTL_HANDLE,
		       strlen(name) + 1, name,
		       &handle, &dlret,
		       256, err);

	if (ret == -1) {
		fprintf(stderr, "Could not remotectl_open(%s): %s\n",
			name, strerror(errno));
		return ret;
	}

	if (dlret) {
		if (dlret == 5)
			fprintf(stderr, "Interface not found: %s\n", name);
		else
			fprintf(stderr, "remotectl_open(%s) error: %s\n",
				name, err);
		return dlret;
	}

	*ctx = fastrpc_create_context(fd, handle);
	return 0;
}

int remotectl_close(struct fastrpc_context *ctx)
{
	uint32_t dlret;
	char err[256];
	int ret;

	ret = fastrpc2(&remotectl_close_def, ctx->fd, REMOTECTL_HANDLE,
		       ctx->handle, &dlret, 256, err);

	if (ret == -1) {
		fprintf(stderr, "Could not remotectl_close: %s\n",
			strerror(errno));
		return ret;
	}

	fastrpc_destroy_context(ctx);
	return dlret;
}
