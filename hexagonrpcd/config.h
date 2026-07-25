/* config.h — FastRPC-style config parser (JSON-based) */
#ifndef HEXAGONRPC_CONFIG_H
#define HEXAGONRPC_CONFIG_H
#include <stddef.h>
#include <stdbool.h>

/* A single path mapping: virtual_path -> physical_path */
struct hexagonrpc_path_mapping {
	const char *virtual_path;
	const char *physical_path;
};

/* Parsed config from <root>/hexagonrpc.json */
struct hexagonrpc_config {
	char *root_path;
	struct hexagonrpc_path_mapping *mappings;
	size_t n_mappings;
};

struct hexagonrpc_config *hexagonrpc_config_load(const char *root_path);
void hexagonrpc_config_free(struct hexagonrpc_config *cfg);

#endif
