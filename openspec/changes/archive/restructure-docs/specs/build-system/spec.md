# Build System — 构建系统基线

> 本文档描述 HexagonRPC 的构建系统行为。

---

## ADDED Requirements

### Requirement: 构建系统选择

HexagonRPC SHALL 使用 Meson（>= 1.1）作为构建系统，Ninja 作为后端。

#### Scenario: 标准构建

- **WHEN** 用户在项目根目录执行 `meson setup build && ninja -C build`
- **THEN** 产出 `libhexagonrpc.so`（在 `build/libhexagonrpc/`）
- **AND** 产出 `hexagonrpcd`（在 `build/hexagonrpcd/`）

### Requirement: 顶层项目定义

顶层 `meson.build` SHALL 定义项目名 `hexagonrpc`、版本号、默认选项和子目录。

#### Scenario: 版本号

- **WHEN** 构建系统读取项目版本
- **THEN** `meson.project_version()` 返回 `'0.5.0'`
- **AND** `libhexagonrpc` 的 soversion 为 `0`

#### Scenario: 子目录顺序

- **WHEN** Meson 解析顶层 `meson.build`
- **THEN** 依次处理 `libhexagonrpc/`、`hexagonrpcd/`、`data/`、`tools/`、`tests/`

### Requirement: 编译选项

HexagonRPC SHALL 通过 `meson.options` 提供编译选项。

#### Scenario: hexagonrpcd_verbose 选项

- **WHEN** 用户以 `-Dhexagonrpcd_verbose=true` 配置
- **THEN** 编译宏 `HEXAGONRPC_VERBOSE` 被定义
- **AND** hexagonrpcd 在每次 RPC 调用时打印详细日志
- **AND** 默认值为 `false`

### Requirement: libhexagonrpc 共享库构建

`libhexagonrpc/meson.build` SHALL 编译以下源文件为一个共享库。

#### Scenario: 源文件列表

- **WHEN** libhexagonrpc 构建
- **THEN** 编译 `fastrpc.c`、`context.c`、`session.c`、`remotectl.c`、`method_defs.c`
- **AND** 产出 `libhexagonrpc.so.0`（soversion 0）
- **AND** 安装公共头文件到 `include/libhexagonrpc/`

### Requirement: hexagonrpcd 可执行文件构建

`hexagonrpcd/meson.build` SHALL 编译 hexagonrpcd 守护进程并链接 libhexagonrpc。

#### Scenario: 源文件列表

- **WHEN** hexagonrpcd 构建
- **THEN** 编译所有 `hexagonrpcd/*.c` 文件（不含 tests/）
- **AND** 链接 `libhexagonrpc` 共享库
- **AND** 可选依赖 `json-c`（用于 JSON 配置解析）

### Requirement: 测试构建

`tests/meson.build` SHALL 构建三个测试可执行文件。

#### Scenario: 测试目标

- **WHEN** 构建测试
- **THEN** 构建 `test_iobuffer`（链接 libhexagonrpc）
- **AND** 构建 `test_hexagonfs`（链接 hexagonrpcd 内部对象文件）
- **AND** 构建 `test_dsp_simulation`（DSP 行为模拟测试）

### Requirement: 安装规则

`meson install` SHALL 按标准 Linux 文件系统层级安装产物。

#### Scenario: 安装路径

- **WHEN** 执行 `ninja -C build install`
- **THEN** `libhexagonrpc.so` 安装到 `{libdir}/`
- **AND** `hexagonrpcd` 安装到 `{bindir}/`
- **AND** 头文件安装到 `{includedir}/libhexagonrpc/`
- **AND** man page 安装到 `{mandir}/man8/hexagonrpcd.8`
