#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../hexagonrpcd/hexagonfs.h"
#include "../hexagonrpcd/rpcd_builder.h"
#include "../hexagonrpcd/config.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <root-dir>\n", argv[0]); return 1; }
    const char *root = argv[1];

    struct hexagonrpc_config cfg = {0};
    cfg.n_mappings = 5;
    cfg.mappings = calloc(5, sizeof(*cfg.mappings));
    cfg.mappings[0].virtual_path = strdup("/odm/etc");
    cfg.mappings[0].physical_path = strdup("odm/etc");
    cfg.mappings[1].virtual_path = strdup("/vendor/etc");
    cfg.mappings[1].physical_path = strdup("vendor/etc");
    cfg.mappings[2].virtual_path = strdup("/vendor/dsp");
    cfg.mappings[2].physical_path = strdup("dsp");
    cfg.mappings[3].virtual_path = strdup("/usr/lib/qcom/adsp");
    cfg.mappings[3].physical_path = strdup("vendor/etc");
    cfg.mappings[4].virtual_path = strdup("/persist");
    cfg.mappings[4].physical_path = strdup("persist");

    printf("=== Test tree with root=%s ===\n\n", root);
    struct hexagonfs_fd *fds[256] = {0};
    struct hexagonfs_dirent *rt = construct_root_dir_with_prefix(root, "adsp", &cfg);
    int rf = hexagonfs_open_root(fds, rt);
    printf("rootfd=%d\n\n", rf);

    const char *paths[] = {
        "/vendor/etc",
        "/vendor/etc/sensors/sns_reg_config",
        "/usr/lib/qcom/adsp/",
        "/persist",
        NULL
    };

    for (const char **p = paths; *p; p++) {
        printf("hexagonfs_openat(..., \"%s\") = ", *p);
        int fd = hexagonfs_openat(fds, rf, rf, *p);
        if (fd >= 0) {
            printf("OK (fd=%d)\n", fd);
            hexagonfs_close(fds, fd);
        } else {
            printf("ERR %d (%s)\n", -fd, strerror(-fd));
        }
    }
    return 0;
}
