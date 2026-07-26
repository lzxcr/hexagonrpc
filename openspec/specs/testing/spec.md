# Testing — 测试套件基线

> 本文档描述 HexagonRPC 的三层测试体系。

---

## Requirements

### Requirement: 三层测试体系

HexagonRPC SHALL 包含三个测试层：单元测试（iobuffer）、组件测试（hexagonfs）、集成模拟测试（dsp-simulation）。

#### Scenario: 测试执行

- **WHEN** 用户执行 `meson test -C build`
- **THEN** 全部三层测试运行并通过
- **AND** 所有测试汇总为单一通过/失败结果

### Requirement: test_iobuffer — I/O Buffer 单元测试

test_iobuffer SHALL 验证 FastRPC wire format 的编解码正确性。

#### Scenario: 编码解码

- **WHEN** `meson test -C build test_iobuffer` 运行
- **THEN** 测试 `inbuf_decode()` 正确提取 scalars 和 buffer 数据
- **AND** 测试 `outbufs_encode()` 正确写入返回值和输出数据
- **AND** 边界条件（空 buffer、满 buffer、不对齐）均覆盖

### Requirement: test_hexagonfs — HexagonFS 组件测试

test_hexagonfs SHALL 验证虚拟文件系统的核心行为。

#### Scenario: 覆盖项

- **WHEN** `meson test -C build test_hexagonfs` 运行
- **THEN** 至少覆盖：
  - 路径解析（`hexagonfs_path_next`）——普通路径、`.` / `..`、多个 `/`
  - FD 分配与释放（`hexagonfs_openat` + `hexagonfs_close`）
  - 文件读写（`hexagonfs_read` / `hexagonfs_write` / `hexagonfs_lseek`）
  - 目录操作（`hexagonfs_readdir`）
  - fstat（`hexagonfs_fstat`）
  - 错误路径（文件不存在、无效 FD、越界访问）

### Requirement: test_dsp_simulation — DSP 行为模拟测试

test_dsp_simulation SHALL 模拟完整 DSP 行为链，端到端验证 apps_std 的 37 个方法。

#### Scenario: 覆盖项

- **WHEN** `meson test -C build test_dsp_simulation` 运行
- **THEN** 至少覆盖：
  - skel 加载流程（`fopen_with_env` + `fread`）
  - ACDB 数据访问（`fopen` + `fread` + `fclose`）
  - 传感器配置（`fopen` + `fstat`）
  - SoC 信息读取（sysfs 映射）
  - seek/tell/stat 组合操作
  - 目录遍历（`opendir` + `readdir` + `closedir`）
  - EOF/Error 状态追踪（`feof` / `ferror` / `clearerr`）
  - 写入操作（`fwrite` + `ftrunc` + `fremove`）
  - 错误路径（文件不存在、无效操作、权限拒绝）
