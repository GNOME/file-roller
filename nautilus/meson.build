shared_module('nautilus-fileroller', 
  sources : [
    'nautilus-fileroller.c',
    'fileroller-module.c'
  ],
  dependencies : [ 
    libm_dep, 
    thread_dep,
    glib_dep, 
    gthread_dep, 
    gtk_dep, 
    libnautilus_extension_dep,
    use_json_glib ? libjson_glib_dep : [],
    use_libnotify ? libnotify_dep : []    
  ],  
  include_directories : config_inc,
  c_args : c_args,
  install : true,
  install_dir : libnautilus_extension_dep.get_pkgconfig_variable('extensiondir')
)
