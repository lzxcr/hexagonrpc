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

static void print_path(const char *root, const char *phys)
{
    if (phys[0] == '/')
        printf("  ⮑  resolves to: %s (absolute path on filesystem)\n", phys);
    else {
        printf("  ⮑  resolves to: %s", root);
        if (root[strlen(root)-1] != '/') printf("/");
        printf("%s\n", phys);
    }
}

int main(int argc, char **argv) {
    struct hexagonrpc_config _def = {0}, *cfg = NULL;
    char cmd[512], tf[256], root_buf[512];

    if (argc == 1) {
        const char *rt = "/tmp/hex-config-test";
        printf("=== Self-test (tempdir) ===\n\n");
        _def.n_mappings = 3;
        _def.mappings = calloc(3, sizeof(*_def.mappings));
        _def.mappings[0].virtual_path = strdup("/vendor/etc");
        _def.mappings[0].physical_path = strdup("etc/");
        _def.mappings[1].virtual_path = strdup("/persist");
        _def.mappings[1].physical_path = strdup("persist/");
        _def.mappings[2].virtual_path = strdup("/sys/devices/soc0");
        _def.mappings[2].physical_path = strdup("socinfo/");
        cfg = &_def;
        strcpy(root_buf, rt);
        snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s/etc %s/persist %s/socinfo",
                 rt, rt, rt, rt);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "touch %s/etc/__hx__ %s/persist/__hx__ %s/socinfo/__hx__",
                 rt, rt, rt);
        system(cmd);
    } else {
        strcpy(root_buf, argv[1]);
        cfg = hexagonrpc_config_load(root_buf);
        if (!cfg || cfg->n_mappings == 0) {
            printf("=== %zu mappings loaded ===\n", cfg ? cfg->n_mappings : 0);
            printf("No config at /usr/share/qcom/conf.d/hexagonrpc.json\n");
            return 1;
        }
        printf("=== Config mode (root: %s) ===\n\n", root_buf);
    }

    printf("Mappings:\n");
    for (size_t i = 0; i < cfg->n_mappings; i++) {
        printf("  %s\n", cfg->mappings[i].virtual_path);
        print_path(root_buf, cfg->mappings[i].physical_path);
        /* Validate: absolute paths on non-tmp roots are likely wrong */
        if (cfg->mappings[i].physical_path[0] == '/'
            && strncmp(root_buf, "/tmp/", 5) != 0)
            printf("  ⚠  WARNING: physical path starts with '/' — this is an absolute\n"
                   "           path on the real filesystem, NOT relative to -R root.\n"
                   "           Change '%s' to something without leading '/'\n"
                   "           to make it relative to the -R root directory.\n",
                   cfg->mappings[i].physical_path);
    }

    printf("\nResolution:\n");
    struct hexagonfs_fd *fds[256] = {0};
    struct hexagonfs_dirent *rt = construct_root_dir_with_prefix(root_buf, "adsp", cfg);
    int rf = hexagonfs_open_root(fds, rt);
    if (rf < 0) { fprintf(stderr, "FATAL\n"); return 1; }

    int failed = 0;
    for (size_t i = 0; i < cfg->n_mappings; i++) {
        snprintf(tf, sizeof(tf), "%s/__hx__",
                 cfg->mappings[i].virtual_path);
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
    {
        errno = 0;
        int fd = hexagonfs_openat(fds, rf, rf, "/__hx_nonexist__");
        printf("  %-30s -> %s  [%s]\n", "/__hx_nonexist__",
               fd < 0 ? "OK (expected)" : "FAIL",
               fd < 0 ? strerror(-fd) : "unexpectedly opened");
        if (fd >= 0) hexagonfs_close(fds, fd);
    }
    printf("\n%d/%zu failed\n", failed, cfg->n_mappings);

    if (cfg == &_def) {
        snprintf(cmd, sizeof(cmd), "rm -rf %s", root_buf); system(cmd);
    } else {
        hexagonrpc_config_free(cfg);
    }
    return 0;
}
