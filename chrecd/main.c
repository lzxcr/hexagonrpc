/*
 * CHRE client daemon entry point
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

#include <libhexagonrpc/fastrpc.h>
#include <libhexagonrpc/remotectl.h>
#include <libhexagonrpc/session.h>
#include <stdio.h>

#include "interfaces/chre_slpi.def"

static int chre_slpi_start_thread(struct fastrpc_context *ctx)
{
	return fastrpc(&chre_slpi_start_thread_def, ctx);
}

static int chre_slpi_wait_on_thread_exit(struct fastrpc_context *ctx)
{
	return fastrpc(&chre_slpi_wait_on_thread_exit_def, ctx);
}

int main(void)
{
	struct fastrpc_context *ctx;
	int fd, ret;

	fd = hexagonrpc_fd_from_env();
	if (fd == -1)
		return 1;

	ret = remotectl_open(fd, "chre_slpi", &ctx);
	if (ret)
		return 1;

	ret = chre_slpi_start_thread(ctx);
	if (ret) {
		fprintf(stderr, "Could not start CHRE\n");
		goto err;
	}

	ret = chre_slpi_wait_on_thread_exit(ctx);
	if (ret)
		fprintf(stderr, "Could not wait for CHRE thread\n");

err:
	remotectl_close(ctx);
	return !!ret;
}
