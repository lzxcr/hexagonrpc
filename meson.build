project('hexagonrpc', 'c', version: '0.5.0', meson_version: '>=1.1')

version_array = meson.project_version().split('.')
api_version = version_array[0] + '.' + version_array[1]

include = include_directories('include')

cflags = ['-Wall', '-Wextra', '-Wpedantic', '-Wno-unused-parameter']

if get_option('hexagonrpcd_verbose')
  cflags += '-DHEXAGONRPC_VERBOSE'
endif

json_c = dependency('json-c', required : false)

subdir('libhexagonrpc')
subdir('hexagonrpcd')

subdir('data')
subdir('tools')

subdir('tests')
