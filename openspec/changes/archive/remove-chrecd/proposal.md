## Why

chrecd 是 CHRE（Context Hub Runtime Environment）客户端守护进程，非 hexagonrpc 核心组件。
其功能与 hexagonrpcd 的 reverse tunnel 正交——它仅是 libhexagonrpc 的一个用户态消费者。
移除它可以简化项目结构和构建系统。

## What Changes

### 1. 删除 chrecd/ 目录

移除 4 个文件：main.c + method_defs.c + meson.build + interfaces/chre_slpi.def

### 2. 清理构建系统

- 删除 root meson.build 中 `subdir('chrecd')` 及 `client_target` 变量
- `client_target` 仅被 chrecd 使用，无其他消费者

### 3. 保留 session.h / session.c

`hexagonrpc_fd_from_env()` 被 `rpcd.c` 的 `-p PROGRAM` 子进程启动功能使用，不删除。

### 4. 更新文档

清除 ANALYSIS.md 中的 chrecd 引用。

### 5. 目录结构不变

现有目录结构合理，无需重组：
- hexagonrpcd/ — core daemon ✅
- libhexagonrpc/ — shared library ✅
- include/ — public headers ✅
- data/ — deployment (systemd + udev + sysusers) ✅
- tests/ — test suite ✅
- tools/ — sns-registrygen ✅
- docs/ — documentation ✅
- openspec/ — spec system ✅
