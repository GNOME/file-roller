if build_api_docs
  toml_data = configuration_data()
  toml_data.set('VERSION', meson.project_version())

  fr_toml = configure_file(
    input: 'file-roller.toml.in',
    output: 'file-roller.toml',
    configuration: toml_data,
  )

  custom_target(
    'file-roller-doc',
    input: [
      fr_toml,
      fr_gir[0],
    ],
    output: 'file-roller',
    command: [
      gi_docgen,
      'generate',
      '--quiet',
      '--add-include-path=@0@'.format(meson.current_build_dir() / '../../src'),
      '--config=@INPUT0@',
      '--output-dir=@OUTPUT@',
      '--no-namespace-dir',
      '--content-dir=@0@'.format(meson.current_source_dir()),
      '@INPUT1@',
    ],
    build_by_default: true,
    install: true,
    install_dir: docdir,
  )
endif
