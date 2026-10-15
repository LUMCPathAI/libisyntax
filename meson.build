project(
  'libisyntax', 'c', 'cpp',
  version: '0.1.0',
  license : 'BSD-2-Clause',
  meson_version : '>=0.51.0',
  default_options : [
    'buildtype=release',
    'c_std=gnu11',
  ],
)

is_macos = host_machine.system() == 'darwin'
is_windows = host_machine.system() == 'windows'
is_apple_silicon = is_macos and host_machine.cpu() in ['arm', 'aarch64']

cc = meson.get_compiler('c')
add_project_arguments(
  cc.get_supported_arguments(
    # there are many of these, and they're sometimes dependent on compile
    # options
    '-Wno-unused-function',
    '-Wno-unused-but-set-variable',
    '-Wno-unused-variable',
  ),
  language : 'c',
)

tiff = dependency('libtiff-4', required : false)
if is_windows
  threads = dependency('', required : false)
  winmm = cc.find_library('winmm')
else
  threads = dependency('threads')
  winmm = dependency('', required : false)
endif

if is_apple_silicon
  add_project_arguments(
    '-march=armv8.2-a+fp16+simd',
    language : 'c'
  )
endif

isyntax_includes = include_directories(
  'src',
  'src/isyntax',
  'src/platform',
  'src/third_party',
  'src/utils',
)

isyntax_source = [
  'src/isyntax/isyntax.c',
  'src/isyntax/isyntax_reader.c',
  'src/platform/platform_mutex.c',
  'src/platform/platform.c',
  'src/platform/work_queue.c',
  'src/third_party/ltalloc.cc',
  'src/third_party/yxml.c',
  'src/utils/block_allocator.c',
  'src/utils/timerutils.c',
  'src/libisyntax.c',
]

if is_windows
  isyntax_source += 'src/platform/win32_utils.c'
else
  isyntax_source += 'src/platform/linux_utils.c'
endif

isyntax = library(
  'isyntax',
  isyntax_source,
  dependencies : [threads, winmm],
  include_directories : isyntax_includes,
  install : true,
)
libisyntax_dep = declare_dependency(
  include_directories : include_directories('src'),
  link_with : isyntax,
)

pkg = import('pkgconfig')
pkg.generate(isyntax)
install_headers('src/libisyntax.h')

isyntax_example = executable(
  'isyntax_example',
  'src/examples/isyntax_example.c',
  dependencies: [libisyntax_dep],
)

if tiff.found()
  executable(
    'isyntax-to-tiff',
    'src/examples/isyntax_to_tiff.c',
    dependencies : [libisyntax_dep, tiff, winmm, threads],
    install : true,
  )
endif

if get_option('tests')
  python = import('python').find_installation(
    modules: ['requests'],
  )

  # Thank you https://gitlab.com/BioimageInformaticsGroup/openphi
  testslide = custom_target(
    'testslide.isyntax',
    command : [
      python, files('test/fetch.py'),
      'https://zenodo.org/record/5037046/files/testslide.isyntax?download=1',
      '@OUTPUT@',
    ],
    console : true,
    output : 'testslide.isyntax',
  )

  test('smoke_example_runs_no_args', isyntax_example)

  # Test that we can show levels and that number of tiles shown is as
  # expected for this test tile.
  test(
    'smoke_example_runs_with_test_slide_showing_levels',
    python,
    args : [
      files('test/match.py'),
      '-e', 'width.*=256',
      '-e', 'height.*=384',
      isyntax_example, testslide,
    ],
  )

  # Regression test that the produced tile pixels did not change from expected.
  test(
    'regression_example_tile_3_5_10_pixel_check',
    python,
    args : [
      files('test/compare-fixture.py'),
      '-f', files('test/expected_output/testslide_example_tile_3_5_10.png'),
      isyntax_example, testslide, '3', '5', '10',
    ],
  )

  if not is_macos
    # TODO: fix this test on macOS: fatal error: 'threads.h' file not found
    thread_test = executable(
      'thread_test',
      'test/thread_test.c',
      dependencies : [libisyntax_dep],
      include_directories : [isyntax_includes],
    )
    test('smoke_thread_test', thread_test)

    concurrent_read_test = executable(
      'concurrent_read_test',
      'test/concurrent_read_test.c',
      dependencies : [libisyntax_dep],
      include_directories : [isyntax_includes],
    )
    test('concurrent_reads_match_single_thread', concurrent_read_test, args : [testslide], timeout : 300)
  endif
endif
