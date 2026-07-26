## Why

`hexagonfs.c` 的架构设计合理(VFS 模式),但存在以下问题:

1. **真实 Bug**:`rootfd` 未校验 (`pop_dir(fd, fds[rootfd])` 可能越界);`HEXAGONFS_MAX_FD` 未替代魔法数字 256;`selected >= -256` 依赖 Linux 内部 errno 范围
2. **防御不足**:`root->ops` / `ops->close` 等指针无 NULL 检查;`copy_segment_and_advance` 对 `"/"` 路径行为不确定
3. **重复代码**:FD 校验 (`fileno < 0 || fileno >= MAX_FD` + `fds[fileno] == NULL` + `ops->op == NULL`) 在 10+ 个函数中重复
4. **性能**:路径解析每一段都 `malloc()` + `free()`,属于热路径
5. **风格不一致**:命名混合 (`allocate_file_number` / `pop_dir` / `destroy_file_descriptor`)

## What Changes

### 1. 修复 bug
- `rootfd` 在函数入口统一校验,与 `dirfd` 同级
- 所有 `256` 魔法数字替换为 `HEXAGONFS_MAX_FD`
- `selected >= -256` 简化为 `selected < 0`,传播任意 errno

### 2. 提取统一 helpers
- `hexagonfs_get_fd(fds, fileno)` — 集中 FD 查找+校验,消除所有重复
- `hexagonfs_release_fd(fd)` — 统一 close+free,覆盖 `destroy_file_descriptor` / `pop_dir`

### 3. 零分配路径解析
- `copy_segment_and_advance` 改为 `hexagonfs_path_next`,返回 `(const char *start, size_t len)` 而非 malloc 的拷贝
- 移除 `goto next` / `free(segment)` 模式
- 直接用 `memcmp` / `openat(seg_start, seg_len, ...)` 而不需要 NUL 终止

### 4. 统一命名
- `allocate_file_number` → `hexagonfs_assign_fd`
- `destroy_file_descriptor` → `hexagonfs_destroy_chain`
- 其他内部函数统一 `hexagonfs_` 前缀

### 5. 防御性检查
- `hexagonfs_open_root`:检查 `fds` / `root` / `root->ops` / `root->ops->from_dirent`
- `hexagonfs_release_fd`:检查 `ops->close` 后再调用

### 6. 清理
- 所有可 const 化的指针常量化
- 统一 `static inline` helper 风格