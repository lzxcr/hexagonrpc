# Interfaces — 接口体系基线

> 本文档描述 HexagonRPC 的接口定义机制与所有已实现的本地接口。

---

## ADDED Requirements

### Requirement: 接口定义机制

HexagonRPC SHALL 使用 `.def` 文件和 `HEXAGONRPC_DEFINE_REMOTE_METHOD` 宏定义远程方法签名，无需 QAIC IDL 编译器。

#### Scenario: .def 文件结构

- **WHEN** 编写 `.def` 文件
- **THEN** 每行一个 `HEXAGONRPC_DEFINE_REMOTE_METHOD(mid, name, innums, inbufs, outnums, outbufs)` 调用
- **AND** 文件被 `#include` 两次：一次生成 `extern const` 声明，一次生成实际 `fastrpc_function_def_interp2` 定义

#### Scenario: .def 文件位置

- **WHEN** 系统构建
- **THEN** 公共接口定义在 `include/libhexagonrpc/interfaces/`
- **AND** hexagonrpcd 内部接口定义在 `hexagonrpcd/interfaces/`

### Requirement: remotectl 接口 (handle=0)

remotectl SHALL 实现 `open` 和 `close` 两个方法，用于 DSP 按名称查找和关闭本地接口。

#### Scenario: remotectl_open (method 0)

- **WHEN** DSP 调用 `remotectl.open("apps_std")`
- **THEN** 在 `ifaces[]` 数组中按 `name` 字段查找 "apps_std"
- **AND** 返回数组索引作为 handle
- **AND** 未找到时返回 error=-5

#### Scenario: remotectl_close (method 1)

- **WHEN** DSP 调用 `remotectl.close(handle)`
- **THEN** 返回 0（空操作，接口是静态的）

### Requirement: apps_std 接口 (handle=1) — 全部 37 个方法

apps_std SHALL 为 DSP 实现完整的 C 标准库风格文件 I/O 接口，共 37 个方法，所有文件操作通过 HexagonFS 路由。

#### Scenario: 方法完整性

- **WHEN** 系统编译完成
- **THEN** `apps_std_procs[]` 数组包含 37 个非 NULL 条目
- **AND** 每个条目对应 `apps_std.def` 中的一个方法

#### Scenario: fopen (method 0)

- **WHEN** DSP 调用 `fopen(name, mode)`
- **THEN** 通过 HexagonFS `hexagonfs_openat()` 打开文件
- **AND** 以 `/` 开头的路径通过虚拟目录树重映射
- **AND** 不以 `/` 开头的路径在 library path 中搜索，fallback 到 rootfd

#### Scenario: freopen (method 1)

- **WHEN** DSP 调用 `freopen(fd, name, mode)`
- **THEN** 先 `hexagonfs_close(fd)` 关闭旧文件
- **AND** 再 `hexagonfs_openat()` 打开新文件
- **AND** 返回新 fd

#### Scenario: fflush (method 2)

- **WHEN** DSP 调用 `fflush(fd)`
- **THEN** 返回 0（空操作）

#### Scenario: fclose (method 3)

- **WHEN** DSP 调用 `fclose(fd)`
- **THEN** 调用 `hexagonfs_close(fd)` 关闭文件
- **AND** 返回 0

#### Scenario: fread (method 4)

- **WHEN** DSP 调用 `fread(fd, buf, size)`
- **THEN** 调用 `hexagonfs_read(fd, buf, size)` 读取数据
- **AND** 更新 `fd_eof[fd]` 状态（读取字节数 < 请求字节数时置 true）
- **AND** 返回实际读取字节数

#### Scenario: fwrite (method 5)

- **WHEN** DSP 调用 `fwrite(fd, buf, size)`
- **THEN** 调用 `hexagonfs_write(fd, size, buf)` 写入数据
- **AND** 返回实际写入字节数

#### Scenario: fgetpos (method 6)

- **WHEN** DSP 调用 `fgetpos(fd, &pos)`
- **THEN** 调用 `hexagonfs_lseek(fd, 0, SEEK_CUR)` 获取当前位置
- **AND** 将 `fpos_t` 结构 memcpy 到输出 buffer

#### Scenario: fsetpos (method 7)

- **WHEN** DSP 调用 `fsetpos(fd, &pos)`
- **THEN** 从输入 buffer memcpy `fpos_t` 结构
- **AND** 调用 `hexagonfs_lseek(fd, pos, SEEK_SET)` 设置位置

#### Scenario: ftell (method 8)

- **WHEN** DSP 调用 `ftell(fd)`
- **THEN** 调用 `hexagonfs_lseek(fd, 0, SEEK_CUR)` 返回当前位置

#### Scenario: fseek (method 9)

- **WHEN** DSP 调用 `fseek(fd, offset, whence)`
- **THEN** 调用 `hexagonfs_lseek(fd, offset, whence)` 设置位置
- **AND** whence 支持 SEEK_SET / SEEK_CUR / SEEK_END

#### Scenario: flen (method 10)

- **WHEN** DSP 调用 `flen(fd)`
- **THEN** 调用 `hexagonfs_fstat(fd, &st)` 获取文件大小
- **AND** 返回 `st.st_size`

#### Scenario: rewind (method 11)

- **WHEN** DSP 调用 `rewind(fd)`
- **THEN** 调用 `hexagonfs_lseek(fd, 0, SEEK_SET)`
- **AND** 重置 `fd_eof[fd]` 和 `fd_err[fd]` 为 false

#### Scenario: feof (method 12)

- **WHEN** DSP 调用 `feof(fd)`
- **THEN** 返回 `ctx.fd_eof[fd]` 的值

#### Scenario: ferror (method 13)

- **WHEN** DSP 调用 `ferror(fd)`
- **THEN** 返回 `ctx.fd_err[fd]` 的值

#### Scenario: clearerr (method 14)

- **WHEN** DSP 调用 `clearerr(fd)`
- **THEN** 重置 `ctx.fd_eof[fd]` 和 `ctx.fd_err[fd]` 为 false

#### Scenario: print_string (method 15)

- **WHEN** DSP 调用 `print_string(str)`
- **THEN** 通过 `printf("DSP: %s\n", str)` 输出到 stdout

#### Scenario: getenv (method 16)

- **WHEN** DSP 调用 `getenv(name)`
- **THEN** 调用 libc `getenv(name)` 并返回结果

#### Scenario: setenv (method 17)

- **WHEN** DSP 调用 `setenv(name, value, overwrite)`
- **THEN** 调用 libc `setenv(name, value, overwrite)` 并返回结果

#### Scenario: unsetenv (method 18)

- **WHEN** DSP 调用 `unsetenv(name)`
- **THEN** 调用 libc `unsetenv(name)` 并返回结果

#### Scenario: fopen_with_env (method 19)

- **WHEN** DSP 调用 `fopen_with_env(env_var, name, mode)`
- **THEN** 解析环境变量值作为搜索路径
- **AND** 支持 `ADSP_LIBRARY_PATH` 和 `ADSP_AVS_CFG_PATH`
- **AND** 通过 HexagonFS `hexagonfs_openat()` 打开文件

#### Scenario: fgets (method 20)

- **WHEN** DSP 调用 `fgets(fd, buf, buf_size)`
- **THEN** 逐字节通过 `hexagonfs_read(fd, &ch, 1)` 读取
- **AND** 读到 `\n` 或填满 `buf_size-1` 时停止
- **AND** NUL 终止输出 buffer

#### Scenario: get_search_paths_with_env (method 21)

- **WHEN** DSP 调用 `get_search_paths_with_env(env_var)`
- **THEN** 返回空序列（numPaths=0, maxPathLen=0）
- **AND** HexagonFS 在文件操作层透明重定向路径

#### Scenario: fileExists (method 22)

- **WHEN** DSP 调用 `fileExists(name)`
- **THEN** 通过 `hexagonfs_openat()` 尝试打开
- **AND** 成功则 `hexagonfs_close()` 并返回 1
- **AND** 失败返回 0

#### Scenario: fsync (method 23)

- **WHEN** DSP 调用 `fsync(fd)`
- **THEN** 返回 0（空操作）

#### Scenario: fremove (method 24)

- **WHEN** DSP 调用 `fremove(name)`
- **THEN** 通过 HexagonFS `hexagonfs_unlink(rootfd, name)` 删除文件
- **AND** 返回 unlink 结果

#### Scenario: fdopen_decrypt (method 25)

- **WHEN** DSP 调用 `fdopen_decrypt(fd)`
- **THEN** 直接返回 fd（无解密需要）

#### Scenario: opendir (method 26)

- **WHEN** DSP 调用 `opendir(path)`
- **THEN** 通过 HexagonFS `hexagonfs_openat()` 打开目录
- **AND** 返回目录 fd

#### Scenario: closedir (method 27)

- **WHEN** DSP 调用 `closedir(fd)`
- **THEN** 调用 `hexagonfs_close(fd)` 关闭目录

#### Scenario: readdir (method 28)

- **WHEN** DSP 调用 `readdir(fd)`
- **THEN** 调用 `hexagonfs_readdir(fd, &dirent)` 读取目录条目
- **AND** 返回条目名称和类型

#### Scenario: mkdir (method 29)

- **WHEN** DSP 调用 `mkdir(name)`
- **THEN** 通过 HexagonFS `hexagonfs_mkdir(rootfd, name)` 创建目录
- **AND** 返回 mkdir 结果

#### Scenario: rmdir (method 30)

- **WHEN** DSP 调用 `rmdir(name)`
- **THEN** 通过 HexagonFS `hexagonfs_rmdir(rootfd, name)` 删除目录
- **AND** 返回 rmdir 结果

#### Scenario: stat (method 31)

- **WHEN** DSP 调用 `stat(fd, &st)`
- **THEN** 调用 `hexagonfs_fstat(fd, &st)` 获取文件状态
- **AND** 返回 stat 结果

#### Scenario: ftrunc (method 32)

- **WHEN** DSP 调用 `ftrunc(fd, length)`
- **THEN** 调用 `hexagonfs_ftruncate(fd, length)` 截断文件
- **AND** 返回 truncate 结果

#### Scenario: frename (method 33)

- **WHEN** DSP 调用 `frename(oldname, newname)`
- **THEN** 直接调用 libc `rename(oldname, newname)`
- **AND** 返回 rename 结果

#### Scenario: fopen_fd (method 34)

- **WHEN** DSP 调用 `fopen_fd(name, mode, &fd, &len)`
- **THEN** 通过 HexagonFS `hexagonfs_openat()` 打开文件
- **AND** 调用 `hexagonfs_fstat()` 获取文件大小
- **AND** 返回 fd 和 len

#### Scenario: fclose_fd (method 35)

- **WHEN** DSP 调用 `fclose_fd(fd)`
- **THEN** 调用 `hexagonfs_close(fd)` 关闭文件

#### Scenario: fopen_with_env_fd (method 36)

- **WHEN** DSP 调用 `fopen_with_env_fd(env_var, name, mode, &fd, &len)`
- **THEN** 同 `fopen_with_env` + 返回 fd 和 len

### Requirement: apps_mem 接口 (handle=2) — 全部 8 个方法

apps_mem SHALL 为 DSP 实现内存映射操作。

#### Scenario: 方法完整性

- **WHEN** 系统编译完成
- **THEN** `apps_mem_procs[]` 数组包含 8 个非 NULL 条目

#### Scenario: request_map (method 0)

- **WHEN** DSP 调用 `reqest_map(fd, size)`
- **THEN** 通过 `FASTRPC_IOCTL_MMAP` ioctl 分配内存
- **AND** 返回映射后的 32 位地址

#### Scenario: request_unmap (method 1)

- **WHEN** DSP 调用 `request_unmap(fd, vaddr, size)`
- **THEN** 通过 `FASTRPC_IOCTL_MUNMAP` ioctl 释放内存

#### Scenario: request_map64 (method 2)

- **WHEN** DSP 调用 `request_map64(fd, size)`
- **THEN** 通过 `FASTRPC_IOCTL_MMAP` ioctl 分配内存
- **AND** 返回映射后的 64 位地址

#### Scenario: request_unmap64 (method 3)

- **WHEN** DSP 调用 `request_unmap64(fd, vaddr, size)`
- **THEN** 通过 `FASTRPC_IOCTL_MUNMAP` ioctl 释放内存

#### Scenario: share_map (method 4)

- **WHEN** DSP 调用 `share_map(fd, size, &vaddr)`
- **THEN** 通过 `mmap()` 映射 DMA-BUF fd
- **AND** 再通过 `FASTRPC_IOCTL_MMAP` 映射到 DSP

#### Scenario: share_unmap (method 5)

- **WHEN** DSP 调用 `share_unmap(fd, vaddr, size)`
- **THEN** 通过 `FASTRPC_IOCTL_MUNMAP` 释放

#### Scenario: dma_handle_map (method 6)

- **WHEN** DSP 调用 `dma_handle_map(fd, size)`
- **THEN** 通过 `FASTRPC_IOCTL_MEM_MAP` ioctl 映射 DMA handle

#### Scenario: dma_handle_unmap (method 7)

- **WHEN** DSP 调用 `dma_handle_unmap(fd, vaddr, size)`
- **THEN** 通过 `FASTRPC_IOCTL_MEM_UNMAP` ioctl 释放

### Requirement: adsp_listener 接口 (handle=3)

adsp_listener SHALL 实现反向隧道的核心——从 DSP 接收 RPC 调用请求。

#### Scenario: adsp_listener_init2 (method 3)

- **WHEN** 调用 `adsp_listener_init2()`
- **THEN** 在 DSP 端初始化反向隧道监听器
- **AND** 返回 0 表示成功

#### Scenario: adsp_listener_next2 (method 4)

- **WHEN** 调用 `adsp_listener_next2(fd, result, rctx, outbufs, &rctx, &handle, &sc, &inbufs_len, inbufs)`
- **THEN** 等待 DSP 发来的下一个 RPC 调用
- **AND** 返回 handle (标识目标接口)
- **AND** 返回 sc (scalars，含 method ID)
- **AND** 返回 inbufs (DSP 传来的输入参数)
- **AND** 同时将上次调用的返回值 (outbufs) 传回 DSP

### Requirement: adsp_default_listener

adsp_default_listener SHALL 支持注册默认监听器。

#### Scenario: register (method 0)

- **WHEN** 调用 `adsp_default_listener_register()`
- **THEN** 在 DSP 端注册反向隧道监听器
- **AND** 返回 handle 和 error code
