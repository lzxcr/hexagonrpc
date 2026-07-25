# HexagonRPC v0.5.0

FastRPC ioctl 包装库 + 反向隧道守护进程 + HexagonFS 虚拟文件系统。

用于与 Qualcomm DSP(ADSP/SDSP/CDSP)通信,为 CHRE 等 DSP 程序提供文件服务,
支持主线 Linux 上的 Android 固件路径透明重定向。

## 架构

```
┌─ libhexagonrpc ── 共享库 (fastrpc/fastrpc2/remotectl 包装)
├─ hexagonrpcd    ── 守护进程 (反向隧道 + HexagonFS)
├─ chrecd         ── CHRE 客户端
└─ HexagonFS      ── 虚拟目录树: Android 路径 → Linux 物理路径
```

## 构建

```bash
meson setup build
ninja -C build
ninja -C build install  # 安装到 /usr/local/
```

## 运行

```bash
hexagonrpcd -f /dev/fastrpc-adsp -R /usr/share/qcom/sdm845/SHIFT/axolotl
```

参数:
- `-f DEVICE` — FastRPC 设备节点 (必需)
- `-R DIR` — HexagonFS 根目录 (默认 /usr/share/qcom/)
- `-d DSP` — DSP 名称 (默认 adsp)
- `-s` — sensors PD 模式
- `-c SHELL` — 创建自定义 PD 加载 ELF
- `-p PROGRAM` — 启动子客户端 (共享 FD)

## 方法覆盖

| 接口 | 方法 | 覆盖率 |
|------|------|--------|
| apps_std | fopen/close/read/write/seek/stat/opendir 等 | **37/37** ✅ |
| apps_mem | mmap/munmap/share_map/dma_handle 等 | **8/8** ✅ |
| remotectl | open/close | **2/2** ✅ |
| adsp_listener | init2/next2 | **2/2** ✅ |
| chre_slpi | start_thread/wait_on_thread_exit | **2/2** ✅ |

详细文档见 `docs/`。

## HexagonFS 路径映射

| DSP 虚拟路径 | 物理路径 |
|-------------|---------|
| `/vendor/etc/acdbdata/` | `{root}/acdb/` |
| `/vendor/dsp/{dsp}/` | `{root}/dsp/{dsp}/` |
| `/vendor/etc/sensors/config/` | `{root}/sensors/config/` |
| `/persist/sensors/registry/` | `{root}/sensors/registry/` |
| `/vendor/etc/sensors/sns_reg_config` | `{root}/sensors/sns_reg.conf` |
| `/sys/devices/soc0/` | `{root}/socinfo/` |
| `/usr/lib/qcom/{dsp}/` | `{root}/dsp/{dsp}/` |

支持硬链接: `/vendor/` ↔ `/system/vendor/`, `/persist/` ↔ `/mnt/vendor/persist/`

## 测试

```bash
meson test -C build
# 输出: iobuffer OK, hexagonfs OK, dsp-simulation OK
```

## 许可

GNU General Public License v3.0
