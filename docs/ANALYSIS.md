# HexagonRPC 架构分析与 FastRPC 差异对比

## 目录

1. [项目概述](#项目概述)
2. [架构总览](#架构总览)
3. [组件详解](#组件详解)
   - [libhexagonrpc — 共享库](#libhexagonrpc--共享库)
   - [hexagonrpcd — 反向隧道守护进程](#hexagonrpcd--反向隧道守护进程)
   - [HexagonFS — 虚拟只读文件系统](#hexagonfs--虚拟只读文件系统)
4. [构建与运行](#构建与运行)
5. [接口（Interface）体系](#接口interface体系)
6. [与官方 FastRPC 差异对比](#与官方-fastrpc-差异对比)
7. [未实现功能清单](#未实现功能清单)
8. [未来计划](#未来计划)

---

## 项目概述

HexagonRPC 是 [Sensor Shell](https://gitlab.com/sensh) 项目对 Qualcomm FastRPC 框架的**独立重新实现**。
项目名中的 "Hexagon" 指 Qualcomm Hexagon DSP 处理器。项目使用 GPLv3 许可。

与官方 FastRPC 不同，HexagonRPC：

- **不需要 QAIC IDL 编译器**：方法签名使用手写 `.def` 文件定义
- **不需要 Qualcomm 闭源库**：纯开源 C 实现
- **使用 Meson 构建**：而非 autotools
- **聚焦于反向隧道**：主要让 AP（Application Processor）接收并处理来自 DSP 的 RPC 调用
- **仅支持 Linux**：不支持 Windows，Android 支持通过 Android.bp

项目版本：**0.4.0**（来自 `meson.build`）

---

## 架构总览

```
┌────────────────────────────────────────────────────┐
│                    应用处理器 (AP)                     │
│                                                    │
│  ┌──────────────┐  ┌──────────────────────────────┐ │
│  │              │  │  │     反向隧道 (listener)   │  │ │
│  │ fastrpc2()───┼──┼──┤ adsp_listener_next2()   │  │ │
│  │              │  │  │       ↕                  │  │ │
│  └──────────────┘  │  │  接口调度                 │  │ │
│                    │  │  ┌───────┬──────┬──────┐ │  │ │
│                    │  │  │localctl│apps  │apps  │ │  │ │
│                    │  │  │(remo- │ _std │ _mem │ │  │ │
│                    │  │  │tectl) │      │      │ │  │ │
│                    │  │  └───────┴──┬───┴──────┘ │  │ │
│                    │  │            │              │  │ │
│                    │  │       HexagonFS           │  │ │
│                    │  │     (虚拟只读文件系统)       │  │ │
│                    │  └────────────────────────┘  │  │
│                    └──────────────┬───────────────┘  │
│                                   │                  │
│                        libhexagonrpc.so              │
│                    (ioctl 包装器 + 接口定义)           │
│                                   │                  │
└───────────────────────────────────┼──────────────────┘
                                    │ ioctl(FASTRPC_IOCTL_*)
                          ┌─────────┴─────────┐
                          │  Linux 内核        │
                          │  fastrpc 驱动      │
                          └─────────┬─────────┘
                                    │ rpmsg
                          ┌─────────┴─────────┐
                          │  Hexagon DSP      │
                          │  (aDSP / sDSP)    │
                          └───────────────────┘
```

**核心数据流**：

1. DSP 发起 RPC 调用 → 通过 rpmsg 到达内核 fastrpc 驱动
2. hexagonrpcd 通过 `adsp_listener_next2()` 轮询接收调用
3. 根据 handle + method ID 分发到对应接口实现
4. 接口实现处理请求（读文件 / 映射内存 / 查找接口），返回结果

---

## 组件详解

### libhexagonrpc — 共享库

**文件**: `libhexagonrpc/`  
**产物**: `libhexagonrpc.so`（API version 0.4）

提供两个层次的 API：

#### 1. 底层：直接 fd + handle

```c
int fastrpc2(const struct fastrpc_function_def_interp2 *def,
             int fd, uint32_t handle, ...);

int vfastrpc2(const struct fastrpc_function_def_interp2 *def,
              int fd, uint32_t handle, va_list arg_list);
```

直接传 FD 和 handle，适合一次性调用。

#### 2. 上下文层：context 对象

```c
struct fastrpc_context *fastrpc_create_context(int fd, uint32_t handle);
void fastrpc_destroy_context(struct fastrpc_context *ctx);

int fastrpc(const struct fastrpc_function_def_interp2 *def,
            const struct fastrpc_context *ctx, ...);
```

将 fd + handle 封装为可复用的 context。

#### 3. 会话层：环境变量传递 FD

```c
int hexagonrpc_fd_from_env(void);  // 从 HEXAGONRPC_FD 环境变量获取 FD
```

hexagonrpcd 通过 `HEXAGONRPC_FD` 将已打开的 FD 传给子进程。

#### 4. 方法定义宏

```c
// 在 .def 文件中定义：
HEXAGONRPC_DEFINE_REMOTE_METHOD(mid, name, innums, inbufs, outnums, outbufs)

// 例如：
HEXAGONRPC_DEFINE_REMOTE_METHOD(4, adsp_listener_next2, 2, 1, 4, 1)
```

解释：
- `mid` — 方法 ID
- `innums` — 第一个输入缓冲区中的 32-bit 值数量（不含各 buffer 的长度）
- `inbufs` — 除第一个外的输入缓冲区数量
- `outnums` — 第一个输出缓冲区中的 32-bit 值数量
- `outbufs` — 除第一个外的输出缓冲区数量

这些数字对应 `REMOTE_SCALARS_MAKE(method, in_count, out_count)` 的参数，
其中 `in_count` = `in_bufs + (has_in_nums_or_bufs ? 1 : 0)`，
`out_count` = `out_bufs + (has_out_nums ? 1 : 0)`。

---

### hexagonrpcd — 反向隧道守护进程

**文件**: `hexagonrpcd/rpcd.c`、`listener.c`  
**产物**: `hexagonrpcd`

核心功能：打开 FastRPC 设备节点，建立与 DSP 的双向通信通道。

#### 命令行参数

| 参数 | 说明 |
|------|------|
| `-f DEVICE` | **必需**。FastRPC 设备节点路径，如 `/dev/fastrpc-adsp` |
| `-d DSP` | DSP 名称（默认空），用于构造 HexagonFS 路径 |
| `-s` | 以 INIT_ATTACH_SNS 模式连接（sensorspd） |
| `-c SHELL` | 创建新 PD 并加载指定 ELF 文件 |
| `-p PROGRAM` | 启动子客户端程序，通过 `HEXAGONRPC_FD` 共享 FD |
| `-R DIR` | HexagonFS 根目录（默认 `/usr/share/qcom/`） |

#### 启动流程

```
main()
  ├─ guess_device_directory_from_compatible()  // 从 device-tree 猜测路径
  ├─ open(fastrpc_node)                        // 打开 /dev/fastrpc-*
  ├─ ioctl(FASTRPC_IOCTL_INIT_ATTACH)          // 附加到 DSP
  │   或 FASTRPC_IOCTL_INIT_ATTACH_SNS          // sensors 模式
  │   或 FASTRPC_IOCTL_INIT_CREATE + DMA        // 创建自定义 PD
  ├─ setup_environment(fd)                     // 设置 HEXAGONRPC_FD 环境变量
  ├─ start_clients()                           // fork + exec 子客户端
  ├─ start_reverse_tunnel(fd, device_dir, dsp) // 主循环（阻塞）
  │    ├─ construct_root_dir()                 // 构建虚拟文件系统
  │    ├─ fastrpc_localctl_init()              // remotectl 接口
  │    ├─ fastrpc_apps_std_init()              // 文件系统接口
  │    ├─ fastrpc_apps_mem_init()              // 内存映射接口
  │    ├─ register_fastrpc_listener()          // 注册 adsp_default_listener
  │    └─ run_fastrpc_listener()               // 事件循环（阻塞）
  └─ terminate_clients()
```

#### 反向隧道事件循环 (`run_fastrpc_listener()`)

```
while true:
    adsp_listener_next2(fd, result, rctx,
                        outbufs,        // 上次的返回值
                        &rctx, &handle, &sc,
                        &inbufs_len, inbufs)  // 接收新调用

    inbuf_decode(sc, inbufs)      // 解码输入参数
    invoke_requested_procedure()   // 根据 handle+method 分发
    outbufs_encode(sc, returned)  // 编码返回值
```

**接口调度** (`invoke_requested_procedure()`):

1. 根据 `handle` 在 `ifaces[]` 数组中查找接口
2. 根据 `method`（从 sc 中提取）在接口的 `procs[]` 中查找实现
3. 验证缓冲区数量和大小
4. 调用 `impl->impl(data, inbufs, outbufs)`

---





---

### HexagonFS — 虚拟只读文件系统

**文件**: `hexagonrpcd/hexagonfs.c`、`hexagonfs.h`、`hexagonfs_mapped.c`、`hexagonfs_virt_dir.c`、`hexagonfs_plat_subtype_name.c`、`rpcd_builder.c`

DSP 上的代码通过 `apps_std` 接口访问文件。hexagonrpcd 将 DSP 请求的文件路径映射到本地文件系统中。

#### 虚拟目录布局

`construct_root_dir()` 在 `rpcd_builder.c` 中构建以下虚拟树：

```
/                               → <prefix>/              (root)
├── mnt/vendor/persist/
│   └── sensors/registry/
│       └── registry            → <prefix>/sensors/registry/
├── persist/
│   └── sensors/registry/
│       └── registry            → <prefix>/sensors/registry/  (hardlink)
├── sys/devices/
│   └── soc0                    → <prefix>/socinfo/           (sysfs)
├── system/vendor/
│   └── etc/
│       ├── sensors/
│       │   ├── config           → <prefix>/sensors/config/
│       │   └── sns_reg_config   → <prefix>/sensors/sns_reg.conf
│       └── acdbdata             → <prefix>/acdb/
├── usr/lib/qcom/
│   └── adsp                    → <prefix>/dsp/<dsp>          (DSP 库)
└── vendor/
    └── etc/
        ├── sensors/
        │   ├── config           → <prefix>/sensors/config/
        │   └── sns_reg_config   → <prefix>/sensors/sns_reg.conf
        └── acdbdata             → <prefix>/acdb/
```

#### 默认路径映射 (prefix = `/usr/share/qcom/`)

| DSP 请求路径 | 本地路径 |
|-------------|---------|
| `acdb/` | `<prefix>/acdb/` → `/vendor/etc/acdbdata` |
| `dsp/` | `<prefix>/dsp/<dsp>` → `/vendor/dsp` |
| `sensors/config/` | `<prefix>/sensors/config/` → `/vendor/etc/sensors/config` |
| `sensors/registry/` | `<prefix>/sensors/registry/` → `/mnt/vendor/persist/sensors/registry/registry` |
| `sensors/sns_reg.conf` | `<prefix>/sensors/sns_reg.conf` → `/vendor/etc/sensors/sns_reg_config` |
| `socinfo/` | `<prefix>/socinfo/` → `/sys/devices/soc0` |

#### 文件操作实现

| 操作 | HexagonFS 函数 | apps_std method |
|------|---------------|-----------------|
| openat | `hexagonfs_openat()` | method 19 (fopen_with_env), 26 (opendir) |
| close | `hexagonfs_close()` | method 3 (fclose), 27 (closedir) |
| read | `hexagonfs_read()` | method 4 (fread) |
| readdir | `hexagonfs_readdir()` | method 28 (readdir) |
| lseek | `hexagonfs_lseek()` | method 9 (fseek) |
| fstat | `hexagonfs_fstat()` | method 31 (stat) |

**限制**:
- 所有文件**只读**（尝试写入返回 `AEE_EUNSUPPORTED`）
- 最大同时打开文件数：256 (`HEXAGONFS_MAX_FD`)
- 仅支持 `ADSP_LIBRARY_PATH` 和 `ADSP_AVS_CFG_PATH` 两个环境变量作为搜索路径
- `fopen_with_env` 仅支持 `r` 模式

#### 文件类型

`hexagonfs_file_ops` 有五种实现：

| 实现 | 说明 |
|------|------|
| `hexagonfs_mapped_ops` | 直接映射到本地文件的普通文件 |
| `hexagonfs_mapped_or_empty_ops` | 映射到本地文件，不存在则返回空（用于可选文件） |
| `hexagonfs_mapped_sysfs_ops` | sysfs 文件映射（带特殊路径处理） |
| `hexagonfs_plat_subtype_name_ops` | 平台子类型名称 |
| `hexagonfs_virt_dir_ops` | 虚拟目录（内存中的 dirent 列表） |

---

## 构建与运行

### 依赖

- Meson >= 1.1
- C 编译器（gcc/clang）
- Linux 内核 fastrpc 驱动（`/dev/fastrpc-*` 设备节点）
- 可选：valgrind（测试用）

### 编译

```bash
cd hexagonrpc
meson setup build
ninja -C build
```

启用详细日志：

```bash
meson setup build -Dhexagonrpcd_verbose=true
ninja -C build
```

### 安装

```bash
ninja -C build install
```

安装位置（可通过 meson 选项覆盖）：
- `libhexagonrpc.so` → `<libdir>/`
- `hexagonrpcd` → `<bindir>/`
- systemd 服务 / Android init rc → 相应系统路径

### 运行

```bash
# 基础：连接 ADSP
hexagonrpcd -f /dev/fastrpc-adsp

# 连接 SDSP
hexagonrpcd -f /dev/fastrpc-sdsp -d sdsp

# Sensors PD 模式
hexagonrpcd -f /dev/fastrpc-adsp -s

# 指定 HexagonFS 根目录（约定格式）
hexagonrpcd -f /dev/fastrpc-adsp -R /usr/share/qcom/sdm845/SHIFT/axolotl

# 带子客户端
```

### 运行测试

```bash
meson setup build
meson test -C build
```

测试内容：
- `test_iobuffer` — I/O 缓冲区编解码测试
- `test_hexagonfs` — 虚拟文件系统测试

---

## 接口（Interface）体系

### 接口定义方式

接口在两个层面定义：

1. **`.def` 文件**：定义方法签名（msg_id + 参数布局），使用 `HEXAGONRPC_DEFINE_REMOTE_METHOD` 宏
2. **实现文件**：`fastrpc_interface` 结构体 + `fastrpc_function_impl` 数组

### 已实现的接口

#### 1. remotectl (handle 0)

**文件**: `localctl.c`，定义 `include/libhexagonrpc/interfaces/remotectl.def`

| Method ID | 方法名 | in_nums | in_bufs | out_nums | out_bufs | 状态 |
|-----------|--------|---------|---------|----------|----------|------|
| 0 | remotectl_open | 0 | 1 | 2 | 1 | ✅ 已实现 |
| 1 | remotectl_close | 1 | 0 | 1 | 1 | ✅ 已实现 |

`remotectl_open` 在 `ifaces[]` 数组中按名称查找接口并返回 handle。
`remotectl_close` 是空操作（接口是静态的）。

#### 2. apps_std (handle 动态分配)

**文件**: `apps_std.c`，定义 `hexagonrpcd/interfaces/apps_std.def`

| Method ID | 方法名 | 状态 | 说明 |
|-----------|--------|------|------|
| 1 | apps_std_freopen | ❌ 未实现 | 仅定义签名，无实现 |
| 2 | apps_std_fflush | ✅ | 空操作（文件只读） |
| 3 | apps_std_fclose | ✅ | 关闭 HexagonFS 文件 |
| 4 | apps_std_fread | ✅ | 读取文件内容 |
| 9 | apps_std_fseek | ✅ | 文件定位 |
| 19 | apps_std_fopen_with_env | ✅ | 通过环境变量搜索并打开文件 |
| 26 | apps_std_opendir | ✅ | 打开目录 |
| 27 | apps_std_closedir | ✅ | 关闭目录 |
| 28 | apps_std_readdir | ✅ | 读取目录项 |
| 31 | apps_std_stat | ✅ | 获取文件状态 |

**共 10/32 个 slot 已实现**（slot 数量由 `n_procs = 32` 定义）。

#### 3. apps_mem (handle 动态分配)

**文件**: `apps_mem.c`，定义 `hexagonrpcd/interfaces/apps_mem.def`

| Method ID | 方法名 | 状态 | 说明 |
|-----------|--------|------|------|
| 2 | apps_mem_request_map64 | ✅ | 映射内存到 DSP |
| 其它 (0,1,3,4,5) | — | ❌ | 未实现 |

**共 1/6 个 slot 已实现**。

实现细节：只支持 `ADSP_MMAP_ADD_PAGES` flag 的映射请求，通过 `FASTRPC_IOCTL_MMAP` ioctl 映射。

#### 4. adsp_listener (handle 3)

**文件**: `listener.c`，定义 `hexagonrpcd/interfaces/adsp_listener.def`

| Method ID | 方法名 | 状态 | 说明 |
|-----------|--------|------|------|
| 3 | adsp_listener_init2 | ✅ | 初始化监听器 |
| 4 | adsp_listener_next2 | ✅ | 获取下一个 RPC 调用 |

这是反向隧道的核心：从 DSP 接收方法调用请求。

**限制**: 当前只支持最大 256 字节的输入缓冲区（`inbufs[256]`），注释说明 "Large (>256B) input buffers aren't implemented"。

#### 5. adsp_default_listener (handle 动态)

**文件**: 定义在 `hexagonrpcd/interfaces/adsp_default_listener.def`

| Method ID | 方法名 | 状态 | 说明 |
|-----------|--------|------|------|
| 0 | adsp_default_listener_register | ✅ | 注册默认监听器 |

在 DSP 上注册反向隧道监听器。



| Method ID | 方法名 | 状态 | 说明 |
|-----------|--------|------|------|


---

## 方法覆盖矩阵（v0.4 → 现代化后）

### apps_std — 37/37 完成

| ID | 方法 | 实现 |
|----|------|------|
| 0 | fopen | ✅ HexagonFS openat |
| 1 | freopen | ✅ close + open |
| 2 | fflush | ✅ 空操作 |
| 3 | fclose | ✅ HexagonFS close |
| 4 | fread | ✅ HexagonFS read + EOF 追踪 |
| 5 | fwrite | ✅ HexagonFS write |
| 6 | fgetpos | ✅ lseek CUR + memcpy fpos_t |
| 7 | fsetpos | ✅ memcpy fpos_t + lseek SET |
| 8 | ftell | ✅ lseek CUR |
| 9 | fseek | ✅ HexagonFS lseek |
| 10 | flen | ✅ HexagonFS fstat → st_size |
| 11 | rewind | ✅ lseek SET + 重置 EOF/err |
| 12 | feof | ✅ 从 ctx.fd_eof[] 读取 |
| 13 | ferror | ✅ 从 ctx.fd_err[] 读取 |
| 14 | clearerr | ✅ 重置 ctx.fd_eof[]/fd_err[] |
| 15 | print_string | ✅ printf |
| 16 | getenv | ✅ libc getenv |
| 17 | setenv | ✅ libc setenv |
| 18 | unsetenv | ✅ libc unsetenv |
| 19 | fopen_with_env | ✅ HexagonFS openat + 路径搜索 |
| 20 | fgets | ✅ 逐字节读到换行 |
| 21 | get_search_paths_with_env | ✅ 空序列 (0 paths, 0 maxLen) |
| 22 | fileExists | ✅ HexagonFS open + stat |
| 23 | fsync | ✅ 空操作 (POSIX) |
| 24 | fremove | ✅ HexagonFS unlink |
| 25 | fdopen_decrypt | ✅ 直接返回 fd |
| 26 | opendir | ✅ HexagonFS openat |
| 27 | closedir | ✅ HexagonFS close |
| 28 | readdir | ✅ HexagonFS readdir |
| 29 | mkdir | ✅ HexagonFS mkdir |
| 30 | rmdir | ✅ HexagonFS rmdir |
| 31 | stat | ✅ HexagonFS fstat |
| 32 | ftrunc | ✅ HexagonFS ftruncate |
| 33 | frename | ✅ libc rename |
| 34 | fopen_fd | ✅ HexagonFS openat + fstat |
| 35 | fclose_fd | ✅ HexagonFS close |
| 36 | fopen_with_env_fd | ✅ HexagonFS openat + fstat |

### apps_mem — 8/8 完成

| ID | 方法 | IOCTL |
|----|------|-------|
| 0 | request_map | FASTRPC_IOCTL_MMAP |
| 1 | request_unmap | FASTRPC_IOCTL_MUNMAP |
| 2 | request_map64 | FASTRPC_IOCTL_MMAP |
| 3 | request_unmap64 | FASTRPC_IOCTL_MUNMAP |
| 4 | share_map | mmap + FASTRPC_IOCTL_MMAP |
| 5 | share_unmap | FASTRPC_IOCTL_MUNMAP |
| 6 | dma_handle_map | FASTRPC_IOCTL_MEM_MAP |
| 7 | dma_handle_unmap | FASTRPC_IOCTL_MEM_UNMAP |

### HexagonFS 扩展

| 操作 | 状态 |
|------|------|
| open / close / read / lseek / fstat / readdir | ✅ 已有 |
| write | ✅ 新增 — mapped 后端 |
| truncate | ✅ 新增 — mapped 后端 |
| unlink / mkdir / rmdir | ✅ API 新增 — virt_dir 后端待实现 |
| O_RDWR 模式 | ✅ 新增 — 先试 RDWR，不可写回退 RDONLY |

---

## 与官方 FastRPC 差异对比

### 设计理念差异

| 方面 | 官方 FastRPC（quic/fastrpc） | HexagonRPC |
|------|---------------------------|------------|
| 许可证 | BSD 3-Clause | GPLv3 |
| 构建系统 | autotools (autoconf/automake) | Meson |
| IDL 编译器 | QAIC（Qualcomm 闭源） | 不需要，手动 `.def` |
| 平台支持 | Linux, Android, Windows | Linux（Android 通过 Android.bp） |
| DSP 支持 | ADSP, CDSP, SDSP, GDSP | ADSP, SDSP（理论上） |
| PD 支持 | rootpd, audiopd, sensorspd 等 | rootpd, sensorspd, 自定义 PD |
| 目标用户 | 通用 FastRPC 应用开发 | Sensor Shell 项目（传感器） |

### apps_std 方法对比

官方 FastRPC 的 `apps_std` 约 **37 个**方法（含 `_skel_invoke` 中的子方法）。
HexagonRPC 实现了 **10 个**。

| 方法 | 官方 FastRPC | HexagonRPC | 备注 |
|------|:----------:|:----------:|------|
| fopen (0) | ✅ | ❌ | |
| freopen (1) | ✅ | ❌ | 有签名无实现 |
| fflush (2) | ✅ | ✅ | hexagonrpc 中为空操作 |
| fclose (3) | ✅ | ✅ | |
| fread (4) | ✅ | ✅ | |
| fwrite (5) | ✅ | ❌ | 文件系统只读 |
| fgetpos (6) | ✅ | ❌ | |
| fsetpos (7) | ✅ | ❌ | |
| ftell (8) | ✅ | ❌ | |
| fseek (9) | ✅ | ✅ | |
| flen (10) | ✅ | ❌ | |
| rewind (11) | ✅ | ❌ | |
| feof (12) | ✅ | ❌ | |
| ferror (13) | ✅ | ❌ | |
| clearerr (14) | ✅ | ❌ | |
| print_string (15) | ✅ | ❌ | |
| getenv (16) | ✅ | ❌ | |
| setenv (17) | ✅ | ❌ | |
| unsetenv (18) | ✅ | ❌ | |
| fopen_with_env (19) | ✅ | ✅ | 仅支持 r 模式 |
| fgets (20) | ✅ | ❌ | |
| get_search_paths_with_env (21) | ✅ | ❌ | |
| fileExists (22) | ✅ | ❌ | |
| fsync (23) | ✅ | ❌ | |
| fremove (24) | ✅ | ❌ | |
| fdopen_decrypt (25) | ✅ | ❌ | |
| opendir (26) | ✅ | ✅ | |
| closedir (27) | ✅ | ✅ | |
| readdir (28) | ✅ | ✅ | |
| mkdir (29) | ✅ | ❌ | |
| rmdir (30) | ✅ | ❌ | |
| stat (31) | ✅ | ✅ | |
| ftrunc (32) | ✅ | ❌ | |
| frename (33) | ✅ | ❌ | |
| fopen_fd (34) | ✅ | ❌ | |
| fclose_fd (35) | ✅ | ❌ | |
| fopen_with_env_fd (36) | ✅ | ❌ | |

### apps_mem 方法对比

| 方法 | 官方 FastRPC | HexagonRPC |
|------|:----------:|:----------:|
| request_map (0) | ✅ | ❌ |
| request_unmap (1) | ✅ | ❌ |
| request_map64 (2) | ✅ | ✅ (仅 ADD_PAGES) |
| request_unmap64 (3) | ✅ | ❌ |
| share_map (4) | ✅ | ❌ |
| share_unmap (5) | ✅ | ❌ |
| dma_handle_map | ✅ | ❌ |
| dma_handle_unmap | ✅ | ❌ |

### 架构差异

| 方面 | 官方 FastRPC | HexagonRPC |
|------|------------|------------|
| 多域（domain）支持 | ✅ 完整的多域管理 | ❌ 无 |
| 性能跟踪 | ✅ FASTRPC_ATRACE | ❌ |
| 配置系统 | ✅ YAML 解析器 | ❌ |
| 日志系统 | ✅ FARF + VERIFY | ❌ 仅 fprintf |
| rpcmem 集成 | ✅ Ion/DMA-BUF 分配器 | ❌ 使用内核 ioctl |
| 异常日志传递 | ✅ aee_error 完整 | ⚠️ 仅有错误码表 |
| 多 DSP 支持 | ✅ ADSP/CDSP/SDSP/GDSP | ⚠️ 主要为 ADSP |
| 自动重启 | ✅ 守护进程自动重启 | ❌ |

---

## 未实现功能清单

### 关键缺失

1. **apps_std 大部分文件操作**：fwrite, ftell, feof, ferror, flen, rewind, getenv, setenv, fgets, fsync, mkdir, rmdir, ftrunc, frename 等
2. **apps_mem 大部分内存操作**：unmap, share_map, share_unmap, dma_handle 等
3. **>256 字节的大缓冲区**支持（listener.c 硬编码限制）
4. **rpcmem 集成**：不能使用 Ion/DMA-BUF 从用户空间分配 DSP 可访问内存
5. **多域支持**：不能区分不同 domain 的会话
7. **daemon 自动重启**：不支持 DSP crash 后自动恢复
8. **缺少写入支持**：整个 HexagonFS 是只读的
9. **缺少 handle 传递**：listener.c 中明确说 "Handles are not supported"
10. **缺少性能跟踪和日志基础设施**

### 其他限制

- 没有配置系统（官方 FastRPC 有 YAML 配置）
- 没有 udev 规则（官方 FastRPC 有设备权限管理）
- 只支持 INIT_ATTACH 和 INIT_ATTACH_SNS，不像官方有 audiopd 等多种 PD 支持

---

## 未来计划

来自 README.md 的 "Future plans"：

1. **多进程共享**：当前反向隧道与打开 FD 的进程绑定，其他进程无法使用。计划实现一个守护进程打开设备并让多个客户端发送请求。
2. **DSP 端工作负载卸载**：通过 FastRPC 将计算任务卸载到 DSP。当 FastRPC 函数被调用时，`<name>_skel_invoke` 在 DSP 上的 `lib<name>_skel.so` 中被调用。需要进一步研究构建系统。

---

*文档版本: 0.4.0 (匹配项目版本)*

## 部署基础设施

| 组件 | 文件 | 目标 |
|------|------|------|
| udev 规则 | `data/60-hexagonrpc.rules` | `/usr/lib/udev/rules.d/` |
| sysusers.d | `data/hexagonrpc.conf` | `/usr/lib/sysusers.d/` |
| systemd service | `data/*.service.in` → 5 个单元 | `/lib/systemd/system/` |
| man page | `hexagonrpcd/hexagonrpcd.8` | `man/man8/` |
| man symlinks | `data/install-man-symlinks.sh` | 为 5 个 domain 创建符号链接 |

install-man-symlinks.sh 通过 `$DESTDIR` 前缀与 Meson 自定义安装脚本约定兼容，确保在 makepkg 等打包环境下正确安装。
