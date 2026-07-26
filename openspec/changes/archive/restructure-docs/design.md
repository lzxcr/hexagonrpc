## Capability 拆分原理

### 拆分依据

从 `hexagonrpc-docs` 单一 capability 拆为 8 个，遵循以下原则：

1. **独立演化**：每个 capability 描述的系统行为可以独立修改而不影响其他 spec
2. **单一受众**：每个 spec 面向一类读者（内核 hacker 看 daemon，应用开发者看 libhexagonrpc，打包者看 deployment）
3. **可测试边界**：每个 capability 有清晰的可验证行为（Scenario → 可执行测试）

### 不拆过细的原因

- `interfaces` 合并 remotectl + apps_std + apps_mem + adsp_listener：它们共享同一套 .def 机制和调度基础设施，拆开会导致大量重复
- `daemon` 合并启动流程 + CLI + 反向隧道：三者强耦合（启动参数决定隧道行为）

### docs/ vs specs/ 的分工

| 维度 | `openspec/specs/` | `docs/` |
|------|-------------------|---------|
| 受众 | 机器可验证、贡献者、审计 | 用户、开发者入门 |
| 格式 | Requirement + Scenario (WHEN/THEN) | 自由格式、示例驱动 |
| 更新时机 | 行为变更时 | spec 变更后同步 |
| 内容 | 系统 SHALL 做什么 | 怎么用、怎么构建 |
