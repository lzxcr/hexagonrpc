# Daemon — hexagonrpcd 守护进程基线

> 本文档描述 `hexagonrpcd` 守护进程的启动流程、CLI 参数、PD 模式和反向隧道行为。

---

## Requirements

### Requirement: CLI 参数

hexagonrpcd SHALL 支持以下命令行参数。

#### Scenario: 参数列表

- **WHEN** hexagonrpcd 启动
- **THEN** 识别以下参数：

| 参数 | 必需 | 说明 |
|------|------|------|
| `-f DEVICE` | 是 | FastRPC 设备节点路径，如 `/dev/fastrpc-adsp` |
| `-R DIR` | 否 | HexagonFS 根目录，默认 `/usr/share/qcom/` |
| `-d DSP` | 否 | DSP 名称，默认空字符串 |
| `-s` | 否 | sensorspd 模式（使用 `INIT_ATTACH_SNS`） |
| `-c SHELL` | 否 | 创建自定义 PD 并加载指定 ELF |
| `-p PROGRAM` | 否 | fork + exec 子客户端，共享 FD |

### Requirement: 设备目录自动检测

hexagonrpcd SHALL 在未指定 `-R` 时自动检测设备目录。

#### Scenario: device-tree 读取

- **WHEN** 用户未指定 `-R` 参数
- **THEN** 守护进程读取 `/proc/device-tree/compatible` 确定 SoC 名称
- **AND** 读取 `/proc/device-tree/model` 确定厂商名称
- **AND** 从 `compatible` 的逗号后部分提取设备代号
- **AND** 依次尝试 `/usr/share/qcom/{soc}/{vendor}/{device}/` 路径
- **AND** 使用第一个存在的路径作为 HexagonFS 根目录

### Requirement: FastRPC 设备初始化

hexagonrpcd SHALL 打开 FastRPC 设备节点并执行适当的初始化 ioctl。

#### Scenario: 标准 INIT_ATTACH

- **WHEN** 用户执行 `hexagonrpcd -f /dev/fastrpc-adsp`（无 `-s` / `-c`）
- **THEN** 打开 `/dev/fastrpc-adsp`（O_RDWR）
- **AND** 执行 `FASTRPC_IOCTL_INIT_ATTACH` ioctl
- **AND** 设置 `HEXAGONRPC_FD` 环境变量为打开的 FD

#### Scenario: sensorspd INIT_ATTACH_SNS

- **WHEN** 用户执行 `hexagonrpcd -f DEVICE -s`
- **THEN** 执行 `FASTRPC_IOCTL_INIT_ATTACH_SNS` 而非 `INIT_ATTACH`

#### Scenario: 自定义 PD INIT_CREATE

- **WHEN** 用户执行 `hexagonrpcd -f DEVICE -c /path/to/shell.elf`
- **THEN** 通过 `FASTRPC_IOCTL_ALLOC_DMA_BUFF` 分配 DMA buffer
- **AND** 将 ELF 文件内容读入 DMA buffer
- **AND** 执行 `FASTRPC_IOCTL_INIT_CREATE` 创建新 PD

### Requirement: 子客户端管理

hexagonrpcd SHALL 通过 `HEXAGONRPC_FD` 环境变量与 fork 的子客户端共享 FastRPC FD。

#### Scenario: 子客户端启动

- **WHEN** 用户执行 `hexagonrpcd -f DEVICE -p /path/to/client`
- **THEN** hexagonrpcd fork 子进程
- **AND** 子进程通过 `HEXAGONRPC_FD` 环境变量获取已打开的 FD
- **AND** 子进程 exec 客户端程序

#### Scenario: 子客户端终止

- **WHEN** hexagonrpcd 即将退出
- **THEN** 向所有子客户端发送 SIGTERM
- **AND** 等待子进程退出

### Requirement: 反向隧道启动

`start_reverse_tunnel()` SHALL 初始化所有本地接口并进入监听循环。

#### Scenario: 接口初始化

- **WHEN** `start_reverse_tunnel()` 被调用
- **THEN** 加载 HexagonFS 配置（hexagonrpc.json）
- **AND** 构建虚拟目录树
- **AND** 初始化 remotectl 接口（`fastrpc_localctl_init()`）
- **AND** 初始化 apps_std 接口（`fastrpc_apps_std_init()`）
- **AND** 初始化 apps_mem 接口（`fastrpc_apps_mem_init()`）
- **AND** 注册 adsp_default_listener
- **AND** 调用 `run_fastrpc_listener()`（阻塞）

### Requirement: 反向隧道事件循环

`run_fastrpc_listener()` SHALL 实现 DSP RPC 请求的接收-分派-返回循环。

#### Scenario: 主循环

- **WHEN** `run_fastrpc_listener()` 运行
- **THEN** 首先调用 `adsp_listener_init2()` 初始化 DSP 端监听器
- **AND** 进入无限循环：
  1. `adsp_listener_next2()` — 等待/接收 DSP 请求
  2. `inbuf_decode()` — 解码输入参数
  3. `invoke_requested_procedure()` — 按 handle+method 分派
  4. `outbufs_encode()` — 编码返回值
  5. 将结果传回 `adsp_listener_next2()` 的 outbufs 参数

#### Scenario: 方法分派

- **WHEN** `invoke_requested_procedure()` 被调用
- **THEN** 按 handle 在 `ifaces[]` 数组中查找接口
- **AND** 按 method ID 在接口的 `procs[]` 中查找实现函数
- **AND** 验证输入/输出 buffer 数量与 .def 定义一致
- **AND** 调用 `impl->impl(data, inbufs, outbufs)`
- **AND** handle 未找到时返回 error=-5
- **AND** method 未找到时返回 error=-4

### Requirement: I/O Buffer 编码

iobuffer 模块 SHALL 提供 FastRPC wire format 的编解码。

#### Scenario: 输入解码

- **WHEN** `inbuf_decode()` 被调用
- **THEN** 从第一个输入 buffer 的 scalars 区域提取 method ID 和参数
- **AND** 后续 buffer 作为不透明数据传入

#### Scenario: 输出编码

- **WHEN** `outbufs_encode()` 被调用
- **THEN** 将返回值和输出数据写入 outbufs 数组
- **AND** 第一个输出 buffer 的 scalars 区域包含返回值
