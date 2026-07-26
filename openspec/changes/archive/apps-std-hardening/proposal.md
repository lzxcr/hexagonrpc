# Proposal: apps_std.c defensive hardening (P0/P1)

## Changes

1. **buffer_ok on remaining functions**: fwrite, ftrunc, fdopen_decrypt, closedir, readdir, fsetpos, setenv
2. **fgets outbuf size check**: `if (!buffer_ok(&outbufs[1], first_in->buf_size))`
3. **getenv truncation**: `if (first_in->val_size < need) return AEE_EBUFFER;`
4. **init/deinit dirfd**: close on init failure; explicit close in deinit

## Excluded (separate proposals needed)
- rename → hexagonfs (API change)
- fopen mode (API change)
- fseek uint64 (ABI change)
- packed structs (ABI change)

## Success criteria
- Build: 0 errors, 0 warnings
- Tests: 3/3 pass
