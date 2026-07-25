# HexagonRPC v0.5.0 — 现代化升级完整归档

## 变更范围

1. apps_std: 10/37 → **37/37** 方法 (新增 27 个)
2. apps_mem: 1/7 → **8/8** 方法 (新增 7 个)
3. HexagonFS: 只读 → **读写双模式** (write/unlink/mkdir/rmdir/truncate)
4. 公共 remotectl: 重复代码 → **提取到 libhexagonrpc**
5. 配置: 无 → **JSON 配置 + DMA-BUF 内存分配器**
6. 命名: fastrpc v0.4.0 → **hexagonrpc v0.5.0**
7. 死代码: 移除 hexagonfs_plat_subtype_name.c / log.h

## 文件统计

- apps_std.c: 1223 行
- apps_mem.c: 315 行
- hexagonfs.c/h + backends: 1020 行
- tests: dsp_simulation.c (37 项测试)
- docs: ANALYSIS.md + DESIGN_MODERNIZATION.md + QUICKSTART.md

## 编译

0 错误, 0 警告, meson test 3/3 通过

