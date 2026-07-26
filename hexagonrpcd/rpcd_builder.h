#ifndef RPCD_BUILDER_H
#define RPCD_BUILDER_H

#include "hexagonfs.h"
#include "config.h"

struct hexagonfs_dirent *construct_root_dir(const char *prefix,
					    const char *dsp,
					    const struct hexagonrpc_config *cfg);

struct hexagonfs_dirent *construct_root_dir_with_prefix(const char *prefix,
							const char *dsp,
							const struct hexagonrpc_config *cfg);

#endif
