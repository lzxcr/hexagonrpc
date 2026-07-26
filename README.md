# HexagonRPC v0.5.0

FastRPC ioctl 包装库 + 反向隧道守护进程 + HexagonFS 虚拟文件系统。

用于与 Qualcomm DSP（ADSP/SDSP/CDSP）通信，为 DSP 程序在主线 Linux 上提供完整的文件 I/O 和内存映射服务，支持 Android 固件路径透明重定向。

## 架构

```
┌─ libhexagonrpc ── 共享库 (fastrpc2 / fastrpc / remotectl 包装)
├─ hexagonrpcd    ── 守护进程 (反向隧道 + 接口调度)
└─ HexagonFS      ── 虚拟目录树: Android 路径 → Linux 物理路径 (读写)
```

## 构建

```bash
meson setup build
ninja -C build
ninja -C build install   # 可选, 安装到 /usr/local/
```

启用详细日志：`meson setup build -Dhexagonrpcd_verbose=true`

## 运行

```bash
hexagonrpcd -f /dev/fastrpc-adsp -R /usr/share/qcom/sdm845/SHIFT/axolotl
```

| 参数 | 说明 |
|------|------|
| `-f DEVICE` | FastRPC 设备节点 (必需) |
| `-R DIR` | HexagonFS 根目录 (默认 /usr/share/qcom/) |
| `-d DSP` | DSP 名称 (默认空) |
| `-s` | sensorspd 模式 |
| `-c SHELL` | 创建自定义 PD 加载 ELF |
| `-p PROGRAM` | 启动子客户端 (共享 FD) |

## 接口覆盖

| 接口 | 方法数 | 覆盖率 |
|------|--------|--------|
| apps_std | 37 | **37/37** ✅ |
| apps_mem | 8 | **8/8** ✅ |
| remotectl | 2 | **2/2** ✅ |
| adsp_listener | 2 | **2/2** ✅ |

## HexagonFS 路径映射

| DSP 虚拟路径 | 物理路径 |
|-------------|---------|
| `/vendor/etc/acdbdata/` | `{root}/acdb/` |
| `/vendor/dsp/{dsp}/` | `{root}/dsp/{dsp}/` |
| `/vendor/etc/sensors/config/` | `{root}/sensors/config/` |
| `/persist/sensors/registry/` | `{root}/sensors/registry/` |
| `/sys/devices/soc0/` | `{root}/socinfo/` |

## 测试

```bash
meson test -C build
# 输出: iobuffer OK, hexagonfs OK, dsp-simulation OK
```

## 文档

- [快速入门](docs/QUICKSTART.md)
- [架构总览](docs/ARCHITECTURE.md)
- [API 参考](docs/API.md)
- [配置指南](docs/CONFIGURATION.md)
- [系统规格](openspec/specs/) — 基线行为规格 (Requirements + Scenarios)

## 许可

GNU General Public License v3.0 (见 [`COPYING`](COPYING))
