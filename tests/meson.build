pymod = import('python')

python3_for_dogtail_tests = pymod.find_installation(
  'python3',
  modules: [
    'dbus',
    'dbusmock',
    'dogtail',
    'gi',
  ],
  required: get_option('dogtail'),
)
dbus_daemon = find_program(
  'dbus-daemon',
  required: get_option('dogtail'),
)

enable_dogtail_tests = python3_for_dogtail_tests.found() and dbus_daemon.found()

if enable_dogtail_tests
  test_env = [
    'LC_ALL=C',
    'G_TEST_SRCDIR=' + meson.current_source_dir(),
    'G_TEST_BUILDDIR=' + meson.current_build_dir(),
    'GSETTINGS_SCHEMA_DIR=' + meson.project_build_root() / 'data',
    'GSETTINGS_BACKEND=memory',
  ]

  test(
    'basic',
    python3_for_dogtail_tests,
    args: [
      meson.current_source_dir() / 'basic.py',
    ],
    env: test_env,
    depends: [
      fr_exe,
      compiled_schemas,
    ],
  )

endif
