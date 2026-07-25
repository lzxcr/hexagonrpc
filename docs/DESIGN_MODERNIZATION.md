# HexagonRPC 现代化升级 — 设计文档

> 版本: 1.0
> 状态: 草案

---

## 1. 设计目标

1. **功能完备** — 实现 FastRPC 的全部 37 个 apps_std 方法和 7 个 apps_mem 方法
2. **路径重定向** — HexagonFS 保持为唯一的文件访问层，DSP 的 Android 路径 → 主线 Linux 路径
3. **主线 Linux 原生** — 零 Qualcomm 闭源依赖、零 Android 特有路径假设
4. **可扩展** — 新接口（如未来添加 domain 支持、性能跟踪）可以模块化添加

---

## 2. 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│  hexagonrpcd (main.c loop)                                       │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │  Reverse Tunnel (listener.c)                                ││
│  │  接收 DSP 请求 → decode → dispatch → impl → encode → 返回  ││
│  └──────────────┬──────────────────────────────────────────────┘│
│                 │  handle 查找                                   │
│                 ▼                                               │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Interface Dispatch (listener.c:invoke_requested_procedure) │
│  │  按 handle + method ID 查找 impl 函数并调用               │   │
│  └─────┬──────────┬──────────┬──────────┬──────────────────┘   │
│        │          │          │          │                       │
│        ▼          ▼          ▼          ▼                       │
│  ┌────────┐ ┌─────────┐ ┌──────────┐ ┌───────────┐             │
│  │remotectl│ │apps_std │ │apps_mem  │ │ (future)  │             │
│  │(已实现) │ │(补全37) │ │(补全7)  │ │ domain/   │             │
│  │        │ │         │ │          │ │ perf/...  │             │
│  └────────┘ └────┬────┘ └────┬─────┘ └───────────┘             │
│                  │            │                                 │
│                  ▼            ▼                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              HexagonFS (hexagonfs.c + 子文件)             │   │
│  │  虚拟目录树 + 路径映射 → Linux 原生文件操作               │   │
│  │  整个 apps_std 的 IO 都走 HexagonFS（只读 + 条件写）     │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. 模块划分

### 3.1 现有模块（不动或最小修改）

| 模块 | 文件 | 改动策略 |
|------|------|----------|
| `libhexagonrpc` | `fastrpc.c`, `context.c`, `session.c` | 不动。ioctl 包装层已稳定 |
| `.def` 接口定义宏 | `include/libhexagonrpc/interface.h` | 不动。宏系统良好 |
| `remotectl` (localctl) | `localctl.c` | 不动 |
| Reverse Tunnel | `listener.c`, `iobuffer.c` | 不动 |
| hexagonrpcd main | `rpcd.c` | 不动 |
| Meson 构建 | `meson.build` 系列 | 新增源文件 + 可能新增功能开关 |
| HexagonFS 核心 | `hexagonfs.c`, `hexagonfs.h` | 不动 |
| HexagonFS backends | `hexagonfs_mapped.c`, `hexagonfs_virt_dir.c` 等 | 不动 |

### 3.2 补全模块

#### apps_std — 当前 10 个方法 → 目标 37 个方法

| ID | 方法 | 当前状态 | 备注 |
|----|------|----------|------|
| 0 | `fopen` | ❌ **缺失** | 通过 HexagonFS 路径映射 + `openat` |
| 1 | `freopen` | ❌ 缺失 | fclose + fopen |
| 2 | `fflush` | ✅ 已实现 | 空操作（仅输出 buffer） |
| 3 | `fclose` | ✅ 已实现 | 走 HexagonFS |
| 4 | `fread` | ✅ 已实现 | 走 HexagonFS |
| 5 | `fwrite` | ❌ 缺失 | 仅支持只读文件（HexagonFS 约束） |
| 6 | `fgetpos` | ❌ 缺失 | 走 HexagonFS seek/tell |
| 7 | `fsetpos` | ❌ 缺失 | 同上 |
| 8 | `ftell` | ❌ 缺失 | 走 HexagonFS |
| 9 | `fseek` | ✅ 已实现 | 走 HexagonFS |
| 10 | `flen` | ❌ 缺失 | hexagonfs_fstat 获取 size |
| 11 | `rewind` | ❌ 缺失 | fseek(fd, 0, SEEK_SET) |
| 12 | `feof` | ❌ 缺失 | 维护 EOF 状态或通过 fread 隐式判断 |
| 13 | `ferror` | ❌ 缺失 | 维护 error 状态 |
| 14 | `clearerr` | ❌ 缺失 | 重置 error/eof 状态 |
| 15 | `print_string` | ❌ 缺失 | printf wrapper |
| 16 | `getenv` | ❌ 缺失 | getenv wrapper |
| 17 | `setenv` | ❌ 缺失 | setenv wrapper |
| 18 | `unsetenv` | ❌ 缺失 | unsetenv wrapper |
| 19 | `fopen_with_env` | ✅ 已实现 | 走 HexagonFS |
| 20 | `fgets` | ❌ 缺失 | fread + 行解析 |
| 21 | `get_search_paths_with_env` | ❌ 缺失 | 返回 HexagonFS 搜索路径列表 |
| 22 | `fileExists` | ❌ 缺失 | hexagonfs_fstat 检查 |
| 23 | `fsync` | ❌ 缺失 | 空操作（只读文件系统） |
| 24 | `fremove` | ❌ 缺失 | unlink wrapper（只读 = AEE_EUNSUPPORTED） |
| 25 | `fdopen_decrypt` | ❌ 缺失 | 空操作（无解密需要） |
| 26 | `opendir` | ✅ 已实现 | 走 HexagonFS |
| 27 | `closedir` | ✅ 已实现 | 走 HexagonFS |
| 28 | `readdir` | ✅ 已实现 | 走 HexagonFS |
| 29 | `mkdir` | ❌ 缺失 | 可写模式扩展 |
| 30 | `rmdir` | ❌ 缺失 | 同上 |
| 31 | `stat` | ✅ 已实现 | 走 HexagonFS |
| 32 | `ftrunc` | ❌ 缺失 | 可写模式扩展 |
| 33 | `frename` | ❌ 缺失 | 可写模式扩展 |
| 34 | `fopen_fd` | ❌ **缺失** | 用 DMA-BUF 打开文件并返回 fd + len（关键功能） |
| 35 | `fclose_fd` | ❌ 缺失 | 关闭 fopen_fd 打开的 fd |
| 36 | `fopen_with_env_fd` | ❌ 缺失 | 带搜索路径的 fopen_fd |

**实现策略**：每个新方法在 `apps_std.c` 中新增一个 `static uint32_t apps_std_xxx(...)` 函数，追加到 `apps_std_procs[]` 数组。函数的 IO 操作统一通过 HexagonFS。

#### apps_mem — 当前 1 个方法 → 目标 7 个方法

| ID | 方法 | 当前状态 | 备注 |
|----|------|----------|------|
| 0 | `request_map` | ❌ 缺失 | 32 位版 request_map64 |
| 1 | `request_unmap` | ❌ 缺失 | 同上 |
| 2 | `request_map64` | ✅ 已实现 | 仅支持 ADSP_MMAP_ADD_PAGES |
| 3 | `request_unmap64` | ❌ 缺失 | 释放 map64 分配的内存 |
| 4 | `share_map` | ❌ 缺失 | 通过 fd 共享已有 mmap |
| 5 | `share_unmap` | ❌ 缺失 | 释放 share_map |
| 6 | `dma_handle_map` | ❌ 缺失 | DMA-BUF handle 映射 |
| 7 | `dma_handle_unmap` | ❌ 缺失 | 释放 dma_handle_map |

**实现策略**：`apps_mem.c` 新增实现。`request_map` 基于 `request_map64` 做 32/64 转换。unmap 和 share 系列通过 `FASTRPC_IOCTL_MUNMAP` 实现。dma_handle 系列通过 DMA-BUF 机制。

### 3.3 新增模块

| 模块 | 文件 | 用途 | 优先级 |
|------|------|------|--------|
| **rpcmem 替代** | `new/rpcmem_linux.c` | Linux DMA-BUF 内存分配替代 Qualcomm rpcmem | P1 — apps_mem 需要 |
| **domain 支持** | `new/domain.c` | 多域（ADSP/CDSP/SDSP）上下文管理 | P2 — 未来 |
| **配置解析** | `new/config.c` | YAML/INI 配置 DSP 搜索路径 | P2 — 未来 |
| **性能跟踪** | `new/perf.c` | FASTRPC_ATRACE 风格的性能日志 | P3 — 未来 |
| **FARF 日志** | `new/log.c` | 替代 HAP_farf 的日志系统 | P2 — 与配置解析配合 |

---

## 4. HexagonFS 与文件访问的设计

### 4.1 核心原则

- **所有**文件操作经过 HexagonFS。apps_std 不直接调用 `fopen()`/`open()`/`stat()`
- HexagonFS 提供 `hexagonfs_openat()`、`hexagonfs_read()` 等一致的接口
- 新方法中，DSP 传过来的路径被 HexagonFS 的虚拟目录树重定向

### 4.2 新增方法文件访问模式

```
DSP 调用 apps_std_fopen("libfoo.so", "r")
  → 方法 0 (fopen) 未通过 fopen_with_env
  → DSP 通常先调用 fopen_with_env 打开文件
  → 但为兼容性，fopen 也可以用 HexagonFS 的 openat(rootfd, rootfd, name)
  → 如果 name 以 "/" 开头 → 通过虚拟目录树重映射
  → 否则 → 在默认搜索路径中查找

DSP 调用 apps_std_fopen_fd("libfoo.so", "r", &fd, &len)
  → 通过 HexagonFS 打开文件
  → 读取内容到 DMA-BUF（rpcmem_linux 分配）
  → 返回 fd（DMA-BUF fd）和 len（文件大小）
```

### 4.3 可写操作的处理

**决策**：HexagonFS 扩展为支持写入，所有写操作直接实现。

实现对现有 `hexagonfs_file_ops` 结构的扩展：

```c
// hexagonfs.h 新增
struct hexagonfs_file_ops {
    // ... 现有字段不变 ...
    ssize_t (*write)(struct hexagonfs_fd *fd, size_t size, const void *ptr);  // 新增
    int (*unlink)(struct hexagonfs_fd *dir, const char *name);                // 新增
    int (*mkdir)(struct hexagonfs_fd *dir, const char *name, mode_t mode);    // 新增
    int (*rmdir)(struct hexagonfs_fd *dir, const char *name);                 // 新增
    int (*rename)(struct hexagonfs_fd *olddir, const char *oldname,
                  struct hexagonfs_fd *newdir, const char *newname);          // 新增
    int (*truncate)(struct hexagonfs_fd *fd, off_t length);                   // 新增
};
```

**后端策略**：

| 后端 | 写操作行为 |
|------|-----------|
| `hexagonfs_mapped_ops` | ✅ 对映射的真实文件可写 |
| `hexagonfs_mapped_or_empty_ops` | ⚠️ 存在真实文件则可写，虚拟空文件不可写 |
| `hexagonfs_mapped_sysfs_ops` | ❌ sysfs 一般不可写 |
| `hexagonfs_virt_dir_ops` | ✅ 支持 mkdir/rmdir/unlink（操作真实目录） |

**安全边界**：所有写操作限制在 HexagonFS root 目录内，不允许路径逃逸（现有 `hexagonfs_openat` 已经有路径解析安全性）。

---

## 5. rpcmem 替代设计

### 5.1 需求分析

FastRPC 的 `rpcmem` 提供两个关键功能：
1. 分配 Ion/DMA-BUF 内存 → 可在 CPU 和 DSP 之间共享
2. 将 fd 转换为指针、将指针转换为 fd

### 5.2 Linux 方案

```c
// new/rpcmem_linux.h
// 基于 Linux DMA-BUF heaps

struct rpcmem_buf {
    int fd;           // DMA-BUF fd
    void *ptr;        // mmap 指针
    size_t size;      // 分配大小
};

// 分配 DMA-BUF 内存
int rpcmem_alloc(int heap_id, uint32_t flags, size_t size, struct rpcmem_buf **buf);

// 释放
void rpcmem_free(struct rpcmem_buf *buf);

// fd → 指针
void *rpcmem_mmap(int fd, size_t size);

// 指针 → fd
int rpcmem_to_fd(void *ptr);
```

---

## 6. 实现记录

### 阶段 1: 基础补全 ✅ 已完成 (2025-01)

| # | 方法 | 变更 |
|---|------|------|
| 1 | `apps_std_fopen()` | 新增 — 走 HexagonFS, library path → root fallback |
| 2 | `apps_std_flen()` | 新增 — hexagonfs_fstat → st_size |
| 3 | `apps_std_ftell()` | 新增 — hexagonfs_lseek(fd, 0, SEEK_CUR) |
| 4 | `apps_std_rewind()` | 新增 — hexagonfs_lseek(fd, 0, SEEK_SET) |
| 5-7 | `feof/ferror/clearerr` | 新增 — apps_std_ctx 新增 fd_eof[256]/fd_err[256] 状态追踪 |
| 8 | `apps_std_fgets()` | 新增 — 逐字节 hexagonfs_read 直到 '\n' 或 buf_size |
| 9 | `apps_std_print_string()` | 新增 — printf("DSP: %s\n", str) |
| 10 | `apps_std_fileExists()` | 新增 — hexagonfs_openat + fstat |

**架构变更**: `struct apps_std_ctx` 新增 `bool fd_eof[256]` 和 `bool fd_err[256]`。
**fread/fclose/fopen_with_env**: 更新以维护 EOF/err 状态。`fopen_with_env` 移除写模式拒绝。

### 阶段 2: 环境操作 ✅ 已完成

| # | 方法 | 变更 |
|---|------|------|
| 11-13 | `getenv/setenv/unsetenv` | 新增 — 直接调用 libc getenv/setenv/unsetenv |

### 阶段 3: HexagonFS 写扩展 + apps_std 写方法 ✅ 已完成

#### 3a. HexagonFS 扩展

**`hexagonfs.h` 变更**:
```c
struct hexagonfs_file_ops {
    // ... 现有字段不变 ...
    ssize_t (*write)(struct hexagonfs_fd *fd, size_t size, const void *ptr);   // 新增
    int (*truncate)(struct hexagonfs_fd *fd, off_t length);                     // 新增
    int (*unlink)(struct hexagonfs_fd *dir, const char *name);                  // 新增
    int (*mkdir)(struct hexagonfs_fd *dir, const char *name, mode_t mode);      // 新增
    int (*rmdir)(struct hexagonfs_fd *dir, const char *name);                   // 新增
};
```

**新增 API**: `hexagonfs_write()` / `hexagonfs_ftruncate()` / `hexagonfs_unlink()` / `hexagonfs_mkdir()` / `hexagonfs_rmdir()`

**`hexagonfs_mapped.c` 变更**:
- `struct mapped_ctx` 新增 `int flags` 字段
- `mapped_from_dirent()` / `mapped_openat()`: 改为先尝试 `O_RDWR`，`EACCES/EROFS` 时回退 `O_RDONLY`
- 新增 `mapped_write()` — 直接 `write()` 到物理 fd
- 新增 `mapped_truncate()` — 直接 `ftruncate()` 到物理 fd

**设计决定**: 不清除 virt_dir 的 mkdir/rmdir/unlink（需要物理路径解析，待后续实现）。

#### 3b. apps_std 写方法

| # | 方法 | 变更 |
|---|------|------|
| 14 | `apps_std_fwrite()` | 新增 — hexagonfs_write |
| 15 | `apps_std_fremove()` | 新增 — hexagonfs_unlink(..., rootfd, name) |
| 16-17 | `apps_std_mkdir/rmdir()` | 新增 — hexagonfs_mkdir/rmdir(..., rootfd, ...) |
| 18 | `apps_std_ftrunc()` | 新增 — hexagonfs_ftruncate |
| 19 | `apps_std_fsync()` | 新增 — 空操作 (POSIX write-through) |
| 20 | `apps_std_fdopen_decrypt()` | 新增 — 直接返回原 fd |
| 21 | `apps_std_frename()` | 新增 — 直接 `rename()` |
| 22-24 | `apps_std_fopen_fd/fclose_fd/fopen_with_env_fd` | 新增 — 走 HexagonFS |

### 阶段 4: apps_std 剩余方法 ✅ 已完成

| # | 方法 | 变更 |
|---|------|------|
| 25 | `apps_std_freopen()` | 新增 — fclose 旧 fd + fopen 新 name。.def 参数修正 `(1,0,1,1)` → `(1,2,1,0)` |
| 26 | `apps_std_fgetpos()` | 新增 — lseek CUR + memcpy fpos_t 到 outbuf |
| 27 | `apps_std_fsetpos()` | 新增 — memcpy fpos_t + lseek SET |
| 28 | `apps_std_get_search_paths_with_env()` | 新增 — 返回空序列 numPaths=0, maxPathLen=0 (设计说明见下) |

### 阶段 5: apps_mem 补全 ✅ 已完成

| # | 方法 | IOCTL | 变更 |
|---|------|-------|------|
| 29 | `apps_mem_request_map()` | FASTRPC_IOCTL_MMAP | 新增 — 32 位包装 |
| 30 | `apps_mem_request_unmap()` | FASTRPC_IOCTL_MUNMAP | 新增 |
| 31 | `apps_mem_request_map64()` | FASTRPC_IOCTL_MMAP | ✅ 已有 |
| 32 | `apps_mem_request_unmap64()` | FASTRPC_IOCTL_MUNMAP | 新增 |
| 33 | `apps_mem_share_map()` | mmap + FASTRPC_IOCTL_MMAP | 新增 — mmap DMA-BUF fd, 映射到 DSP |
| 34 | `apps_mem_share_unmap()` | FASTRPC_IOCTL_MUNMAP | 新增 |
| 35 | `apps_mem_dma_handle_map()` | FASTRPC_IOCTL_MEM_MAP | 新增 |
| 36 | `apps_mem_dma_handle_unmap()` | FASTRPC_IOCTL_MEM_UNMAP | 新增 |

### 设计决定记录

#### method 21: get_search_paths_with_env

返回空序列 (`numPaths=0, maxPathLen=0`) 而非完整搜索路径列表。

- **原因**: FastRPC 使用 slim type `_cstring1_t[]` 变长序列 wire 格式，需要在 iobuffer 编码器中新增 ~150 行序列编码支持。此方法为纯信息性调试 API。
- **影响**: DSP 收到空列表后回退使用 `ADSP_LIBRARY_PATH` 环境变量，`fopen_with_env` 已完整处理。HexagonFS 在文件操作层透明重定向路径，DSP 无需运行时发现搜索路径。

#### HexagonFS virt_dir 缺少 mkdir/rmdir/unlink

virt_dir 后端维护纯虚拟目录树，不持有物理路径。目录创建/删除需要物理路径解析。待后续为 virt_dir 添加 root 物理路径前缀后实现。

#### rpcmem_linux 模块

apps_mem 的 `share_map` / `dma_handle_map` 直接使用 ioctl（FASTRPC_IOCTL_MMAP/MEM_MAP），不依赖独立的 rpcmem 内存分配器。独立的 rpcmem_linux 模块（DMA-BUF heap 分配）留待未来需要用户态内存分配时添加。

---

## 7. 验证策略

每个方法完成时验证：

1. **编译** — `meson compile` 通过
2. **链接** — hexagonrpcd 正确链接新方法
3. **单元测试** — 对可独立测试的方法写 meson test 用例
4. **集成测试** — 在 QEMU/设备上反向隧道验证

---

## 8. 设计自审

| 问题 | 回答 |
|------|------|
| 每单元职责清晰？ | ✅ apps_std = 文件操作，apps_mem = 内存操作，HexagonFS = 路径重定向 + 文件读写 |
| 接口明确？ | ✅ HexagonFS 提供 `<hexagonfs.h>` API，所有文件操作（含写）经 HexagonFS 路由 |
| 无过早优化？ | ✅ domain、perf、config 都标为 P2/P3 |
| YAGNI 砍了什么？ | ❌ 无 QAIC skel 生成（不依赖 IDL）；❌ 无 QList 等复杂数据结构；❌ 无 Windows 支持 |
| 有无未定的地方？ | ⚠️ rpcmem_linux 需要 `/dev/dma_heap/` 设备节点，非所有 Linux 内核都有此支持 |
