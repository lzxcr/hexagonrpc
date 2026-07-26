# Architecture — 系统架构基线

> 本文档描述 HexagonRPC 系统的组件组成、依赖关系与核心数据流。

---

## Requirements

### Requirement: 组件组成

HexagonRPC SHALL 由以下四个主要组件组成：

- **libhexagonrpc**：共享库 (`libhexagonrpc.so`)，提供 FastRPC ioctl 包装 API
- **hexagonrpcd**：守护进程，建立反向隧道并服务 DSP 发来的 RPC 请求
- **HexagonFS**：编译进 hexagonrpcd 的虚拟文件系统，为 DSP 提供 Android 路径 → Linux 路径重定向
- **接口定义 (.def)**：手写方法签名文件，在编译时生成 `extern const` 声明

#### Scenario: 组件依赖关系

- **WHEN** 系统构建完成
- **THEN** `hexagonrpcd` 链接 `libhexagonrpc.so`
- **AND** HexagonFS 实现代码 (`hexagonfs.c`/`hexagonfs_mapped.c`/`hexagonfs_virt_dir.c`/`rpcd_builder.c`) 编译进 `hexagonrpcd`
- **AND** `.def` 文件通过 `#include` 机制分别由 `libhexagonrpc` 和 `hexagonrpcd` 编译

### Requirement: 运行时拓扑

HexagonRPC SHALL 运行在以下拓扑中：

```
应用处理器 (AP)                           Qualcomm DSP
┌──────────────────────────┐              ┌──────────┐
│  hexagonrpcd             │   ioctl      │          │
│  ┌────────────────────┐  │ ◄══════════► │  fastrpc  │
│  │ reverse tunnel     │  │  FASTRPC_    │  driver   │
│  │ (listener.c)       │  │  IOCTL_*     │  (rpmsg)  │
│  │  ↕                 │  │              │          │
│  │ interface dispatch │  │              └──────────┘
│  │  ├─ remotectl(0)   │  │
│  │  ├─ apps_std(1)    │──┤──► Linux VFS / DMA-BUF
│  │  └─ apps_mem(2)    │  │
│  │       ↕             │  │
│  │   HexagonFS         │  │
│  └────────────────────┘  │
│            │              │
│     libhexagonrpc.so      │
│     (ioctl wrappers)      │
└──────────────────────────┘
```

#### Scenario: 数据流方向

- **WHEN** DSP 发起 RPC 调用
- **THEN** 请求通过 rpmsg 到达内核 fastrpc 驱动
- **AND** hexagonrpcd 通过 `adsp_listener_next2()` 轮询取出
- **AND** 按 handle + method ID 分派到对应实现函数
- **AND** 结果通过 `outbufs_encode()` 编码后经 `adsp_listener_next2` 返回 DSP

### Requirement: 接口体系

HexagonRPC SHALL 使用 handle 索引的接口表进行方法分派。

#### Scenario: 接口注册与查找

- **WHEN** `start_reverse_tunnel()` 初始化
- **THEN** 以下三个接口注册到 `ifaces[]` 数组：

| Handle | 接口 | 提供方 | 方法来源 |
|--------|------|--------|----------|
| 0 | `remotectl` | `localctl.c` | `remotectl.def` (2 methods) |
| 1 | `apps_std` | `apps_std.c` | `apps_std.def` (37 methods) |
| 2 | `apps_mem` | `apps_mem.c` | `apps_mem.def` (8 methods) |
| 3 | `adsp_listener` | `listener.c` | `adsp_listener.def` (2 methods) |

- **AND** handle=0 的 `remotectl_open` 用于按名称查找其他接口（返回数组索引作为 handle）

### Requirement: 层间隔离

HexagonRPC SHALL 保持组件间的清晰边界。

#### Scenario: 文件访问隔离

- **WHEN** `apps_std` 需要操作文件
- **THEN** 实现函数 SHALL 通过 `hexagonfs_*()` API 操作
- **AND** SHALL NOT 直接调用 libc 的 `fopen()` / `open()` / `fread()` / `read()` 等文件函数

#### Scenario: 内存访问隔离

- **WHEN** `apps_mem` 需要分配/映射内存
- **THEN** 实现函数 SHALL 通过 `FASTRPC_IOCTL_MMAP` / `FASTRPC_IOCTL_MUNMAP` / `FASTRPC_IOCTL_MEM_MAP` / `FASTRPC_IOCTL_MEM_UNMAP` ioctl
- **AND** SHALL 使用 `rpcmem_linux.c` 的 DMA-BUF 堆分配器进行用户态分配
