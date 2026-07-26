# HexagonRPC API 参考

## libhexagonrpc — FastRPC 包装库

### 头文件

```c
#include <libhexagonrpc/fastrpc.h>       // fastrpc2, fastrpc, fastrpc_context
#include <libhexagonrpc/remotectl.h>     // remotectl_open, remotectl_close
#include <libhexagonrpc/session.h>       // hexagonrpc_fd_from_env
```

### 方法定义宏

```c
// .def 文件
HEXAGONRPC_DEFINE_REMOTE_METHOD(mid, name, innums, inbufs, outnums, outbufs)
```

参数说明：
- `mid` — 方法 ID
- `innums` — 第一个输入 buffer 中的 32-bit 字数（不含 buffer 长度字段）
- `inbufs` — 额外输入 buffer 数量
- `outnums` — 第一个输出 buffer 中的 32-bit 字数
- `outbufs` — 额外输出 buffer 数量

这些数字对应 `REMOTE_SCALARS_MAKE(method, in_count, out_count)` 的参数，其中：
- `in_count = inbufs + (in_nums > 0 || inbufs > 0 ? 1 : 0)`
- `out_count = outbufs + (out_nums > 0 ? 1 : 0)`

### fastrpc2 — 底层调用

```c
int fastrpc2(const struct fastrpc_function_def_interp2 *def,
             int fd, uint32_t handle, ...);
```

可变参数顺序：
1. 输入 scalars（`in_nums` 个 uint32_t 值）
2. 各输入 buffer 长度（`inbufs` 个 uint64_t 值）
3. 各输入 buffer 指针（`inbufs` 个 void* 值）
4. 各输出 scalar 的目标指针（`out_nums` 个 uint32_t* 值）
5. 各输出 buffer 最大长度（`outbufs` 个 uint64_t 值）
6. 各输出 buffer 指针（`outbufs` 个 void* 值）

返回值：0 = 成功，负值 = errno 或 AEE error code。

### fastrpc_context — 上下文对象

```c
struct fastrpc_context *fastrpc_create_context(int fd, uint32_t handle);
void fastrpc_destroy_context(struct fastrpc_context *ctx);
int fastrpc(const struct fastrpc_function_def_interp2 *def,
            const struct fastrpc_context *ctx, ...);
```

### hexagonrpc_fd_from_env — 获取共享 FD

```c
int hexagonrpc_fd_from_env(void);
// 读取 HEXAGONRPC_FD 环境变量并解析为 int
// 返回 -1 表示未设置或无效
```

### remotectl — 远程接口查找

```c
int remotectl_open(int fd, const char *interface_name,
                   struct fastrpc_context **ctx);
int remotectl_close(struct fastrpc_context *ctx);
```

## 使用示例

### 基本调用

```c
#include <libhexagonrpc/fastrpc.h>
#include <libhexagonrpc/interfaces/remotectl.def>

int fd = open("/dev/fastrpc-adsp", O_RDWR);

// 查找 "apps_std" 接口
struct fastrpc_context *apps_std;
uint32_t handle, dlret;
char err[256];

int ret = fastrpc2(&remotectl_open_def, fd, 0,  // handle=0
                   strlen("apps_std") + 1, "apps_std",
                   &handle, &dlret, 256, err);

// 创建 context 对象
struct fastrpc_context *ctx = fastrpc_create_context(fd, handle);

// 调用方法
fastrpc(&apps_std_flen_def, ctx, fileno, &length);

// 清理
fastrpc_destroy_context(ctx);
close(fd);
```

### 使用环境变量获取 FD

```c
int fd = hexagonrpc_fd_from_env();
if (fd < 0) {
    fprintf(stderr, "HEXAGONRPC_FD not set; are we running under hexagonrpcd?\n");
    exit(1);
}
// fd 由 hexagonrpcd 通过 -p 选项 fork+exec 时传入
```

## 已实现的 .def 接口文件

### 公共接口 (`include/libhexagonrpc/interfaces/`)

| 文件 | handle | 方法数 |
|------|--------|--------|
| `remotectl.def` | 0 | 2 |

### 内部接口 (`hexagonrpcd/interfaces/`)

| 文件 | handle | 方法数 |
|------|--------|--------|
| `apps_std.def` | 动态 | 37 |
| `apps_mem.def` | 动态 | 8 |
| `adsp_listener.def` | 3 | 2 |
| `adsp_default_listener.def` | 动态 | 1 |
