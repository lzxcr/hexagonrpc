/*
 * HexagonFS virtual filesystem builder — builds tree from config mappings
 *
 * path_mappings in hexagonrpc.json define how DSP virtual paths map to
 * physical filesystem paths under the -R root directory.
 *
 * Example hexagonrpc.json:
 *   { "path_mappings": [
 *       {"virtual": "/vendor/etc", "physical": "/etc/"},
 *       {"virtual": "/persist",    "physical": "/persist/"},
 *       {"virtual": "/sys/devices/soc0", "physical": "/socinfo/"}
 *   ]}
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "hexagonfs.h"
#include "config.h"

static struct hexagonfs_dirent *hfs_leaf(const char *name, const char *phys)
{
	struct hexagonfs_dirent *f = malloc(sizeof(*f));
	if (!f) return NULL;
	f->name = strdup(name);
	if (!f->name) {
		free(f);
		return NULL;
	}
	f->ops = &hexagonfs_mapped_ops;
	f->u.phys = phys;
	return f;
}

/* Find a child by name, or NULL */
static struct hexagonfs_dirent *find_child(struct hexagonfs_dirent *dir,
					   const char *name)
{
	if (dir->ops != &hexagonfs_virt_dir_ops)
		return NULL;
	for (struct hexagonfs_dirent **c = dir->u.dir; c && *c; c++)
		if (!strcmp((*c)->name, name))
			return *c;
	return NULL;
}

/* Append a new virt_dir child */
static struct hexagonfs_dirent *add_dir(struct hexagonfs_dirent *dir,
					const char *name)
{
	int n = 0;
	struct hexagonfs_dirent *child;

	/* Find existing child first */
	child = find_child(dir, name);
	if (child) return child;

	/* Count existing children */
	for (struct hexagonfs_dirent **c = dir->u.dir; c && *c; c++)
		n++;

	/* Realloc to add one more */
	struct hexagonfs_dirent **newlist = realloc(dir->u.dir,
		(n + 2) * sizeof(*newlist));
	if (!newlist) return NULL;
	dir->u.dir = newlist;

	

	struct hexagonfs_dirent *node = calloc(1, sizeof(*node));
	if (!node) return NULL;

	node->name = strdup(name);
	if (!node->name) {
		free(node);
		return NULL;
	}
	node->ops = &hexagonfs_virt_dir_ops;
	node->u.dir = calloc(1, sizeof(void*));
	if (!node->u.dir) {
		free(node->name);
		free(node);
		return NULL;
	}
	newlist[n] = node;
	newlist[n+1] = NULL;

	return newlist[n];
}

/*
 * Build tree from config: walk each mapping, create virt_dir chain,
 * attach mapped leaf at the end.
 */
static struct hexagonfs_dirent *build_root(const char *prefix,
					   const struct hexagonrpc_config *cfg)
{
	struct hexagonfs_dirent *root;
	char *name_copy;
	const char *root_path = prefix ? prefix : "";

	root = calloc(1, sizeof(*root));
	if (!root) return NULL;
	root->name = strdup("/");
	root->ops = &hexagonfs_virt_dir_ops;
	root->u.dir = calloc(1, sizeof(void*));

	if (!cfg || !cfg->mappings)
		return root;

	for (size_t i = 0; i < cfg->n_mappings; i++) {
		const char *virt = cfg->mappings[i].virtual_path;
		const char *phys = cfg->mappings[i].physical_path;
		if (!virt || !phys) continue;

		/* Skip leading / */
		while (*virt == '/') virt++;

		name_copy = strdup(virt);
		if (!name_copy) continue;

		/* Tokenize and walk/create path */
		struct hexagonfs_dirent *cur = root;
		char *seg = strtok(name_copy, "/"), *phys_copy = NULL;
		while (seg) {
			char *next = strtok(NULL, "/");
			if (next) {
				/* Intermediate segment: ensure virt_dir exists */
				struct hexagonfs_dirent *d = add_dir(cur, seg);
				if (!d) break;
				cur = d;
			} else {
				/* Leaf segment: attach mapped entry */
				phys_copy = malloc(strlen(root_path) +
						   strlen(phys) + 2);
				if (!phys_copy) break;
				if (phys[0] == '/') {
					strcpy(phys_copy, phys);
				} else {
					size_t rlen = strlen(root_path);
					memcpy(phys_copy, root_path, rlen);
					if (rlen > 0 && phys_copy[rlen - 1] != '/')
						phys_copy[rlen++] = '/';
					strcpy(phys_copy + rlen, phys);
				}

				/* Check if this name already exists as a child */
				struct hexagonfs_dirent *existing = find_child(cur, seg);
				if (!existing) {
					/* Need to append a mapped leaf */
					int n = 0;
					for (struct hexagonfs_dirent **c = cur->u.dir; c && *c; c++)
						n++;
					struct hexagonfs_dirent **nl = realloc(cur->u.dir,
						(n + 2) * sizeof(*nl));
					if (!nl) { free(phys_copy); break; }
					cur->u.dir = nl;
					nl[n] = hfs_leaf(seg, phys_copy);
					if (!nl[n]) { free(phys_copy); nl[n] = NULL; break; }
					nl[n+1] = NULL;
				} else {
					/* Already exists — replace or leave */
					free(phys_copy);
				}
			}
			seg = next;
		}
		free(name_copy);
	}
	return root;
}

struct hexagonfs_dirent *construct_root_dir(const char *prefix,
					    const char *dsp,
					    const struct hexagonrpc_config *cfg)
{
	(void)dsp;
	return build_root(prefix, cfg);
}

struct hexagonfs_dirent *construct_root_dir_with_prefix(const char *prefix,
							const char *dsp,
							const struct hexagonrpc_config *cfg)
{
	struct hexagonfs_dirent *root = construct_root_dir(prefix, dsp, cfg);
	struct virt_dir_dirent_data *root_data;

	if (!root) return NULL;

	root_data = malloc(sizeof(*root_data));
	if (!root_data) return root;

	root_data->root_path = strdup(prefix ? prefix : "");
	root_data->dirlist = (const struct hexagonfs_dirent *const *)root->u.dir;
	root->u.ptr = root_data;
	return root;
}
