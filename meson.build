project('bmplib', 'c', default_options: ['c_std=c11'], version: '1.4.6')

cc = meson.get_compiler('c')


add_project_arguments('-pedantic', language : 'c')
add_project_arguments('-fvisibility=hidden', language: 'c')
if get_option('buildtype') == 'debug'
  add_project_arguments('-DDEBUG', language: 'c')
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
                  'logging.c']

bmplib = shared_library('bmp',
                        bmplib_sources,
                        version: meson.project_version(),
                        install: true,
                        dependencies: m_dep)

pkg_mod = import('pkgconfig')
pkg_mod.generate(libraries: bmplib,
                 version: meson.project_version(),
                 name: 'libbmp',
                 filebase: 'libbmp',
                 description: 'Library for reading/writing Windows BMP files.')
