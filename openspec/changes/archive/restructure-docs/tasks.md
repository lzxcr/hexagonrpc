## 1. 新建 8 个 baseline capability specs

- [ ] 1.1 `openspec/specs/architecture/spec.md` — 组件组成、依赖、数据流
- [ ] 1.2 `openspec/specs/build-system/spec.md` — Meson 构建、编译选项、产物
- [ ] 1.3 `openspec/specs/daemon/spec.md` — 启动流程、CLI、PD 模式、反向隧道
- [ ] 1.4 `openspec/specs/libhexagonrpc/spec.md` — API 层次
- [ ] 1.5 `openspec/specs/interfaces/spec.md` — 接口体系（.def + remotectl + apps_std + apps_mem + listener）
- [ ] 1.6 `openspec/specs/hexagonfs/spec.md` — 虚拟 FS、路径映射、FD 管理、读写语义
- [ ] 1.7 `openspec/specs/deployment/spec.md` — systemd/udev/sysusers.d/man pages
- [ ] 1.8 `openspec/specs/testing/spec.md` — test_iobuffer / test_hexagonfs / test_dsp_simulation

## 2. 重写 docs/ 用户文档

- [ ] 2.1 更新 `README.md`
- [ ] 2.2 更新 `docs/QUICKSTART.md`
- [ ] 2.3 新建 `docs/ARCHITECTURE.md`（从 ANALYSIS.md 精简）
- [ ] 2.4 新建 `docs/API.md`
- [ ] 2.5 新建 `docs/CONFIGURATION.md`

## 3. 删除过期文件

- [ ] 3.1 删除 `docs/ANALYSIS.md`
- [ ] 3.2 删除 `docs/DESIGN_MODERNIZATION.md`
- [ ] 3.3 删除 `openspec/specs/hexagonrpc-docs/`（旧单一 capability）

## 4. 归档 changes

- [ ] 4.1 归档 `refactor-hexagonfs/` → archive
- [ ] 4.2 归档 `apps-std-hardening/` → archive
- [ ] 4.3 归档 `fix-warnings/` → archive
