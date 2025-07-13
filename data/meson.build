po_dir = join_paths(meson.project_source_root(), 'po')

install_data(
  'org.gnome.FileRoller.gschema.xml',
  install_dir: join_paths(datadir, 'glib-2.0', 'schemas'),
)

gnome.compile_schemas(
  depend_files: [
    'org.gnome.FileRoller.gschema.xml',
  ],
)

install_data(
  'packages.match',
  install_dir: join_paths(datadir, meson.project_name()),
)

# .desktop file

desktop_in_file = custom_target(
  'org.gnome.FileRoller.desktop.in',
  input: [
    'supported-mime-types',
    'org.gnome.FileRoller.desktop.in.in',
  ],
  output: 'org.gnome.FileRoller.desktop.in',
  command: [
    find_program('python3'),
    files('set-mime-type-entry.py'),
    '@INPUT@',
  ],
  capture: true,
  install: false,
)

i18n.merge_file(
  input: desktop_in_file,
  output: 'org.gnome.FileRoller.desktop',
  type: 'desktop',
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(datadir, 'applications'),
)

# .service files

service_data = configuration_data()
service_data.set('bindir', join_paths(prefix, get_option('bindir')))

configure_file(
  input: 'org.gnome.FileRoller.service.in',
  output: 'org.gnome.FileRoller.service',
  configuration: service_data,
  install: true,
  install_dir: join_paths(datadir, 'dbus-1/services'),
)

configure_file(
  input: 'org.gnome.ArchiveManager1.service.in',
  output: 'org.gnome.ArchiveManager1.service',
  configuration: service_data,
  install: true,
  install_dir: join_paths(datadir, 'dbus-1/services'),
)

# .metainfo file

i18n.merge_file(
  input: 'org.gnome.FileRoller.metainfo.xml.in',
  output: 'org.gnome.FileRoller.metainfo.xml',
  type: 'xml',
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(datadir, 'metainfo'),
)

# Subdirectories

subdir('icons')
