shared_module(
  'nautilus-fileroller',
  sources: [
    'nautilus-fileroller.c',
    'fileroller-module.c',
  ],
  dependencies: [
    libm_dep,
    thread_dep,
    glib_dep,
    gthread_dep,
    libnautilus_extension_dep,
    use_json_glib ? libjson_glib_dep : [],
  ],
  include_directories: config_inc,
  c_args: c_args,
  install: true,
  install_dir: libnautilus_extension_dep.get_variable('extensiondir', pkgconfig_define: ['libdir', libdir]),
)
