project(
  'file-roller',
  'c',
  license: 'GPL-2.0-or-later',
  version: '44.5',
  meson_version: '>=0.59',
)

glib_version = '>=2.38'
gtk4_version = '>=4.8.1'
hdy_version = '>=1.5.0'
nautilus_version = '>=43.beta'
json_glib_version = '>=0.14.0'
libarchive_version = '>=3.1.900a'

gnome = import('gnome')
i18n = import('i18n')
gettext_package = meson.project_name()
prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))
datadir = join_paths(prefix, get_option('datadir'))
docdir = join_paths(datadir, 'doc')
privexecdir = join_paths(prefix, get_option('libexecdir'), meson.project_name())
c_comp = meson.get_compiler('c')

# Dependencies

use_native_appchooser = get_option('use_native_appchooser')

libm_dep = c_comp.find_library('m')
thread_dep = dependency('threads')
glib_dep = dependency('glib-2.0', version: glib_version)
gthread_dep = dependency('gthread-2.0')
gtk_dep = dependency('gtk4', version: gtk4_version)
libportal_dep = dependency('libportal', version: '>= 0.7', required: use_native_appchooser)
libportal_gtk4_dep = dependency('libportal-gtk4', version: '>= 0.7', required: use_native_appchooser)
libadwaita_dep = dependency('libadwaita-1', version: '>= 1.2')

# Optional dependencies
gobject_introspection_dep = dependency('gobject-introspection-1.0', required: get_option('introspection'))

libnautilus_extension_dep = dependency('libnautilus-extension-4', version: nautilus_version, required: get_option('nautilus-actions'))
build_nautilus_actions = libnautilus_extension_dep.found()

libjson_glib_dep = dependency('json-glib-1.0', version: json_glib_version, required: false)
use_json_glib = libjson_glib_dep.found()

libarchive_dep = dependency('libarchive', version: libarchive_version, required: get_option('libarchive'))
use_libarchive = libarchive_dep.found()

cpio_path = 'cpio'
if get_option('cpio') == 'auto'
  cpio = find_program('gcpio', 'cpio', required: false)
  if cpio.found()
    cpio_path = cpio.full_path()
  endif
endif

gi_docgen = find_program(
  'gi-docgen',
  version: '>= 2021.1',
  required: get_option('api_docs'),
)

build_introspection = gobject_introspection_dep.found()
build_api_docs = gi_docgen.found()

if build_api_docs and not build_introspection
  error('Building “api_docs” requires the “introspection” feature to be enabled as well, make sure you have libgirepository installed.')
endif

# config.h

config_data = configuration_data()
config_data.set_quoted('GETTEXT_PACKAGE', gettext_package)
config_data.set_quoted('LOCALEDIR', join_paths(prefix, get_option('localedir')))
config_data.set_quoted('PACKAGE_NAME', meson.project_name())
config_data.set_quoted('PACKAGE_VERSION', meson.project_version())
config_data.set('DEVELOPMENT_VERSION', meson.project_version().contains('.alpha') or meson.project_version().contains('.beta'))
config_data.set_quoted('FILE_ROLLER_RESOURCE_UI_PATH', '/org/gnome/FileRoller/ui/')
if get_option('run-in-place')
  config_data.set_quoted('PRIVDATADIR', join_paths(meson.project_source_root(), 'data'))
  config_data.set_quoted('PRIVEXECDIR', join_paths(meson.project_source_root(), 'src', 'commands'))
  config_data.set_quoted('UIDIR', join_paths(meson.project_source_root(), 'data', 'ui'))
  config_data.set_quoted('SHDIR', join_paths(meson.project_source_root(), 'src', 'sh'))
else
  config_data.set_quoted('PRIVDATADIR', join_paths(datadir, meson.project_name()))
  config_data.set_quoted('PRIVEXECDIR', privexecdir)
  config_data.set_quoted('UIDIR', join_paths(datadir, meson.project_name(), 'ui'))
  config_data.set_quoted('SHDIR', join_paths(prefix, get_option('libexecdir'), meson.project_name()))
endif
if build_introspection
  config_data.set('ENABLE_INTROSPECTION', 1)
endif
if use_json_glib
  config_data.set('HAVE_JSON_GLIB', 1)
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
config_data.set_quoted('CPIO_PATH', cpio_path)
config_data.set('USE_NATIVE_APPCHOOSER', use_native_appchooser)
config_file = configure_file(output: 'config.h', configuration: config_data)
config_inc = include_directories('.')

# C args

c_args = ['-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_38']
if get_option('buildtype').contains('debug')
  warn_deprecated = get_option('warn-deprecated')
  test_args = [
    '-Wall',
    '-Wextra',
    '-Wcast-align',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wpointer-arith',
    '-Wformat-security',
    '-Wno-unused-parameter',
  ]
else
  warn_deprecated = false
  test_args = ['-Wall']
endif
if not(warn_deprecated)
  test_args += [ '-Wno-deprecated-declarations' ]
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
subdir('docs')

gnome.post_install(
  gtk_update_icon_cache: true,
  glib_compile_schemas: true,
  update_desktop_database: true,
)

# Summary

summary(
  {
    'prefix': prefix,
    'warn deprecated': warn_deprecated,
    'nautilus actions': build_nautilus_actions,
    'packagekit': get_option('packagekit'),
    'libarchive': use_libarchive,
    'cpio': cpio_path,
  },
  section: 'Configuration summary:',
)
