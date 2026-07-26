# Deployment — 部署基础设施基线

> 本文档描述 HexagonRPC 的安装路径、systemd 集成、udev 规则和 man page 部署。

---

## Requirements

### Requirement: 安装路径

`meson install` SHALL 按标准 Linux FHS 安装产物。

#### Scenario: 安装路径映射

- **WHEN** 执行 `ninja -C build install`（含 `$DESTDIR`）
- **THEN** 安装路径如下：

| 产物 | 安装路径 |
|------|----------|
| `libhexagonrpc.so` | `{libdir}/` |
| `hexagonrpcd` | `{bindir}/` |
| 公共头文件 | `{includedir}/libhexagonrpc/` |
| udev 规则 | `{prefix}/lib/udev/rules.d/` |
| sysusers.d 配置 | `{prefix}/lib/sysusers.d/` |
| systemd service 单元 | `{prefix}/lib/systemd/system/` |
| man page (hexagonrpcd.8) | `{mandir}/man8/` |

- **AND** `$DESTDIR` 前缀在所有安装路径中正确生效（支持打包流程如 makepkg）

### Requirement: systemd service 单元

HexagonRPC SHALL 提供 5 个 systemd service 单元。

#### Scenario: service 列表

- **WHEN** 安装完成
- **THEN** 以下 service 单元可用：

| Service 名称 | DSP / PD |
|-------------|----------|
| `hexagonrpcd-adsp-rootpd.service` | ADSP root PD |
| `hexagonrpcd-adsp-audiopd.service` | ADSP audio PD |
| `hexagonrpcd-adsp-sensorspd.service` | ADSP sensors PD |
| `hexagonrpcd-cdsp.service` | CDSP |
| `hexagonrpcd-sdsp.service` | SDSP |

#### Scenario: service 模板机制

- **WHEN** service 文件被 Meson 处理
- **THEN** `.service.in` 中的 `@BINDIR@` 占位符替换为实际 bindir 路径
- **AND** 各 service 的 `ExecStart` 指向正确的 `hexagonrpcd -f /dev/fastrpc-{dsp}` 及对应参数

### Requirement: udev 规则

HexagonRPC SHALL 通过 udev 规则自动设置 FastRPC 设备节点权限。

#### Scenario: udev 规则内容

- **WHEN** udev 规则文件 `60-hexagonrpc.rules` 安装到 `/lib/udev/rules.d/`
- **THEN** 匹配 `/dev/fastrpc-*` 设备节点
- **AND** 设置所属组和管理权限

### Requirement: sysusers.d 配置

HexagonRPC SHALL 通过 sysusers.d 创建专用的 `hexagonrpc` 系统用户。

#### Scenario: 用户创建

- **WHEN** systemd-sysusers 处理 `hexagonrpc.conf`
- **THEN** 创建 `hexagonrpc` 系统用户（如果不存在）
- **AND** 该用户用于运行 hexagonrpcd 守护进程

### Requirement: man page

HexagonRPC SHALL 提供 man page 并为其创建符号链接。

#### Scenario: man page 安装

- **WHEN** 安装 man page
- **THEN** `hexagonrpcd.8` 安装到 `{mandir}/man8/`
- **AND** 通过 `install-man-symlinks.sh` 为 5 个 domain 创建符号链接：
  - `hexagonrpcd-adsp-rootpd.8`
  - `hexagonrpcd-adsp-audiopd.8`
  - `hexagonrpcd-adsp-sensorspd.8`
  - `hexagonrpcd-cdsp.8`
  - `hexagonrpcd-sdsp.8`

#### Scenario: DESTDIR 兼容

- **WHEN** 在打包环境中运行（`$DESTDIR` 已设置）
- **THEN** `install-man-symlinks.sh` 通过 `$DESTDIR` 前缀正确创建符号链接
- **AND** 目标 man page 路径使用 `$DESTDIR` 前缀
