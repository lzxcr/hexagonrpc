/* config.c — JSON config parser for hexagonrpc */
#include "config.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct hexagonrpc_config *hexagonrpc_config_load(const char *root_path) {
	struct hexagonrpc_config *cfg;
	struct json_object *root, *obj;
	char *path;
	int path_len;

	cfg = calloc(1, sizeof(*cfg));
	if (!cfg) return NULL;

	cfg->root_path = root_path ? strdup(root_path) : NULL;

	/* Try device-local config first, then fall back to global */
	path_len = snprintf(NULL, 0, "%s/hexagonrpc.json", root_path ? root_path : "/usr/share/qcom");
	path = malloc(path_len + 1);
	if (!path) goto out;
	sprintf(path, "%s/hexagonrpc.json", root_path ? root_path : "/usr/share/qcom");

	root = json_object_from_file(path);
	if (!root) {
		/* Fallback: try global conf.d */
		free(path);
		path = strdup("/usr/share/qcom/conf.d/hexagonrpc.json");
		if (!path) goto out;
		root = json_object_from_file(path);
	}
	free(path);
	if (!root) goto out;

	if (json_object_object_get_ex(root, "path_mappings", &obj)) {
		int n = json_object_array_length(obj);
		if (n > 0) {
			cfg->mappings = calloc(n, sizeof(*cfg->mappings));
			cfg->n_mappings = n;
			for (int i = 0; i < n; i++) {
				struct json_object *entry = json_object_array_get_idx(obj, i);
				struct json_object *v, *p;
				if (json_object_object_get_ex(entry, "virtual", &v) && v)
					cfg->mappings[i].virtual_path = strdup(json_object_get_string(v));
				if (json_object_object_get_ex(entry, "physical", &p) && p)
					cfg->mappings[i].physical_path = strdup(json_object_get_string(p));
			}
		}
	}
	json_object_put(root);

out:
	return cfg;
}

void hexagonrpc_config_free(struct hexagonrpc_config *cfg) {
	if (!cfg) return;
	for (size_t i = 0; i < cfg->n_mappings; i++) {
		free((void*)cfg->mappings[i].virtual_path);
		free((void*)cfg->mappings[i].physical_path);
	}
	free(cfg->mappings);
	free(cfg->root_path);
	free(cfg);
}
