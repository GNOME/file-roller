po_dir = join_paths(meson.source_root(), 'po')

install_data('org.gnome.FileRoller.gschema.xml',
  install_dir : join_paths(datadir, 'glib-2.0', 'schemas')
)

install_data('packages.match',
  install_dir : join_paths(datadir, meson.project_name())
)

# .desktop file

desktop_data = configuration_data()
desktop_data.set('VERSION', meson.project_version())            
desktop_in_file = configure_file(
  input : 'org.gnome.FileRoller.desktop.in.in',
  output : 'org.gnome.FileRoller.desktop.in',
  configuration : desktop_data
)

i18n.merge_file(
  input : desktop_in_file,
  output : 'org.gnome.FileRoller.desktop',
  type : 'desktop',
  po_dir : po_dir,
  install : true,
  install_dir : join_paths(datadir, 'applications')
)

# .service files

service_data = configuration_data()
service_data.set('bindir', join_paths(prefix, get_option('bindir'))) 

configure_file(
  input : 'org.gnome.FileRoller.service.in',
  output : 'org.gnome.FileRoller.service',
  configuration : service_data,
  install : true,
  install_dir : join_paths(datadir, 'dbus-1/services')
)

configure_file(
  input : 'org.gnome.FileRoller.ArchiveManager1.service.in',
  output : 'org.gnome.FileRoller.ArchiveManager1.service',
  configuration : service_data,
  install : true,
  install_dir : join_paths(datadir, 'dbus-1/services')
)

# .appdata file

i18n.merge_file(
  input : 'org.gnome.FileRoller.appdata.xml.in',
  output : 'org.gnome.FileRoller.appdata.xml',
  type : 'xml',
  po_dir : po_dir,
  install : true,
  install_dir : join_paths(datadir, 'metainfo')
)

# Subdirectories

subdir('icons')
