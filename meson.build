project('hyprsplit', 'cpp',
  version: '0.1',
  default_options: ['buildtype=release', 'cpp_std=c++23', 'warning_level=1'],
)

add_project_arguments(
  [
    '-DWLR_USE_UNSTABLE'
  ],
  language: 'cpp')

globber = run_command('find', '.', '-name', '*.cpp', check: true)
src = globber.stdout().strip().split('\n')

shared_module(meson.project_name(), src,
  dependencies: [
    dependency('hyprland'),
    dependency('pixman-1'),
    dependency('libdrm'),
  ],
  install: true,
)
