## Why

项目经历了多次迭代，文件命名逐渐不一致，产生了备份文件夹。需要最后一次清理使项目结构简洁、命名统一。

## What Changes

### 1. 重命名不一致的源文件

| 原名 | 新名 | 理由 |
|------|------|------|
| `tests/dsp_simulation.c` | `tests/test_dsp_simulation.c` | 与其他测试文件缺失 `test_` 前缀不一致 |
| `tools/sscregistrygen.c` | `tools/sns_registrygen.c` | `ssc` 缩写晦涩，全称应为 Sensors SubSystem |
| `hexagonrpcd/interfaces.c` | `hexagonrpcd/method_defs.c` | `interfaces` 命名模糊，实际是方法定义注册 |
| `chrecd/interfaces.c` | `chrecd/method_defs.c` | 同上，保持一致 |
| `libhexagonrpc/interfaces.c` | `libhexagonrpc/method_defs.c` | 同上 |

### 2. 清理 openspec 归档备份

移除 `openspec/changes/archive/` 下的 `.old` 后缀目录（来自之前归档操作的备份残留）

### 3. 移除允许构建过程中生成的文件

创建 `.gitignore` 忽略 `build/` 目录

### 4. 更新所有引用

- 所有 `meson.build` 中源文件列表
- 所有 `Android.bp` 中源文件列表
- 重命名后的文件不会影响公开 API
