# Refactor hexagonfs.c — Round 2 (ownership + lifecycle)

## MODIFIED Requirements

### Requirement: Ownership model
The VFS SHALL own all fd lifecycle metadata. Drivers SHALL NOT need to set
`child->up` or `child->is_assigned` — these SHALL be set by the VFS layer
immediately after a driver's `openat` succeeds.

#### Scenario: Driver creates child fd
- **WHEN** a driver's `openat(from_dirent, &child)` returns 0
- **THEN** the VFS SHALL set `child->up = parent` and `child->refcount = 1`
- **AND** the driver SHALL NOT touch `child->up` or `child->refcount`

### Requirement: Refcount lifecycle
The VFS SHALL use reference counting instead of `bool is_assigned`.
`close()` SHALL decrement refcount; free occurs at 0.

#### Scenario: Normal close
- **WHEN** `hexagonfs_close(fileno)` is called
- **THEN** the fd's refcount SHALL be decremented
- **AND** if refcount reaches 0, the fd and all unshared ancestors SHALL be freed

### Requirement: openat error path
`hexagonfs_openat` SHALL NOT destroy the starting (root or directory) fd object
on error — only newly created intermediate fd objects.

#### Scenario: openat fails mid-walk
- **WHEN** a path walk fails at segment N
- **THEN** only fd objects created for segments 1..N SHALL be freed
- **AND** the starting fd (fds[selected]) SHALL NOT be freed

### Requirement: Duplicate "."/".." elimination
Path segment handling for `.` and `..` SHALL be centralized in one location,
not duplicated in walk_child and openat.

### Requirement: Defensive cycle detection
`hexagonfs_fd_release` SHALL guard against self-referential `up` pointers
with an assert or depth limit.

## ADDED Requirements

### Requirement: Single-pass path scanning
`hexagonfs_path_next()` SHALL scan each segment in a single pass without
calling both `strchr()` and `strlen()`.

### Requirement: Consistent early-return style
All public FD operations SHALL use flat early-return pattern:
```c
if (!fd) return -EBADF;
if (!fd->ops->op) return -ENOSYS;
return fd->ops->op(...);
```
