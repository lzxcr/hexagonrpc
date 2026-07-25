## Why

编译产生 `-Wunused-result` 和 `-Wunused-variable` 警告，影响构建质量。

## What Changes

### 1. sns_registrygen.c: 忽略 write() 返回值
添加 `(void)` 转换抑制警告。

### 2. rpcd.c: create_shell_pd 忽略 read() 返回值
添加错误检查：read() 返回值应与 st_size 一致。

### 3. test_hexagonfs.c: 忽略 read() 返回值
添加 `(void)` 转换（测试代码，正确性由断言保证）。

### 4. test_dsp_simulation.c: 未使用变量 + 忽略返回值
- 删除 `off_t pos` 未使用变量
- `system()` 结果添加 `(void)` 抑制
- `write()` 添加 `(void)` 抑制（测试代码中已通过 READ 验证写入正确性）
