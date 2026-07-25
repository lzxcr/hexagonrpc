# HexagonRPC 快速入门指南

## 前置条件

- Linux 内核 >= 5.4（需要 FastRPC ioctl 支持）
- Qualcomm SoC 设备（SDM845、SM8150 等），或已加载 `fastrpc` 内核模块
- Meson >= 1.1 + Ninja
- GCC 或 Clang

## 编译

### 本地编译

```bash
cd hexagonrpc
meson setup build
ninja -C build
```

产物：
| 文件 | 路径 | 说明 |
|------|------|------|
| `libhexagonrpc.so` | `build/libhexagonrpc/` | FastRPC ioctl 包装库 |
| `hexagonrpcd` | `build/hexagonrpcd/` | 反向隧道守护进程 |

### 编译选项

```bash
# 启用详细日志（打印每个 RPC 调用）
meson setup build -Dhexagonrpcd_verbose=true
```

### 安装

```bash
ninja -C build install
# 默认安装到 /usr/local/
```

## 运行

### 1. 确认 FastRPC 设备节点存在

```bash
ls -la /dev/fastrpc-*
# 典型输出：/dev/fastrpc-adsp, /dev/fastrpc-sdsp, /dev/fastrpc-cdsp
```

### 2. 启动 hexagonrpcd

```bash
# 连接到 ADSP（音频/传感器 DSP）
hexagonrpcd -f /dev/fastrpc-adsp

# 连接到 SDSP（传感器 DSP）
hexagonrpcd -f /dev/fastrpc-sdsp

# 以 sensorspd 模式连接（仅传感器）
hexagonrpcd -f /dev/fastrpc-adsp -s

# 指定 HexagonFS 文件根目录
hexagonrpcd -f /dev/fastrpc-adsp -R /usr/share/qcom/sdm845/OnePlus/lemonade
```

### 3. 启动客户端程序

```bash
# hexagonrpcd 会设置 HEXAGONRPC_FD 环境变量
# 客户端程序通过该变量获取共享的文件描述符
```


```bash
```

## 命令行参数参考

| 参数 | 说明 | 示例 |
|------|------|------|
| `-f DEVICE` | **必需**。FastRPC 设备节点路径 | `-f /dev/fastrpc-adsp` |
| `-d DSP` | DSP 名称，用于 HexagonFS 子目录 | `-d adsp` |
| `-R DIR` | HexagonFS 根目录（默认 `/usr/share/qcom/`） | `-R /usr/share/qcom/sdm845` |
| `-s` | 以 `INIT_ATTACH_SNS` 模式连接（sensorspd） | `-s` |
| `-c SHELL` | 创建新 PD 并加载指定 ELF | `-c /path/to/shell.elf` |
| `-p PROGRAM` | 启动子客户端程序（共享 FD） | `-p /usr/bin/sensor-app` |

## HexagonFS 目录准备

hexagonrpcd 为 DSP 提供一个虚拟只读文件系统。默认从 `/usr/share/qcom/` 提供文件。

### 预期目录结构

```
/usr/share/qcom/{soc}/{vendor}/{device}/
├── acdb/           → 映射到 /vendor/etc/acdbdata/
├── dsp/{dsp}/      → 映射到 /vendor/dsp/{dsp}/
├── sensors/
│   ├── config/     → 映射到 /vendor/etc/sensors/config/
│   ├── registry/   → 映射到 /mnt/vendor/persist/sensors/registry/
│   └── sns_reg.conf → 映射到 /vendor/etc/sensors/sns_reg_config
└── socinfo/        → 映射到 /sys/devices/soc0/
```

### 自动检测设备目录

hexagonrpcd 会读取 `/proc/device-tree/compatible` 和 `/proc/device-tree/model` 自动猜测设备目录：

- SoC 名称：从 `compatible` 中提取 `qcom,` 前缀的条目
- 厂商名称：从 `model` 中提取第一个空格之前的部分
- 设备代号：从 `compatible` 中提取逗号后的部分

例如设备 `/usr/share/qcom/sdm845/SHIFT/axolotl` 会被自动检测。

## 编程接口

### 使用 libhexagonrpc

```c
#include <libhexagonrpc/fastrpc.h>
#include <libhexagonrpc/interfaces/remotectl.def>

// 1. 打开设备
int fd = open("/dev/fastrpc-adsp", O_RDWR);

// 2. 调用远程方法
uint32_t handle, dlret;
char err[256];
int ret = fastrpc2(&remotectl_open_def, fd, REMOTECTL_HANDLE,
                   strlen("apps_std") + 1, "apps_std",
                   &handle, &dlret, 256, err);

// 3. 或使用 context 接口
struct fastrpc_context *ctx = fastrpc_create_context(fd, handle);
ret = fastrpc(&some_method_def, ctx, arg1, arg2, &result);
fastrpc_destroy_context(ctx);
```

### 从环境变量获取 FD

```c
#include <libhexagonrpc/session.h>

int fd = hexagonrpc_fd_from_env();
if (fd == -1) {
    fprintf(stderr, "HEXAGONRPC_FD not set\n");
    return 1;
}
```

## 调试

### 启用详细日志

编译时加 `-Dhexagonrpcd_verbose=true` 会打印每个远程方法调用：

```
openat($ADSP_LIBRARY_PATH, libcalculator_skel.so, r) -> 3
read(3, 4096) -> 1024
close(3)
```

### 运行测试

```bash
meson setup build
ninja -C build
meson test -C build

# 使用 valgrind
meson test -C build --setup=valgrind
```

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| `Could not open FastRPC node` | 设备节点不存在或权限不足 | 检查 `/dev/fastrpc-*`，确保用户在 `fastrpc` 组 |
| `Could not attach to FastRPC node` | 内核模块未加载或 DSP 未就绪 | `dmesg \| grep fastrpc` |
| `Could not find local interface` | DSP 请求的接口未注册 | 检查 hexagonrpcd 输出 |
| `Unsupported method` | DSP 调用了未实现的方法 | 参考差异矩阵，确认缺失方法 |

## systemd 集成

项目提供了 systemd service 模板（`data/` 目录），安装后可通过 udev 自动启动。服务文件包括：

- `hexagonrpcd-adsp-rootpd.service` — ADSP root PD
- `hexagonrpcd-adsp-sensorspd.service` — ADSP sensors PD
- `hexagonrpcd-sdsp.service` — SDSP

## Android 集成

项目包含 `Android.bp` 文件，可集成到 AOSP 构建系统：

```makefile
# 在 Android 源码树中
mmma external/hexagonrpc
```

对应的 init rc 文件：
- `hexagonrpcd-adsp-rootpd.rc`
- `hexagonrpcd-adsp-sensorspd.rc`
- `hexagonrpcd-sdsp.rc`

## 许可

GNU General Public License v3.0（见 `COPYING` 文件）。
