# Sources

source_files = files(
  'dlg-add.c',
  'dlg-ask-password.c',
  'dlg-batch-add.c',
  'dlg-delete.c',
  'dlg-extract.c',
  'dlg-open-with.c',
  'dlg-package-installer.c',
  'dlg-password.c',
  'dlg-prop.c',
  'dlg-update.c',
  'file-utils.c',
  'fr-application.c',
  'fr-application-menu.c',
  'fr-archive.c',
  'fr-command-7z.c',
  'fr-command-ace.c',
  'fr-command-alz.c',
  'fr-command-ar.c',
  'fr-command-arj.c',
  'fr-command.c',
  'fr-command-cfile.c',
  'fr-command-cpio.c',
  'fr-command-dpkg.c',
  'fr-command-iso.c',
  'fr-command-jar.c',
  'fr-command-lha.c',
  'fr-command-lrzip.c',
  'fr-command-rar.c',
  'fr-command-rpm.c',
  'fr-command-tar.c',
  'fr-command-unsquashfs.c',
  'fr-command-unstuff.c',
  'fr-command-zip.c',
  'fr-command-zoo.c',
  'fr-error.c',
  'fr-file-data.c',
  'fr-file-selector-dialog.c',
  'fr-init.c',
  'fr-location-bar.c',
  'fr-location-button.c',
  'fr-new-archive-dialog.c',
  'fr-places-sidebar.c',
  'fr-process.c',
  'fr-window-actions-callbacks.c',
  'fr-window.c',
  'gio-utils.c',
  'glib-utils.c',
  'gtk-utils.c',
  'java-utils.c',
  'main.c',
  'open-file.c',
  'preferences.c',
  'rar-utils.c',
)

fr_headers = files(
  'fr-application-menu.h',
  'fr-application.h',
  'fr-archive.h',
  'fr-command-7z.h',
  'fr-command-ace.h',
  'fr-command-alz.h',
  'fr-command-ar.h',
  'fr-command-arj.h',
  'fr-command-cfile.h',
  'fr-command-cpio.h',
  'fr-command-dpkg.h',
  'fr-command-iso.h',
  'fr-command-jar.h',
  'fr-command-lha.h',
  'fr-command-lrzip.h',
  'fr-command-rar.h',
  'fr-command-rpm.h',
  'fr-command-tar.h',
  'fr-command-unsquashfs.h',
  'fr-command-unstuff.h',
  'fr-command-zip.h',
  'fr-command-zoo.h',
  'fr-command.h',
  'fr-error.h',
  'fr-file-data.h',
  'fr-file-selector-dialog.h',
  'fr-init.h',
  'fr-location-bar.h',
  'fr-location-button.h',
  'fr-new-archive-dialog.h',
  'fr-places-sidebar.h',
  'fr-process.h',
  'fr-window-actions-callbacks.h',
  'fr-window-actions-entries.h',
  'fr-window.h',
)

if libjson_glib_dep.found()
  source_files += ['fr-command-unarchiver.c']
  fr_headers += ['fr-command-unarchiver.h']
endif
if use_libarchive
  source_files += ['fr-archive-libarchive.c']
  fr_headers += ['fr-archive-libarchive.h']
endif

gresource_files = gnome.compile_resources(
  'fr-resources',
  'file-roller.gresource.xml',
  c_name: 'fr',
)

marshal_files = gnome.genmarshal(
  'fr-marshal',
  prefix: 'fr_marshal',
  sources: 'fr-marshal.list',
)

enum_files = gnome.mkenums_simple(
  'fr-enum-types',
  sources: [
    'typedefs.h',
  ],
)

# Build targets

fr_exe = executable(
  'file-roller',
  sources: [
    config_file,
    source_files,
    gresource_files,
    marshal_files,
    enum_files,
  ],
  dependencies: [
    libm_dep,
    thread_dep,
    glib_dep,
    gthread_dep,
    gtk_dep,
    libadwaita_dep,
    use_native_appchooser ? libportal_dep : [],
    use_native_appchooser ? libportal_gtk4_dep : [],
    build_introspection ? gobject_introspection_dep : [],
    use_json_glib ? libjson_glib_dep : [],
    use_libarchive ? libarchive_dep : [],
  ],
  include_directories: config_inc,
  c_args: c_args,
  install: true,
  # Required for GIRepository to be able to dump symbols.
  export_dynamic: build_introspection,
)

if build_introspection
  fr_gir = gnome.generate_gir(
    fr_exe,
    sources: [
      source_files,
      fr_headers,
      enum_files,
      'typedefs.h',
    ],
    namespace: 'FileRoller',
    nsversion: '1.0',
    identifier_prefix: 'Fr',
    includes: [
      'Gio-2.0',
      'GObject-2.0',
      'Gtk-4.0',
    ],
  )
endif

executable(
  'test-server',
  'test-server.c',
  dependencies: [
    libm_dep,
    thread_dep,
    glib_dep,
    gthread_dep,
    gtk_dep,
    use_json_glib ? libjson_glib_dep : [],
  ],
  include_directories: config_inc,
  c_args: c_args,
  install: false,
)

test(
  'safe-path',
  executable(
    'test-safe-path',
    sources: ['test-safe-path.c', 'glib-utils.c'],
    dependencies: [
      libm_dep,
      thread_dep,
      glib_dep,
      gthread_dep,
      gtk_dep,
    ],
    include_directories: config_inc,
    c_args: c_args,
  ),
)

# Subdirectories

subdir('commands')
subdir('sh')
