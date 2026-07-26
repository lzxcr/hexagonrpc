# libhexagonrpc — 共享库 API 基线

> 本文档描述 `libhexagonrpc.so` 提供的编程接口。

---

## Requirements

### Requirement: fastrpc2 底层调用

`fastrpc2()` SHALL 通过 `FASTRPC_IOCTL_INVOKE` ioctl 直接调用 DSP 远程方法。

#### Scenario: 基本调用

- **WHEN** 调用者传入 `fastrpc_function_def_interp2` 指针、FD、handle 和可变参数
- **THEN** 根据 def 中的 `in_nums`/`in_bufs`/`out_nums`/`out_bufs` 构造 `fastrpc_invoke_args` 数组
- **AND** 通过 `FASTRPC_IOCTL_INVOKE` ioctl 发送
- **AND** 将输出 scalars 写回调用者提供的指针
- **AND** 返回 ioctl 的返回值（0 = 成功）

#### Scenario: 可变参数约定

- **WHEN** 调用者传可变参数
- **THEN** 参数顺序为：所有输入 scalars（按 def 的 in_nums 数量）→ 各输入 buffer 的长度值（uint64_t）→ 各输入 buffer 指针 → 各输出 scalars 的目标指针 → 各输出 buffer 的最大长度值 → 各输出 buffer 指针
- **AND** 每个输入/输出 buffer 对应一对（长度, 指针）

### Requirement: vfastrpc2 可变参数版本

`vfastrpc2()` SHALL 提供 `va_list` 版本，行为与 `fastrpc2()` 一致。

#### Scenario: va_list 兼容性

- **WHEN** 调用者使用 `va_list` 传参
- **THEN** 与 `fastrpc2(...)` 完全等价

### Requirement: fastrpc_context 上下文封装

`fastrpc_create_context()` 和 `fastrpc_destroy_context()` SHALL 将 FD + handle 封装为可复用的 `fastrpc_context` 对象。

#### Scenario: context 创建与销毁

- **WHEN** 调用 `fastrpc_create_context(fd, handle)`
- **THEN** 返回 `malloc` 分配的 `fastrpc_context*`，存储 fd 和 handle
- **AND** 调用 `fastrpc_destroy_context(ctx)` 释放内存

### Requirement: fastrpc 高层调用

`fastrpc()` SHALL 基于 context 对象进行调用，等价于 `fastrpc2(def, ctx->fd, ctx->handle, ...)`。

#### Scenario: context 调用

- **WHEN** 调用 `fastrpc(&def, ctx, ...)`
- **THEN** 内部调用 `vfastrpc2(&def, ctx->fd, ctx->handle, args)`
- **AND** 无需调用者重复传 FD 和 handle

### Requirement: REMOTE_SCALARS 宏

libhexagonrpc SHALL 提供 `REMOTE_SCALARS_MAKE` / `REMOTE_SCALARS_INBUFS` / `REMOTE_SCALARS_OUTBUFS` 宏用于构造 FastRPC scalars 值。

#### Scenario: scalars 构造

- **WHEN** 调用者使用 `REMOTE_SCALARS_MAKE(method, in_count, out_count)`
- **THEN** `in_count` = in_bufs + (有 in_nums 或 in_bufs ? 1 : 0)
- **AND** `out_count` = out_bufs + (有 out_nums ? 1 : 0)
- **AND** 返回值嵌入 method ID（低 16 位）和 buffer 计数

### Requirement: hexagonrpc_fd_from_env

`hexagonrpc_fd_from_env()` SHALL 从环境变量获取共享的 FastRPC FD。

#### Scenario: 正常获取

- **WHEN** 调用 `hexagonrpc_fd_from_env()`
- **THEN** 读取 `HEXAGONRPC_FD` 环境变量
- **AND** 解析为整数文件描述符
- **AND** 环境变量不存在或无效时返回 -1

### Requirement: remotectl 公共实现

libhexagonrpc SHALL 提供 `remotectl_open()` / `remotectl_close()` 的公共实现。

#### Scenario: remotectl_open

- **WHEN** 调用 `remotectl_open(fd, name, &ctx)`
- **THEN** 通过 `fastrpc2(&remotectl_open_def, fd, REMOTECTL_HANDLE, ...)` 向 DSP 端发起接口查找
- **AND** 成功时创建 `fastrpc_context` 并写入 ctx
- **AND** 返回 0 表示成功

#### Scenario: remotectl_close

- **WHEN** 调用 `remotectl_close(ctx)`
- **THEN** 调用 `fastrpc2(&remotectl_close_def, ctx->fd, ctx->handle, ...)` 关闭远程接口
- **AND** 调用 `fastrpc_destroy_context(ctx)`
- **AND** 返回 0 表示成功

### Requirement: 接口定义宏

libhexagonrpc SHALL 提供 `HEXAGONRPC_DEFINE_REMOTE_METHOD` 宏用于手写 `.def` 文件。

#### Scenario: 宏参数

- **WHEN** 在 `.def` 文件中使用 `HEXAGONRPC_DEFINE_REMOTE_METHOD(mid, name, innums, inbufs, outnums, outbufs)`
- **THEN** `innums` = 第一个输入 buffer 中的 32-bit 字数（不含 buffer 自身的长度字段）
- **AND** `inbufs` = 除第一个外的额外输入 buffer 数量
- **AND** `outnums` = 第一个输出 buffer 中的 32-bit 字数
- **AND** `outbufs` = 除第一个外的额外输出 buffer 数量

#### Scenario: 编译时双重角色

- **WHEN** 编译包含 `.def` 文件的源文件
- **THEN** 定义 `HEXAGONRPC_BUILD_METHOD_DEFINITIONS=1` 时，宏展开为 `const struct fastrpc_function_def_interp2 name_def = {...}`
- **AND** 未定义时，宏展开为 `extern const struct fastrpc_function_def_interp2 name_def`
