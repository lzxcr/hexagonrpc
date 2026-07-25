systemd_units = [
  'hexagonrpcd-adsp.service',
  'hexagonrpcd-adsp-audiopd.service',
  'hexagonrpcd-adsp-sensorspd.service',
  'hexagonrpcd-cdsp.service',
  'hexagonrpcd-sdsp.service',
]

foreach unit : systemd_units
  configure_file(
    input: unit + '.in',
    output: unit,
    configuration: { 'bindir': get_option('prefix') / get_option('bindir') },
    install: true,
    install_dir: get_option('libdir') / 'systemd/system',
  )
endforeach

# udev rules
install_data(
  '60-hexagonrpc.rules',
  install_dir: '/usr/lib/udev/rules.d',
)

# sysusers.d
install_data(
  'hexagonrpc.conf',
  install_dir: '/usr/lib/sysusers.d',
)
