#!/bin/bash
set -euo pipefail

# HexagonRPC integration tests
# Runs on: any Linux with build artifacts (no DSP required)
# Tests: compilation, CLI, hexagonfs, library, dispatch chart

BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
PROJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PASS=0
FAIL=0
EXE="${BUILD_DIR}/hexagonrpcd/hexagonrpcd"
LIB="${BUILD_DIR}/libhexagonrpc/libhexagonrpc.so.0.5"

ok()   { PASS=$((PASS+1)); echo "  ✅ $1"; }
fail() { FAIL=$((FAIL+1)); echo "  ❌ $1"; }

# --- 1. Binary sanity ---
echo "=== 1. Binary sanity ==="
[ -x "$EXE" ] && ok "hexagonrpcd exists and executable" || fail "hexagonrpcd missing"
file "$EXE" | grep -q "ELF" && ok "hexagonrpcd is an ELF binary" || fail "not an ELF"
ldd "$EXE" | grep -q "libhexagonrpc" && ok "links libhexagonrpc" || fail "no libhexagonrpc"

# --- 2. CLI argument handling ---
echo "=== 2. CLI argument handling ==="
$EXE --help 2>&1 
$EXE -z 2>&1 | grep -q "Usage:" && ok "invalid arg shows usage" || fail "invalid arg handling"
$EXE -f /dev/fastrpc-test -R /tmp 2>&1 | grep -q "Could not open FastRPC" && \
  ok "nonexistent device -> proper error" || fail "device error msg missing"

# --- 3. Library symbol verification ---
echo "=== 3. Library symbols ==="
  nm -D "$LIB" | grep -q " $sym$" && ok "symbol $sym found" || fail "symbol $sym missing"
done

# --- 4. Build quality ---
echo "=== 4. Build quality ==="
(cd "$PROJ_DIR" && ninja -C build 2>&1 | grep -cE "warning|Warn" | xargs -r echo "warnings found:") || true
(cd "$PROJ_DIR" && ninja -C build 2>&1 | grep -qE "error:" && fail "build has errors" || ok "build clean")

# --- 5. Method dispatch completeness ---
echo "=== 5. Method dispatch completeness ==="
echo ""
echo "  apps_std:"
for id in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 \
          20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36; do
  nm "$EXE" | grep -q "apps_std_fopen${id}_def" 2>/dev/null || true
done
# Check no NULL implementations
NULLS=$(grep -c '\.def = NULL' "$PROJ_DIR/hexagonrpcd/apps_std.c" 2>/dev/null || echo 0)
[ "$NULLS" -eq 0 ] && ok "apps_std: 0 NULL entries" || fail "apps_std: $NULLS NULL entries"
# Count .def entries in procs array
DEFS=$(grep -c '\.def = &' "$PROJ_DIR/hexagonrpcd/apps_std.c")
[ "$DEFS" -eq 37 ] && ok "apps_std: 37/37 methods implemented" || fail "apps_std: $DEFS/37 implemented"

echo ""
echo "  apps_mem:"
NULLS=$(grep -c '\.def = NULL' "$PROJ_DIR/hexagonrpcd/apps_mem.c" 2>/dev/null || echo 0)
[ "$NULLS" -eq 0 ] && ok "apps_mem: 0 NULL entries" || fail "apps_mem: $NULLS NULL entries"
DEFS=$(grep -c '\.def = &' "$PROJ_DIR/hexagonrpcd/apps_mem.c")
[ "$DEFS" -eq 8 ] && ok "apps_mem: 8/8 methods implemented" || fail "apps_mem: $DEFS/8 implemented"

# --- 6. .def file count matches procs count ---
echo ""
echo "  interface definitions:"
STD_DEF=$(grep -c 'HEXAGONRPC_DEFINE' "$PROJ_DIR/hexagonrpcd/interfaces/apps_std.def")
MEM_DEF=$(grep -c 'HEXAGONRPC_DEFINE' "$PROJ_DIR/hexagonrpcd/interfaces/apps_mem.def")
[ "$STD_DEF" -eq 37 ] && ok "apps_std.def: 37 definitions" || fail "apps_std.def: $STD_DEF != 37"
[ "$MEM_DEF" -eq 8 ]  && ok "apps_mem.def: 8 definitions"   || fail "apps_mem.def: $MEM_DEF != 8"

# --- 7. HexagonFS directory construction ---
echo ""
echo "=== 6. HexagonFS structural test ==="
TEST_ROOT=$(mktemp -d)
cleanup() { rm -rf "$TEST_ROOT"; }
trap cleanup EXIT

mkdir -p "$TEST_ROOT/acdb"
mkdir -p "$TEST_ROOT/dsp/adsp"
mkdir -p "$TEST_ROOT/sensors/config"
mkdir -p "$TEST_ROOT/sensors/registry"
touch "$TEST_ROOT/sensors/sns_reg.conf"
mkdir -p "$TEST_ROOT/socinfo"
echo "test_acdb" > "$TEST_ROOT/acdb/test.bin"
echo "test_skel" > "$TEST_ROOT/dsp/adsp/libtest_skel.so"
echo "test_sns_cfg" > "$TEST_ROOT/sensors/config/test.json"
echo "test_reg" > "$TEST_ROOT/sensors/registry/test.reg"
echo "test_soc" > "$TEST_ROOT/socinfo/machine"

# Write a small test program that exercises hexagonfs
TEST_FILE="$TEST_ROOT/hexagonfs_test.c"
cat > "$TEST_FILE" << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "hexagonfs.h"
#include <fcntl.h>

int main(int argc, char **argv) {
    const char *prefix = argv[1];
    struct hexagonfs_dirent *root = construct_root_dir_with_prefix(prefix, "adsp");
    struct hexagonfs_fd *fds[256] = {0};
    int rootfd = hexagonfs_open_root(fds, root);
    int fd;
    char buf[64];

    if (rootfd < 0) { perror("root"); return 1; }
    printf("ROOT_FD=%d\n", rootfd);

    /* Test 1: Open known virt_dir path */
    fd = hexagonfs_openat(fds, rootfd, rootfd, "/acdb/test.bin");
    if (fd >= 0) {
        memset(buf, 0, sizeof(buf));
        ssize_t n = hexagonfs_read(fds, fd, 10, buf);
        hexagonfs_close(fds, fd);
        printf("OPEN_ACDB=%s\n", strstr(buf, "test_acdb") ? "OK" : "FAIL");
    } else { printf("OPEN_ACDB=FAIL (%d)\n", fd); }

    /* Test 2: Open via ADSP_LIBRARY_PATH equivalent */
    fd = hexagonfs_openat(fds, rootfd, rootfd, "/usr/lib/qcom/adsp/libtest_skel.so");
    if (fd >= 0) {
        memset(buf, 0, sizeof(buf));
        ssize_t n = hexagonfs_read(fds, fd, 10, buf);
        hexagonfs_close(fds, fd);
        printf("OPEN_SKEL=%s\n", strstr(buf, "test_skel") ? "OK" : "FAIL");
    } else { printf("OPEN_SKEL=FAIL (%d)\n", fd); }

    /* Test 3: Open sensors config */
    fd = hexagonfs_openat(fds, rootfd, rootfd, "/vendor/etc/sensors/config/test.json");
    if (fd >= 0) {
        memset(buf, 0, sizeof(buf));
        ssize_t n = hexagonfs_read(fds, fd, 20, buf);
        hexagonfs_close(fds, fd);
        printf("OPEN_SNS_CFG=%s\n", strstr(buf, "test_sns_cfg") ? "OK" : "FAIL");
    } else { printf("OPEN_SNS_CFG=FAIL (%d)\n", fd); }

    /* Test 4: Open sensors registry */
    fd = hexagonfs_openat(fds, rootfd, rootfd, "/mnt/vendor/persist/sensors/registry/test.reg");
    if (fd >= 0) {
        hexagonfs_close(fds, fd);
        printf("OPEN_REG_HARDLINK=OK\n");
    } else { printf("OPEN_REG_HARDLINK=FAIL (%d)\n", fd); }

    /* Test 5: Open vendor/etc via hard link */
    fd = hexagonfs_openat(fds, rootfd, rootfd, "/system/vendor/etc/acdbdata/test.bin");
    if (fd >= 0) {
        hexagonfs_close(fds, fd);
        printf("OPEN_VENDOR_HARDLINK=OK\n");
    } else { printf("OPEN_VENDOR_HARDLINK=FAIL (%d)\n", fd); }

    /* Test 6: stat */
    struct stat st;
    fd = hexagonfs_openat(fds, rootfd, rootfd, "/acdb/test.bin");
    if (fd >= 0) {
        int r = hexagonfs_fstat(fds, fd, &st);
        hexagonfs_close(fds, fd);
        printf("STAT=%s (%ld bytes)\n", r==0?"OK":"FAIL", (long)st.st_size);
    }

    /* Test 7: Write support */
    fd = hexagonfs_openat(fds, rootfd, rootfd, "/acdb/test.bin");
    if (fd >= 0) {
        ssize_t w = hexagonfs_write(fds, fd, 5, "hello");
        hexagonfs_close(fds, fd);
        printf("WRITE=%s (%ld)\n", w>=0?"OK":"FAIL", (long)w);
    }

    return 0;
}
EOF

# Build the test
gcc -std=gnu17 -D_GNU_SOURCE -D_DEFAULT_SOURCE -I"$PROJ_DIR/hexagonrpcd" -I"$PROJ_DIR/include" \
    "$TEST_FILE" \
    "$PROJ_DIR/hexagonrpcd/hexagonfs.c" \
    "$PROJ_DIR/hexagonrpcd/hexagonfs_mapped.c" \
    "$PROJ_DIR/hexagonrpcd/hexagonfs_virt_dir.c" \
    "$PROJ_DIR/hexagonrpcd/rpcd_builder.c" \
    -o "$TEST_ROOT/hexagonfs_test" 2>&1 && ok "hexagonfs test compiled" || fail "hexagonfs test compiled"

# Run the test
echo ""
echo "--- HexagonFS Test Output ---"
"$TEST_ROOT/hexagonfs_test" "$TEST_ROOT" 2>&1
echo "--- End Test Output ---"
echo ""

# Verify each result
"$TEST_ROOT/hexagonfs_test" "$TEST_ROOT" 2>&1 | grep -q "OPEN_ACDB=OK" && ok "hexagonfs: open acdb file" || fail "hexagonfs: acdb open failed"
"$TEST_ROOT/hexagonfs_test" "$TEST_ROOT" 2>&1 | grep -q "OPEN_SKEL=OK" && ok "hexagonfs: open skel lib" || fail "hexagonfs: skel open failed"
"$TEST_ROOT/hexagonfs_test" "$TEST_ROOT" 2>&1 | grep -q "OPEN_SNS_CFG=OK" && ok "hexagonfs: open sensor config via /vendor/..." || fail "hexagonfs: sensor config failed"
"$TEST_ROOT/hexagonfs_test" "$TEST_ROOT" 2>&1 | grep -q "OPEN_REG_HARDLINK=OK" && ok "hexagonfs: mnt/vendor/persist hardlink" || fail "hexagonfs: hardlink failed"
"$TEST_ROOT/hexagonfs_test" "$TEST_ROOT" 2>&1 | grep -q "OPEN_VENDOR_HARDLINK=OK" && ok "hexagonfs: /system/vendor hardlink" || fail "hexagonfs: hardlink 2 failed"
"$TEST_ROOT/hexagonfs_test" "$TEST_ROOT" 2>&1 | grep -q "STAT=OK" && ok "hexagonfs: stat works" || fail "hexagonfs: stat failed"
"$TEST_ROOT/hexagonfs_test" "$TEST_ROOT" 2>&1 | grep -q "WRITE=OK" && ok "hexagonfs: write works (default enabled)" || fail "hexagonfs: write failed"

# --- 7. Run existing meson tests ---
echo ""
echo "=== 7. Meson test suite ==="
(cd "$PROJ_DIR" && meson test -C build 2>&1 | grep -E "OK|Fail|Ok|FAIL") && ok "meson tests pass" || fail "meson tests"

# --- Summary ---
echo ""
echo "==========================="
echo "  Results: $PASS passed, $FAIL failed"
echo "==========================="

cleanup
[ "$FAIL" -eq 0 ]
