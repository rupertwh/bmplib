project('bmplib', 'c', default_options: ['c_std=c11', 'warning_level=3'], version: '1.7.7')

cc = meson.get_compiler('c')

add_project_arguments(['-pedantic','-fvisibility=hidden'], language : 'c')

if get_option('buildtype') == 'debug'
  add_project_arguments('-DDEBUG', language: 'c')
elif get_option('buildtype') == 'release'
  add_project_arguments('-DNDEBUG', language: 'c')
endif

if get_option('sanitize')
  sanitize = [
    '-fsanitize=signed-integer-overflow',
    '-fsanitize=undefined',
    '-fsanitize=float-divide-by-zero',
  ]
  add_project_arguments(sanitize, language : 'c')
  add_project_link_arguments(sanitize, language: 'c')
endif

m_dep = cc.find_library('m', required : false)

conf_data = configuration_data()
conf_data.set('insanity_limit_mb', get_option('insanity_limit_mb'))
conf_data.set('libversion', meson.project_version())

configure_file(input : 'config.h.in',
               output : 'config.h',
               configuration : conf_data)


install_headers('bmplib.h')

bmplib_sources = ['bmp-read.c',
                  'bmp-write.c',
                  'bmp-read-loadimage.c',
                  'bmp-read-loadindexed.c',
                  'bmp-common.c',
                  'huffman.c',
                  'logging.c']


if cc.sizeof('float') != 4
  error('sizeof(float) must be 4 bytes.')
elif cc.sizeof('int') < 4
  error('sizeof(int) must be at least 32 bit.')
endif

gen_huffman = executable('gen-huffman', 'gen-huffman.c')
huff_codes = custom_target('huffman-codes.h',
                           output: 'huffman-codes.h',
                           command: [gen_huffman, '@OUTPUT@'],
)


bmplib = shared_library('bmp',
                        [bmplib_sources, huff_codes[0]],
                        version: meson.project_version(),
                        install: true,
                        dependencies: m_dep,
)

pkg_mod = import('pkgconfig')
pkg_mod.generate(libraries: bmplib,
                 version: meson.project_version(),
                 name: 'libbmp',
                 filebase: 'libbmp',
                 description: 'Library for reading/writing Windows BMP files.',
)
