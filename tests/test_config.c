#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../hexagonrpcd/hexagonfs.h"
#include "../hexagonrpcd/rpcd_builder.h"
#include "../hexagonrpcd/config.h"

static void print_phys(const char *s) {
    size_t n = strlen(s);
    while (n > 0 && s[n-1] == '/') n--;
    printf("%.*s", (int)n, s);
}
static void print_path(const char *root, const char *phys) {
    if (phys[0] == '/')
        printf("  \xe2\xae\x91  resolves to: %s (absolute)\n", phys);
    else {
        printf("  \xe2\xae\x91  resolves to: %s/", root);
        if (root[strlen(root)-1] == '/')
            print_phys(phys);
        else
            printf("%s\n", phys);
    }
}

int main(int argc, char **argv) {
    struct hexagonrpc_config _def = {0}, *cfg = &_def;
    char cmd[512], tf[256], root_buf[512];
    int is_tmp = 0;

    if (argc > 1) {
        strcpy(root_buf, argv[1]);
        cfg = hexagonrpc_config_load(root_buf);
        if (!cfg || cfg->n_mappings == 0) {
            printf("No config at /usr/share/qcom/conf.d/hexagonrpc.json\n");
            return 1;
        }
        printf("=== Config mode (root: %s) ===\n\n", root_buf);
    } else {
        is_tmp = 1;
        strcpy(root_buf, "/tmp/hex-config-test");
        _def.n_mappings = 5;
        _def.mappings = calloc(5, sizeof(*_def.mappings));
        _def.mappings[0].virtual_path = strdup("/vendor/etc");
        _def.mappings[0].physical_path = strdup("vendor/etc");
        _def.mappings[1].virtual_path = strdup("/persist");
        _def.mappings[1].physical_path = strdup("persist");
        _def.mappings[2].virtual_path = strdup("/usr/lib/qcom/adsp");
        _def.mappings[2].physical_path = strdup("vendor/etc");
        _def.mappings[3].virtual_path = strdup("/odm/etc");
        _def.mappings[3].physical_path = strdup("odm/etc");
        _def.mappings[4].virtual_path = strdup("/sys/devices/soc0");
        _def.mappings[4].physical_path = strdup("socinfo");
        printf("=== Self-test (tempdir) ===\n\n");
        snprintf(cmd, sizeof(cmd),
                 "rm -rf %s && mkdir -p %s/vendor/etc %s/persist %s/socinfo %s/odm/etc",
                 root_buf, root_buf, root_buf, root_buf, root_buf);
        system(cmd);
        snprintf(cmd, sizeof(cmd),
                 "touch %s/vendor/etc/__hx__ %s/persist/__hx__ "
                 "%s/socinfo/__hx__ %s/odm/etc/__hx__",
                 root_buf, root_buf, root_buf, root_buf);
        system(cmd);
    }

    printf("Mappings:\n");
    for (size_t i = 0; i < cfg->n_mappings; i++) {
        printf("  %s\n", cfg->mappings[i].virtual_path);
        print_path(root_buf, cfg->mappings[i].physical_path);
    }

    struct hexagonfs_fd *fds[256] = {0};
    struct hexagonfs_dirent *rt = construct_root_dir_with_prefix(root_buf, "adsp", cfg);
    int rf = hexagonfs_open_root(fds, rt);
    if (rf < 0) { fprintf(stderr, "FATAL\n"); return 1; }

    printf("\nResolution:\n");
    int failed = 0;
    for (size_t i = 0; i < cfg->n_mappings; i++) {
        snprintf(tf, sizeof(tf), "%s/__hx__", cfg->mappings[i].virtual_path);
        errno = 0;
        int fd = hexagonfs_openat(fds, rf, rf, tf);
        if (fd >= 0) {
            printf("  %-30s -> OK\n", tf);
            hexagonfs_close(fds, fd);
        } else {
            printf("  %-30s -> ERR  [%s]\n", tf, strerror(-fd));
            failed++;
        }
    }
    errno = 0;
    int fd = hexagonfs_openat(fds, rf, rf, "/__hx_nonexist__");
    printf("  %-30s -> %s  [%s]\n", "/__hx_nonexist__",
           fd < 0 ? "OK (expected)" : "FAIL",
           fd < 0 ? strerror(-fd) : "unexpectedly");
    if (fd >= 0) hexagonfs_close(fds, fd);
    printf("\n%d/%zu failed\n", failed, cfg->n_mappings);
    if (!is_tmp) { hexagonrpc_config_free(cfg); }
    return 0;
}
