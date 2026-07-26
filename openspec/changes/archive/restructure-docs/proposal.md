## Why

当前文档体系存在以下结构性问题：

1. **基线 spec 未按 capability 拆分**：`openspec/specs/hexagonrpc-docs/spec.md` 将所有内容（架构/构建/API/FS/部署/测试）塞入单一 capability，违反 OpenSpec "按能力拆分" 原则
2. **版本信息矛盾**：`docs/ANALYSIS.md` 标注 v0.4.0，声称 apps_std 仅 10/37 方法，与当前代码（37/37 + 写入支持）严重不一致
3. **docs/ 与 openspec/specs/ 内容重叠**：同一事实在两处重复描述，且不一致
4. **changes/ 积压**：3 个 active change（refactor-hexagonfs / apps-std-hardening / fix-warnings）已反映在代码中但未归档
5. **DESIGN_MODERNIZATION.md 半成品**：大量 "✅ 已完成" 阶段标记但从未整理入基线 spec

## What Changes

### 1. 重建 `openspec/specs/` 基线（按 8 个 capability 拆分）

每个 capability 独立目录，各自一份 `spec.md`，从当前代码行为反推：

| Capability | 内容 |
|------------|------|
| `architecture` | 组件组成（libhexagonrpc / hexagonrpcd / HexagonFS）、依赖关系、数据流 |
| `build-system` | Meson 构建、编译选项、产物 |
| `daemon` | hexagonrpcd 启动流程、CLI 参数、PD 模式（INIT_ATTACH / INIT_ATTACH_SNS / INIT_CREATE）、反向隧道循环 |
| `libhexagonrpc` | API 层次（fastrpc2 / fastrpc / context / session）、remotectl_open/close |
| `interfaces` | 接口定义机制（.def + 宏）、remotectl(handle=0) / apps_std(37) / apps_mem(8) / adsp_listener |
| `hexagonfs` | 虚拟目录布局、路径映射、文件操作集(5 种后端)、FD 管理、读写语义 |
| `deployment` | systemd units / udev / sysusers.d / man pages / 安装路径 / DESTDIR |
| `testing` | test_iobuffer / test_hexagonfs / test_dsp_simulation |

### 2. 重写 `docs/` 用户文档

| 文件 | 来源 | 说明 |
|------|------|------|
| `README.md` | 更新 | 保持简洁，对齐当前版本 |
| `QUICKSTART.md` | 更新 | 刷新过时内容 |
| `ARCHITECTURE.md` | 从 ANALYSIS.md 精简 | 架构总览 + 数据流，面向开发者 |
| `API.md` | 新建 | libhexagonrpc 编程接口参考 |
| `CONFIGURATION.md` | 新建 | hexagonrpc.json 配置格式 |

**删除**：
- `docs/ANALYSIS.md` — 内容吸收进 ARCHITECTURE.md + specs
- `docs/DESIGN_MODERNIZATION.md` — 内容吸收进各 capability spec

### 3. 清理 `openspec/changes/`

- `refactor-hexagonfs/` → 内容吸收进 `specs/hexagonfs/spec.md`，移入 archive
- `apps-std-hardening/` → 内容吸收进 `specs/interfaces/spec.md`，移入 archive
- `fix-warnings/` → 简单改动，移入 archive
- 旧 `hexagonrpc-docs/` capability → 删除（内容已拆分）
