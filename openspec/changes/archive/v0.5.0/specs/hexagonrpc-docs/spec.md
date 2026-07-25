# HexagonRPC — 系统规格（基线）

> 本文档描述 HexagonRPC 系统的**当前行为**（真相基线）。
> 格式：每项 Requirement 含 SHALL 声明 + 至少一个 Scenario(WHEN/THEN/AND)。

---

## Requirements

### Requirement: 架构组成

HexagonRPC SHALL 由以下四个主要组件组成：

- **libhexagonrpc**：共享库，提供 ioctl 包装器 `fastrpc2()` / `fastrpc()`、`remotectl_open/close()` 客户端帮助函数和远程方法定义
- **hexagonrpcd**：守护进程，建立反向隧道并服务 DSP 发来的 RPC 请求
- **chrecd**：CHRE（Context Hub Runtime Environment）客户端守护进程
- **HexagonFS**：虚拟文件系统（读写双模式），为 DSP 提供文件访问和 Android 路径 → Linux 路径重定向

#### Scenario: 组件依赖关系

- **WHEN** 系统构建完成
- **THEN** `hexagonrpcd` 和 `chrecd` 链接到 `libhexagonrpc.so`
- **AND** HexagonFS 实现代码编译进 `hexagonrpcd`

---

### Requirement: 构建系统

HexagonRPC SHALL 使用 Meson（>= 1.1）构建，支持可选编译选项。

#### Scenario: 标准构建

- **WHEN** 用户在项目根目录执行 `meson setup build && ninja -C build`
- **THEN** 产出 `libhexagonrpc.so`（在 `build/libhexagonrpc/`）
- **AND** 产出 `hexagonrpcd`（在 `build/hexagonrpcd/`）
- **AND** 产出 `chrecd`（安装在 `$(libexecdir)/hexagonrpc/`）

#### Scenario: 启用详细日志

- **WHEN** 用户以 `-Dhexagonrpcd_verbose=true` 配置
- **THEN** 编译宏 `HEXAGONRPC_VERBOSE` 被定义
- **AND** hexagonrpcd 在每次 RPC 调用时打印详细日志

---

### Requirement: hexagonrpcd 守护进程

hexagonrpcd SHALL 打开 FastRPC 设备节点，建立反向隧道，并服务 DSP 发来的远程方法调用。

#### Scenario: 基本启动

- **WHEN** 用户执行 `hexagonrpcd -f /dev/fastrpc-adsp`
- **THEN** 守护进程打开设备节点，执行 `FASTRPC_IOCTL_INIT_ATTACH`
- **AND** 注册 `adsp_default_listener` 接口
- **AND** 进入反向隧道监听循环

#### Scenario: sensorspd 模式

- **WHEN** 用户执行 `hexagonrpcd -f DEVICE -s`
- **THEN** 守护进程执行 `FASTRPC_IOCTL_INIT_ATTACH_SNS` 而非 `INIT_ATTACH`

#### Scenario: 自定义 ELF PD

- **WHEN** 用户执行 `hexagonrpcd -f DEVICE -c /path/to/shell.elf`
- **THEN** 守护进程通过 `FASTRPC_IOCTL_ALLOC_DMA_BUFF` + `FASTRPC_IOCTL_INIT_CREATE` 创建新 PD

#### Scenario: 子客户端启动

- **WHEN** 用户执行 `hexagonrpcd -f DEVICE -p /path/to/client`
- **THEN** 守护进程 fork 并 exec 客户端程序
- **AND** 通过 `HEXAGONRPC_FD` 环境变量传递 FD
- **AND** hexagonrpcd 退出时发送 SIGTERM 终止所有子客户端

#### Scenario: 设备目录自动检测

- **WHEN** 用户未指定 `-R` 参数
- **THEN** 守护进程读取 `/proc/device-tree/compatible` 和 `/proc/device-tree/model`
- **AND** 猜测 SoC 名称、厂商名称、设备代号
- **AND** 使用第一个存在的 `/usr/share/qcom/{soc}/{vendor}/{device}/` 路径

---

### Requirement: libhexagonrpc API

libhexagonrpc SHALL 提供两个层次的接口：基于 FD+handle 的 `fastrpc2` 和基于 context 的 `fastrpc`。

#### Scenario: fastrpc2 直接调用

- **WHEN** 调用者传入方法定义、FD、handle 和可变参数
- **THEN** 库构造 `fastrpc_invoke_args` 数组
- **AND** 通过 `FASTRPC_IOCTL_INVOKE` ioctl 发送
- **AND** 将输出值写回调用者提供的指针

#### Scenario: fastrpc context 调用

- **WHEN** 调用者通过 `fastrpc_create_context(fd, handle)` 创建 context
- **AND** 调用 `fastrpc(&def, ctx, ...)`
- **THEN** 等价于 `fastrpc2(&def, ctx->fd, ctx->handle, ...)`

#### Scenario: 从环境变量获取 FD

- **WHEN** 调用 `hexagonrpc_fd_from_env()`
- **THEN** 读取 `HEXAGONRPC_FD` 环境变量并解析为 int
- **AND** 变量不存在或无效时返回 -1

---

### Requirement: 反向隧道（Reverse Tunnel）

反向隧道 SHALL 通过 `adsp_listener_next2` 远程方法接收 DSP 发来的 RPC 请求，并调度到本地接口实现。

#### Scenario: 隧道建立

- **WHEN** `run_fastrpc_listener()` 被调用
- **THEN** 先调用 `adsp_listener_init2()` 初始化
- **AND** 循环调用 `adsp_listener_next2()` 接收请求
- **AND** 每次收到请求后解码输入、调用 `invoke_requested_procedure()`、编码输出、返回结果

#### Scenario: 接口调度

- **WHEN** DSP 请求某个 handle + method 的调用
- **THEN** 检查 handle 是否在注册的接口数组中
- **AND** 检查 method 是否在接口的 procs 数组中
- **AND** 验证输入/输出 buffer 数量匹配
- **AND** 调用对应的 `impl` 函数

#### Scenario: 接口注册

- **WHEN** `start_reverse_tunnel()` 被调用
- **THEN** 注册三个接口：`remotectl`(handle=0)、`apps_std`(handle=1)、`apps_mem`(handle=2)
- **AND** `remotectl` 接口的 `localctl_open` 负责按名称查找其他接口

---

### Requirement: 本地接口 — remotectl

remotectl SHALL 实现 `open` 和 `close` 两个方法，用于 DSP 按名称查找本地接口。

#### Scenario: remotectl_open

- **WHEN** DSP 调用 `remotectl.open("apps_std")`
- **THEN** 在接口数组中查找名为 "apps_std" 的接口
- **AND** 返回其在数组中的索引作为 handle
- **AND** 未找到时返回 error=-5

---

### Requirement: 本地接口 — apps_std（文件系统）

apps_std SHALL 为 DSP 提供文件 I/O 操作，通过 HexagonFS 虚拟文件系统实现。

#### Scenario: 已实现的方法

- **WHEN** DSP 调用以下方法
- **THEN** 以下方法已实现并返回正确结果：

| Method ID | 名称 | 功能 |
|-----------|------|------|
| 2 | fflush | 空操作（返回 0） |
| 3 | fclose | 关闭 HexagonFS 文件描述符 |
| 4 | fread | 读取文件内容 |
| 9 | fseek | 文件定位（SEEK_SET/CUR/END） |
| 19 | fopen_with_env | 通过环境变量路径打开文件（只支持 ADSP_LIBRARY_PATH / ADSP_AVS_CFG_PATH） |
| 26 | opendir | 打开目录 |
| 27 | closedir | 关闭目录 |
| 28 | readdir | 读取目录条目 |
| 31 | stat | 获取文件状态信息 |

#### Scenario: 仅支持只读

- **WHEN** DSP 尝试以写模式打开文件（mode='w' 或 'a'）
- **THEN** 返回 `AEE_EUNSUPPORTED`

---

### Requirement: 本地接口 — apps_mem（内存映射）

apps_mem SHALL 为 DSP 提供内存映射操作。

#### Scenario: request_map64

- **WHEN** DSP 调用 `apps_mem.request_map64`
- **AND** rflags 包含 `ADSP_MMAP_ADD_PAGES` (0x1000)
- **THEN** 通过 `FASTRPC_IOCTL_MMAP` ioctl 分配内存
- **AND** 返回映射后的虚拟地址

#### Scenario: 不支持 DMA Buffer 请求

- **WHEN** DSP 调用 `apps_mem.request_map64` 且 rflags 不包含 `ADSP_MMAP_ADD_PAGES`
- **THEN** 返回 `AEE_EUNSUPPORTED`

---

### Requirement: HexagonFS 虚拟文件系统

HexagonFS SHALL 为 DSP 提供只读的虚拟文件系统视图，将路径映射到宿主机上的实际文件。

#### Scenario: 目录布局

- **WHEN** HexagonFS 根目录被构造
- **THEN** 提供以下虚拟路径映射：

| 虚拟路径 | 物理路径 |
|----------|----------|
| `/acdb/` | `{root}/acdb/` |
| `/dsp/{dsp}/` | `{root}/dsp/{dsp}/` |
| `/sensors/config/` | `{root}/sensors/config/` |
| `/sensors/registry/` | `{root}/sensors/registry/` |
| `/sensors/sns_reg.conf` | `{root}/sensors/sns_reg.conf` |
| `/socinfo/` | `{root}/socinfo/` |
| `/vendor/etc/acdbdata/` | → `/acdb/`（符号链接风格） |
| `/vendor/etc/sensors/` | → `/sensors/` |
| `/mnt/vendor/persist/` | → `/persist/` |

#### Scenario: 文件操作

- **WHEN** DSP 通过 HexagonFS 操作文件
- **THEN** 支持的操作：openat、close、read、lseek、readdir、fstat
- **AND** 不支持写入操作

#### Scenario: FD 管理

- **WHEN** HexagonFS 分配文件描述符
- **THEN** 使用最多 256 个 FD（`HEXAGONFS_MAX_FD=256`）
- **AND** 按最早可用编号分配

---

### Requirement: CHRE 客户端（chrecd）

chrecd SHALL 作为 CHRE 客户端，通过共享 FD 与 DSP 通信。

#### Scenario: CHRE 启动

- **WHEN** `chrecd` 启动且 `HEXAGONRPC_FD` 已设置
- **THEN** 通过 `remotectl_open("chre_slpi")` 获取接口
- **AND** 调用 `chre_slpi_start_thread()`
- **AND** 调用 `chre_slpi_wait_on_thread_exit()` 等待线程结束

---

### Requirement: 接口定义机制

HexagonRPC SHALL 使用 `.def` 文件定义远程方法签名，无需 QAIC IDL 编译器。

#### Scenario: 方法定义宏

- **WHEN** 编写 `.def` 文件
- **THEN** 使用 `HEXAGONRPC_DEFINE_REMOTE_METHOD(mid, name, innums, inbufs, outnums, outbufs)`
- **AND** `innums` = 第一个输入 buffer 中的 32-bit 字数（不含 buffer 长度）
- **AND** `inbufs` = 额外输入 buffer 数量
- **AND** `outnums` = 第一个输出 buffer 中的 32-bit 字数
- **AND** `outbufs` = 额外输出 buffer 数量

#### Scenario: 编译时双重角色

- **WHEN** 编译接口定义文件
- **THEN** 定义 `HEXAGONRPC_BUILD_METHOD_DEFINITIONS=1` 时为 `extern const` 声明
- **AND** 在 `interfaces.c` 中定义宏后 `#include` `.def` 文件时生成实际定义

### Requirement: apps_std 方法覆盖

HexagonRPC SHALL 实现全部 37 个 apps_std 方法，且所有文件 I/O 通过 HexagonFS 路由。

#### Scenario: 方法完整性

- **WHEN** 系统编译完成
- **THEN** `apps_std_procs[]` 数组包含 37 个非 NULL 条目
- **AND** 每个条目对应 `apps_std.def` 中定义的一个方法

#### Scenario: 文件操作全走 HexagonFS

- **WHEN** DSP 调用任意 apps_std 文件操作方法（打开/读取/写入/关闭/seek/stat）
- **THEN** 实现函数通过 `hexagonfs_*()` API 操作
- **AND** 不直接调用 `fopen()`/`open()`/`fread()`/`read()` 等 libc 文件函数

### Requirement: apps_mem 方法覆盖

HexagonRPC SHALL 实现全部 8 个 apps_mem 方法，通过 Linux FASTRPC_IOCTL 与内核交互。

#### Scenario: 方法完整性

- **WHEN** 系统编译完成
- **THEN** `apps_mem_procs[]` 数组包含 8 个非 NULL 条目
- **AND** 每个条目对应 `apps_mem.def` 中定义的一个方法

### Requirement: 写入支持

HexagonRPC SHALL 默认启用写入支持，无需额外配置。

#### Scenario: 默认读写打开

- **WHEN** DSP 调用 `fopen(name, "w")` 或 `fopen_with_env(ADSP_LIBRARY_PATH, ...)`
- **THEN** HexagonFS 以 `O_RDWR` 尝试打开物理文件
- **AND** 仅当文件系统拒绝写入时（EACCES/EROFS）回退到 `O_RDONLY`
- **AND** 不依赖 JSON 配置或环境变量来启用写入

#### Scenario: 写操作

- **WHEN** DSP 调用 `fwrite`/`fremove`/`mkdir`/`rmdir`/`ftrunc`/`frename` 等写方法
- **THEN** 实现通过 HexagonFS 的 `write`/`unlink`/`mkdir`/`rmdir`/`truncate` 函数指针执行
- **AND** 映射到对应的 POSIX 系统调用

### Requirement: 公共 remotectl

HexagonRPC SHALL 在 `libhexagonrpc` 中提供 `remotectl_open()`/`remotectl_close()` 公共实现，供 `hexagonrpcd` 和 `chrecd` 共用。

#### Scenario: 消除重复代码

- **WHEN** `rpcd.c` 或 `chrecd/main.c` 调用 remotectl_open/close
- **THEN** 调用 `libhexagonrpc/remotectl.c` 中的共享实现
- **AND** 不包含本地的重复实现

### Requirement: 测试套件

HexagonRPC SHALL 包含三层测试：

1. 单元测试：`iobuffer`（缓冲区编码/解码）
2. HexagonFS 测试：`hexagonfs`（路径解析、文件操作）
3. DSP 模拟测试：`dsp-simulation`（完整 37 项 DSP 行为模拟）

#### Scenario: 测试完整性

- **WHEN** 执行 `meson test -C build`
- **THEN** 全部三层测试通过
- **AND** dsp-simulation 测试覆盖：skel 加载、ACDB 访问、传感器配置、SoC 信息、seek/tell/stat、目录操作、错误路径
