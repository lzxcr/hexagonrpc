executable('hexagonrpcd',
  'aee_error.c',
  'apps_mem.c',
  'apps_std.c',
  'config.c',
  'method_defs.c',
  'hexagonfs.c',
  'hexagonfs_mapped.c',
  'hexagonfs_virt_dir.c',
  'iobuffer.c',
  'listener.c',
  'localctl.c',
  'rpcd.c',
  'rpcd_builder.c',
  'rpcmem_linux.c',
  c_args : cflags,
  include_directories : include,
  install : true,
  link_with : libhexagonrpc,
  dependencies : json_c,
  install_dir : get_option('bindir'),
)
install_man('hexagonrpcd.8')

# Man page symlinks for each domain
meson.add_install_script(
  '../data/install-man-symlinks.sh',
  get_option('prefix') / get_option('mandir') / 'man8',
)
