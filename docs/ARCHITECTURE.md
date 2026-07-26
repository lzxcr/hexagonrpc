# HexagonRPC 架构总览

## 项目概述

HexagonRPC 是 [Sensor Shell](https://gitlab.com/sensh) 项目对 Qualcomm FastRPC 框架的**独立重新实现**，使用 GPLv3 许可。

与官方 FastRPC 不同，HexagonRPC：
- **不需要 QAIC IDL 编译器** — 手写 `.def` 文件定义方法签名
- **不需要 Qualcomm 闭源库** — 纯开源 C 实现
- **使用 Meson 构建** — 而非 autotools
- **聚焦于反向隧道** — AP 接收并处理来自 DSP 的 RPC 调用

## 组件

```
┌────────────────────────────────────────────────────┐
│                    应用处理器 (AP)                    │
│                                                    │
│  ┌──────────────┐  ┌──────────────────────────────┐ │
│  │              │  │      反向隧道 (listener)       │ │
│  │ fastrpc2()───┼──┼──→ adsp_listener_next2()    │ │
│  │              │  │        ↕                     │ │
│  └──────────────┘  │   接口调度                    │ │
│                    │  ┌───────┬──────┬──────┐     │ │
│                    │  │localctl│apps  │apps  │     │ │
│                    │  │(remo- │ _std │ _mem │     │ │
│                    │  │tectl) │(37)  │(8)   │     │ │
│                    │  └───────┴──┬───┴──────┘     │ │
│                    │            │                 │ │
│                    │       HexagonFS (读写)        │ │
│                    └──────────────────────────────┘ │
│                                   │                 │
│                        libhexagonrpc.so              │
│                    (ioctl 包装器 + 接口定义)          │
│                                   │                 │
└───────────────────────────────────┼─────────────────┘
                                    │ ioctl(FASTRPC_IOCTL_*)
                          ┌─────────┴─────────┐
                          │  Linux 内核        │
                          │  fastrpc 驱动      │
                          └─────────┬─────────┘
                                    │ rpmsg
                          ┌─────────┴─────────┐
                          │  Hexagon DSP      │
                          │  (ADSP/SDSP/CDSP) │
                          └───────────────────┘
```

### libhexagonrpc — 共享库

提供两个 API 层次：

| API | 函数 | 适用场景 |
|-----|------|----------|
| 底层 | `fastrpc2(fd, handle, ...)` | 一次性调用 |
| 高层 | `fastrpc(ctx, ...)` | 可复用的 context 对象 |

方法签名通过 `HEXAGONRPC_DEFINE_REMOTE_METHOD` 宏在 `.def` 文件中手写定义，无需 IDL 编译器。

### hexagonrpcd — 反向隧道守护进程

核心流程：

```
main()
  ├─ guess_device_directory_from_compatible()   // 从 device-tree 猜测设备路径
  ├─ open(/dev/fastrpc-*)                       // 打开设备节点
  ├─ ioctl(FASTRPC_IOCTL_INIT_ATTACH)           // 附加到 DSP
  ├─ setup_environment(fd)                      // 设置 HEXAGONRPC_FD
  ├─ start_clients()                            // fork 子客户端（可选）
  └─ start_reverse_tunnel()                     // 主循环（阻塞）
       ├─ construct_root_dir()                  // 构建虚拟文件系统
       ├─ fastrpc_localctl_init()               // remotectl (handle=0)
       ├─ fastrpc_apps_std_init()               // apps_std (handle=1)
       ├─ fastrpc_apps_mem_init()               // apps_mem (handle=2)
       ├─ register_fastrpc_listener()           // 注册 listener
       └─ run_fastrpc_listener()                // 事件循环
```

### 反向隧道事件循环

```
while true:
    adsp_listener_next2()         // 等待 DSP 请求（同时返回上次结果）
    inbuf_decode()                // 解码输入参数
    invoke_requested_procedure()  // 按 handle + method 分派
    outbufs_encode()              // 编码返回值
```

### 接口分派表

| Handle | 接口 | 提供方 | 方法数 |
|--------|------|--------|--------|
| 0 | `remotectl` | `localctl.c` | 2 (open/close) |
| 1 | `apps_std` | `apps_std.c` | 37 (文件 I/O) |
| 2 | `apps_mem` | `apps_mem.c` | 8 (内存映射) |
| 3 | `adsp_listener` | `listener.c` | 2 (init2/next2) |

### HexagonFS — 虚拟文件系统

为 DSP 提供读写虚拟文件系统，将 Android 路径透明映射到 Linux 宿主机物理路径。

**目录布局**：
```
/                                → {root}/
├── acdb/                        → {root}/acdb/
├── dsp/{dsp}/                   → {root}/dsp/{dsp}/
├── sensors/
│   ├── config/                  → {root}/sensors/config/
│   ├── registry/                → {root}/sensors/registry/
│   └── sns_reg.conf             → {root}/sensors/sns_reg.conf
├── socinfo/                     → {root}/socinfo/
├── vendor/etc/acdbdata/         → /acdb/ (别名)
├── vendor/etc/sensors/          → /sensors/ (别名)
├── persist/sensors/registry/    → /sensors/registry/ (别名)
└── sys/devices/soc0/            → /socinfo/ (别名)
```

**后端类型**：

| 后端 | 用途 | 读写 |
|------|------|------|
| `mapped_ops` | 映射到真实物理文件的普通文件 | 读写 |
| `mapped_or_empty_ops` | 可选文件（不存在则空） | 只读 |
| `mapped_sysfs_ops` | sysfs 文件（如 socinfo） | 只读 |
| `virt_dir_ops` | 虚拟目录（内存中 dirent 列表） | 目录操作 |
| `plat_subtype_name_ops` | 动态生成的平台子类型名 | 只读 |

## 与官方 FastRPC 差异

| 方面 | 官方 FastRPC | HexagonRPC |
|------|-------------|------------|
| 许可证 | BSD 3-Clause | GPLv3 |
| 构建系统 | autotools | Meson |
| IDL 编译器 | QAIC（闭源） | 不需要（手写 .def） |
| 平台支持 | Linux, Android, Windows | Linux |
| 方法覆盖 | apps_std 37, apps_mem 8 | apps_std 37, apps_mem 8 |
| 写入支持 | 是 | 是 |
| 多域支持 | 是 | 否（未来计划） |
| 性能跟踪 | FASTRPC_ATRACE | 否 |
| 自动重启 | 是 | 否 |
