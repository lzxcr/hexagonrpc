## Why

HexagonRPC 目前缺少完整的部署基础设施：udev 规则、sysusers 配置、多 domain service 文件、man page 符号链接。与 FastRPC 对比，需要补齐这些以保证功能等价和易用性。

## What Changes

### 1. udev 规则 + sysusers.d

创建 `data/60-hexagonrpc.rules` 和 `data/hexagonrpc.conf`：
- udev：`/dev/fastrpc-*` owner=root, group=hexagonrpc, mode=0660
- udev：设备就绪时启动对应的 systemd service
- sysusers.d：创建 `hexagonrpc` 系统组

### 2. 补齐所有 domain service 文件

参照 FastRPC 的 7 个 service，为 HexagonRPC 创建：

| service 文件 | 参数 | 设备节点 |
|-------------|------|---------|
| hexagonrpcd-adsp-rootpd.service | -f /dev/fastrpc-adsp | ✅ 已有 |
| hexagonrpcd-adsp-audiopd.service | -f /dev/fastrpc-adsp -c audiopd | 新增 |
| hexagonrpcd-adsp-sensorspd.service | -f /dev/fastrpc-adsp -s | ✅ 已有 |
| hexagonrpcd-cdsp.service | -f /dev/fastrpc-cdsp | 新增 |
| hexagonrpcd-gdsp.service | -f /dev/fastrpc-gdsp -d gdsp | 新增 |
| hexagonrpcd-sdsp.service | -f /dev/fastrpc-sdsp -d sdsp | ✅ 已有 |

### 3. man page 完善

- 将 man page 从 `man1` 改为 `man8`（系统守护进程标准位置）
- 为每个 domain 创建符号链接（hexagonrpcd-adsp.8 → hexagonrpcd.8 等）

### 4. 更新 data/meson.build 安装规则

安装上述所有新文件。

### 5. 更新相关文档
