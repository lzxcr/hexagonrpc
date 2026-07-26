# HexagonFS — 虚拟文件系统基线

> 本文档描述 HexagonFS 虚拟文件系统的目录布局、路径映射、文件操作 API 和 FD 管理机制。

---

## ADDED Requirements

### Requirement: 核心职责

HexagonFS SHALL 为 DSP 提供透明的虚拟文件系统视图，将 Android 风格路径映射到 Linux 宿主机物理路径。

#### Scenario: 透明重定向

- **WHEN** DSP 通过 apps_std 访问文件
- **THEN** 路径经 HexagonFS 虚拟目录树从 Android 路径重定向到 Linux 物理路径
- **AND** DSP 无需感知底层路径映射逻辑

### Requirement: 默认读写模式

HexagonFS SHALL 默认以读写模式打开物理文件。

#### Scenario: O_RDWR 优先

- **WHEN** apps_std 请求打开文件
- **THEN** 先以 `O_RDWR` 尝试打开物理文件
- **AND** 仅当文件系统拒绝写入（EACCES / EROFS）时回退到 `O_RDONLY`

### Requirement: 虚拟目录布局

`construct_root_dir_with_prefix()` SHALL 构建以下虚拟目录树。

#### Scenario: 目录结构

- **WHEN** HexagonFS 根目录被构造
- **THEN** 提供以下虚拟路径映射：

| 虚拟路径 | 物理路径 |
|----------|----------|
| `/acdb/` | `{root}/acdb/` |
| `/dsp/{dsp}/` | `{root}/dsp/{dsp}/` |
| `/sensors/config/` | `{root}/sensors/config/` |
| `/sensors/registry/registry` | `{root}/sensors/registry/` |
| `/sensors/sns_reg.conf` | `{root}/sensors/sns_reg.conf` |
| `/socinfo/` | `{root}/socinfo/` |

- **AND** 以下别名路径通过硬链接风格映射：

| 虚拟别名 | → 虚拟目标 |
|----------|-----------|
| `/vendor/etc/acdbdata/` | `/acdb/` |
| `/vendor/etc/sensors/config/` | `/sensors/config/` |
| `/vendor/etc/sensors/sns_reg_config` | `/sensors/sns_reg.conf` |
| `/vendor/dsp/{dsp}/` | `/dsp/{dsp}/` |
| `/system/vendor/` | `/vendor/` |
| `/persist/sensors/registry/` | `/sensors/registry/` |
| `/mnt/vendor/persist/` | `/persist/` |
| `/sys/devices/soc0/` | `/socinfo/` |
| `/usr/lib/qcom/{dsp}/` | `/dsp/{dsp}/` |

### Requirement: 配置文件驱动

HexagonFS SHALL 通过 `hexagonrpc.json` 配置文件支持自定义路径映射。

#### Scenario: JSON 配置加载

- **WHEN** `hexagonrpc_config_load()` 被调用
- **THEN** 解析 `{device_dir}/hexagonrpc.json`
- **AND** 文件格式为：

```json
{
  "root_path": "/usr/share/qcom/sdm845/SHIFT/axolotl",
  "mappings": [
    {"virtual_path": "/vendor/etc", "physical_path": "vendor/etc"},
    {"virtual_path": "/vendor/dsp", "physical_path": "dsp"}
  ]
}
```

- **AND** `root_path` 覆盖默认根目录
- **AND** `mappings` 定义额外的虚拟→物理映射
- **AND** 文件不存在时不报错（使用默认目录结构）

### Requirement: 文件操作集类型

HexagonFS SHALL 提供五种 `hexagonfs_file_ops` 后端实现。

#### Scenario: mapped_ops — 映射文件

- **WHEN** 访问映射到真实物理文件的虚拟路径
- **THEN** 使用 `hexagonfs_mapped_ops`
- **AND** 支持：openat、close、read、write、lseek、fstat、truncate

#### Scenario: mapped_or_empty_ops — 可选映射文件

- **WHEN** 访问可能存在也可能不存在的文件
- **THEN** 使用 `hexagonfs_mapped_or_empty_ops`
- **AND** 存在真实文件时行为同 mapped_ops
- **AND** 不存在时模拟空文件（read 返回 0）

#### Scenario: mapped_sysfs_ops — sysfs 映射

- **WHEN** 访问 `/sys/` 路径
- **THEN** 使用 `hexagonfs_mapped_sysfs_ops`
- **AND** 带特殊 socinfo 子路径处理

#### Scenario: virt_dir_ops — 虚拟目录

- **WHEN** 访问纯虚拟目录节点
- **THEN** 使用 `hexagonfs_virt_dir_ops`
- **AND** 支持：openat（遍历子 dirent）、close、readdir
- **AND** 不支持：read、write（目录不可读写）

#### Scenario: plat_subtype_name_ops — 平台子类型名

- **WHEN** 访问平台子类型名称文件
- **THEN** 使用 `hexagonfs_plat_subtype_name_ops`
- **AND** 动态生成 SoC 子类型信息

### Requirement: FD 管理

HexagonFS SHALL 管理最多 256 个同时打开的文件描述符。

#### Scenario: FD 分配

- **WHEN** `hexagonfs_openat()` 成功打开文件
- **THEN** 从 `fds[]` 数组中分配最早可用的 slot（编号 0-255）
- **AND** `HEXAGONFS_MAX_FD` 宏定义为 256

#### Scenario: FD 释放

- **WHEN** `hexagonfs_close(fileno)` 被调用
- **THEN** 调用对应 `ops->close(fd)` 关闭底层资源
- **AND** 将 `fds[fileno]` 置为 NULL
- **AND** 释放 `hexagonfs_fd` 结构体

### Requirement: 文件操作 API

HexagonFS SHALL 提供以下公共 API 函数。

#### Scenario: hexagonfs_open_root

- **WHEN** 调用 `hexagonfs_open_root(fds, root, &fd)`
- **THEN** 打开根目录并分配 FD
- **AND** 返回 0 成功，负 errno 失败

#### Scenario: hexagonfs_openat

- **WHEN** 调用 `hexagonfs_openat(fds, dirfd, path, &fd)`
- **THEN** 从 dirfd 指定的目录开始逐段解析 path
- **AND** 每段通过 `ops->from_dirent` 或 `ops->openat` 打开
- **AND** 新创建的中间节点 FD 在成功后链接到父节点
- **AND** 路径不存在时返回 -ENOENT
- **AND** 返回 0 成功，负 errno 失败

#### Scenario: hexagonfs_close

- **WHEN** 调用 `hexagonfs_close(fileno)`
- **THEN** 减少 fd 引用计数
- **AND** refcount 归零时释放 fd 及其独享的祖先节点
- **AND** 返回 0 成功

#### Scenario: hexagonfs_read

- **WHEN** 调用 `hexagonfs_read(fileno, buf, size)`
- **THEN** 调用 `fd->ops->read(fd, size, buf)`
- **AND** 返回实际读取字节数或负 errno

#### Scenario: hexagonfs_write

- **WHEN** 调用 `hexagonfs_write(fileno, size, data)`
- **THEN** 调用 `fd->ops->write(fd, size, data)`
- **AND** 返回实际写入字节数或负 errno

#### Scenario: hexagonfs_lseek

- **WHEN** 调用 `hexagonfs_lseek(fileno, offset, whence)`
- **THEN** 调用 `fd->ops->lseek(fd, offset, whence)`
- **AND** 返回新位置或负 errno

#### Scenario: hexagonfs_fstat

- **WHEN** 调用 `hexagonfs_fstat(fileno, &st)`
- **THEN** 调用 `fd->ops->fstat(fd, &st)`
- **AND** 返回 0 成功或负 errno

#### Scenario: hexagonfs_readdir

- **WHEN** 调用 `hexagonfs_readdir(fileno, &dirent)`
- **THEN** 调用 `fd->ops->readdir(fd, &dirent)`
- **AND** 返回条目名称和类型，目录末尾返回 0

#### Scenario: hexagonfs_ftruncate

- **WHEN** 调用 `hexagonfs_ftruncate(fileno, length)`
- **THEN** 调用 `fd->ops->truncate(fd, length)`
- **AND** 返回 0 成功或负 errno

#### Scenario: hexagonfs_mkdir / rmdir / unlink

- **WHEN** 调用 `hexagonfs_mkdir(fds, rootfd, name)` 等目录操作
- **THEN** 在 rootfd 指定的目录下执行操作
- **AND** 映射到对应的 POSIX 系统调用

### Requirement: 路径解析

HexagonFS SHALL 安全地解析路径，防止目录逃逸。

#### Scenario: 零分配路径解析

- **WHEN** `hexagonfs_path_next()` 解析路径
- **THEN** 返回 `(const char *start, size_t len)` 指向原始路径中的段
- **AND** 不进行 malloc/free
- **AND** 跳过连续的 `/`
- **AND** 正确处理 `.`（当前目录）和 `..`（上级目录）

#### Scenario: 路径逃逸防护

- **WHEN** DSP 请求包含 `..` 的路径
- **THEN** 路径解析不超过虚拟根目录
- **AND** 不会泄露宿主机上 HexagonFS root 之外的路径

### Requirement: refcount（引用计数）生命周期

HexagonFS SHALL 使用引用计数管理 FD 生命周期。

#### Scenario: 正常关闭

- **WHEN** `hexagonfs_close(fileno)` 被调用
- **THEN** fd->refcount 减 1
- **AND** refcount 归零时释放 fd 及其所有非共享祖先节点

#### Scenario: openat 所有权

- **WHEN** driver 的 `openat()` 成功创建子 fd
- **THEN** VFS 设置 `child->up = parent` 且 `child->refcount = 1`
- **AND** driver 不触碰 child->up 或 child->refcount

#### Scenario: openat 失败回滚

- **WHEN** 路径遍历在中间段失败
- **THEN** 仅释放在此次遍历中创建的新 fd
- **AND** 起始 fd (fds[selected]) 不受影响
