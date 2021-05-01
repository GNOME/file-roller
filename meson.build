project('file-roller', 'c',
  license : 'GPL2+',
  version : '3.40.0',
  meson_version : '>=0.50'
)

glib_version = '>=2.38'
gtk_version = '>=3.22.0'
nautilus_version = '>=3.28.0'
json_glib_version = '>=0.14.0'
libnotify_version = '>=0.4.3'
libarchive_version = '>=3.1.900a'

gnome = import('gnome')
i18n = import('i18n')
gettext_package = meson.project_name()
prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
privexecdir = join_paths(prefix, get_option('libexecdir'), meson.project_name())
c_comp = meson.get_compiler('c')

meson.add_install_script('postinstall.py')

# Dependencies

libm_dep = c_comp.find_library('m')
thread_dep = dependency('threads')
glib_dep = dependency('glib-2.0', version : glib_version)
gthread_dep = dependency('gthread-2.0')
gtk_dep = dependency('gtk+-3.0', version : gtk_version)

# Optional dependencies

libnautilus_extension_dep = dependency('libnautilus-extension', version : nautilus_version, required : get_option('nautilus-actions'))
build_nautilus_actions = libnautilus_extension_dep.found()

libjson_glib_dep = dependency('json-glib-1.0', version : json_glib_version, required : false)
use_json_glib = libjson_glib_dep.found()
 
libnotify_dep = dependency('libnotify', version : libnotify_version, required : get_option('notification'))
use_libnotify = libnotify_dep.found()

libarchive_dep = dependency('libarchive', version : libarchive_version, required : get_option('libarchive'))
use_libarchive = libarchive_dep.found()

have_mkdtemp = c_comp.has_function('mkdtemp')

cpio_path = 'cpio'
if get_option('cpio') == 'auto'
  cpio = find_program('gcpio', 'cpio', required : false)
  if cpio.found()
    cpio_path = cpio.path()
  endif
endif

# config.h

config_data = configuration_data()
config_data.set_quoted('GETTEXT_PACKAGE', gettext_package)
config_data.set_quoted('LOCALEDIR', join_paths(prefix, get_option('localedir')))
config_data.set_quoted('PACKAGE_NAME', meson.project_name())
config_data.set_quoted('PACKAGE_VERSION', meson.project_version())
if get_option('run-in-place')
  config_data.set_quoted('PRIVDATADIR', join_paths(meson.source_root(), 'data'))
  config_data.set_quoted('PRIVEXECDIR', join_paths(meson.source_root(), 'src', 'commands'))
  config_data.set_quoted('UIDIR', join_paths(meson.source_root(), 'data', 'ui'))
  config_data.set_quoted('SHDIR', join_paths(meson.source_root(), 'src', 'sh'))  
else
  config_data.set_quoted('PRIVDATADIR', join_paths(datadir, meson.project_name()))
  config_data.set_quoted('PRIVEXECDIR', privexecdir)  
  config_data.set_quoted('UIDIR', join_paths(datadir, meson.project_name(), 'ui'))
  config_data.set_quoted('SHDIR', join_paths(prefix, get_option('libexecdir'), meson.project_name()))  
endif
if use_json_glib
  config_data.set('HAVE_JSON_GLIB', 1)
endif
if use_libnotify
  config_data.set('ENABLE_NOTIFICATION', 1)
endif
if use_libarchive
  config_data.set('ENABLE_LIBARCHIVE', 1)
endif
if get_option('packagekit')
  config_data.set('ENABLE_PACKAGEKIT', 1)
endif
if get_option('buildtype').contains('debug')
  config_data.set('DEBUG', 1)
endif
if have_mkdtemp
  config_data.set('HAVE_MKDTEMP', 1)
endif
config_data.set_quoted('CPIO_PATH', cpio_path)
config_file = configure_file(output : 'config.h', configuration : config_data)
config_inc = include_directories('.')

# C args

c_args = [ '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_38' ]
if get_option('buildtype').contains('debug')
  test_args = [ 
    '-Wall', 
    '-Wcast-align', 
    '-Wtype-limits', 
    '-Wclobbered', 
    '-Wempty-body', 
    '-Wignored-qualifiers', 
    '-Wmissing-prototypes',
    '-Wnested-externs', 
    '-Wpointer-arith', 
    '-Wno-sign-compare', 
    '-Wformat-security'
  ]
else
  c_args += [ '-Wno-deprecated-declarations' ]
  test_args = [ '-Wall' ]
endif
c_args += c_comp.get_supported_arguments(test_args)

# Subdirectories

subdir('data')
subdir('help')
if build_nautilus_actions
  subdir('nautilus')
endif
subdir('po')
subdir('src')

# Summary

summary = [
  'configuration summary:',
  '',
  '           project: @0@ @1@'.format(meson.project_name(), meson.project_version()),
  '            prefix: @0@'.format(prefix),
  '  nautilus actions: @0@'.format(build_nautilus_actions),
  '     notifications: @0@'.format(use_libnotify),
  '        packagekit: @0@'.format(get_option('packagekit')),
  '        libarchive: @0@'.format(use_libarchive),
  '      have mkdtemp: @0@'.format(have_mkdtemp),
  '              cpio: @0@'.format(cpio_path),
  ''
]
message('\n'.join(summary))
