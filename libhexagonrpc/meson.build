libhexagonrpc = shared_library('hexagonrpc',
  'context.c',
  'fastrpc.c',
  'method_defs.c',
  'remotectl.c',
  'session.c',
  c_args : cflags,
  include_directories : include,
  soversion : api_version,
  install : true
)

