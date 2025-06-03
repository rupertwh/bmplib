project('bmplib', 'c', default_options: ['c_std=c11', 'warning_level=3'], version: '1.8.0')

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
                  'bmp-read-icons.c',
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

test_read_conversions = executable('test_read_conversions',
                 'test-read-conversions.c',
                 'bmp-common.c',
                 'bmp-read.c',
                 'bmp-read-icons.c',
                 'bmp-write.c',
                 'huffman.c',
                 'logging.c',
                 huff_codes[0],
                 dependencies: m_dep,
)

test_read_io = executable('test_read_io',
                 'test-read-io.c',
                 'bmp-common.c',
                 'bmp-read.c',
                 'bmp-read-icons.c',
                 'bmp-write.c',
                 'huffman.c',
                 'logging.c',
                 'test-fileio-pipes.c',
                 huff_codes[0],
                 dependencies: m_dep,
)

test_write_io = executable('test_write_io',
                 'test-write-io.c',
                 'bmp-common.c',
                 'bmp-read.c',
                 'bmp-read-icons.c',
                 'bmp-read-loadimage.c',
                 'huffman.c',
                 'logging.c',
                 'test-fileio-pipes.c',
                 huff_codes[0],
                 dependencies: m_dep,
)

test('read - s_s2_13_to_float',              test_read_conversions, args : ['s_s2_13_to_float'])
test('read - s_convert64',                   test_read_conversions, args : ['s_convert64'])
test('read - s_float_to_s2_13',              test_read_conversions, args : ['s_float_to_s2_13'])
test('read - roundtrip_s2.13-float-s2.13',   test_read_conversions, args : ['roundtrip_s2.13-float-s2.13'])
test('read - s_srgb_gamma_float',            test_read_conversions, args : ['s_srgb_gamma_float'])
test('read - s_int8_to_result_format/float', test_read_conversions, args : ['s_int8_to_result_format', 'float'])
test('read - s_int8_to_result_format/int',   test_read_conversions, args : ['s_int8_to_result_format', 'int'])
test('read - s_int8_to_result_format/s2.13', test_read_conversions, args : ['s_int8_to_result_format', 's2.13'])
test('read - s_buffer32_fill',               test_read_io, args : ['s_buffer32_fill'])
test('read - s_buffer32_bits',               test_read_io, args : ['s_buffer32_bits'])
test('read - s_read_rgb_pixel',              test_read_io, args : ['s_read_rgb_pixel'])
test('read - read_u16_le',                   test_read_io, args : ['read_u16_le'])
test('read - read_s16_le',                   test_read_io, args : ['read_s16_le'])
test('read - read_u32_le',                   test_read_io, args : ['read_u32_le'])
test('read - read_s32_le',                   test_read_io, args : ['read_s32_le'])
test('write - write_u32_le',                 test_write_io, args : ['write_u32_le'])
test('write - write_s32_le',                 test_write_io, args : ['write_s32_le'])
test('write - write_u16_le',                 test_write_io, args : ['write_u16_le'])
test('write - write_s16_le',                 test_write_io, args : ['write_s16_le'])
test('write - s_imgrgb_to_outbytes/int',     test_write_io, args : ['s_imgrgb_to_outbytes', 'int'])
test('write - s_imgrgb_to_outbytes/float',   test_write_io, args : ['s_imgrgb_to_outbytes', 'float'])
test('write - s_imgrgb_to_outbytes/s2.13',   test_write_io, args : ['s_imgrgb_to_outbytes', 's2.13'])
