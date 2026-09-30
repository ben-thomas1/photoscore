project('photoscore', ['c', 'objc'],
    version         : '0.1.0',
    meson_version   : '>=1.3.0',
    default_options : [
        'c_std=c23',
        'warning_level=2',
        'werror=true',
    ],
)

cc   = meson.get_compiler('c')
objc = meson.get_compiler('objc')

# The flag lists below are the intended set. Anything the host toolchain does
# not accept is filtered out by get_supported_arguments(), so the same file
# builds on macOS/arm64 (where the Intel CET and GNU ld hardening flags do not
# exist) and on Linux/x86-64 (where they do).

warnings = [
    '-Wshadow',
    '-Wconversion',
    '-Wsign-conversion',
    '-Wvla',
    '-Wcast-align',
    '-Wpointer-arith',
    '-Wwrite-strings',
    '-Wformat=2',
    '-Wnull-dereference',
    '-Wdouble-promotion',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wimplicit-fallthrough',
]

# -O0 and -g come from meson's buildtype=debug; not repeated here.
debug_args = [
    '-fsanitize=address,undefined',
    '-fno-sanitize-recover=all',
    '-fno-omit-frame-pointer',
    '-fsanitize-address-use-after-scope',    # default-on in clang >= 7
    '-ftrivial-auto-var-init=pattern',
    '-fstack-protector-all',
    '-DPS_DEBUG',
]

debug_link_args = [
    '-fsanitize=address,undefined',
    '-v',
]

release_args = [
    '-O3',
    '-march=native',
    '-mtune=native',
    '-ffast-math',
    '-flto',
    '-fomit-frame-pointer',
    '-fno-stack-protector',
    '-fPIE',
    '-fcf-protection=full',                  # x86 CET; absent on arm64
    '-mbranch-protection=standard',          # arm64 PAC/BTI; absent on x86
    '-DNDEBUG',
]

release_link_args = [
    '-flto',
    '-Wl,-pie',                              # plain -pie warns as unused at compile
    '-Wl,-z,relro,-z,now',                   # GNU ld only
    '-Wl,-z,noexecstack',                    # GNU ld only; default on Mach-O
    '-v',
]

version_define = '-DPS_VERSION="' + meson.project_version() + '"'

add_project_arguments(cc.get_supported_arguments(warnings), language : 'c')
add_project_arguments(version_define, language : 'c')

# Objective-C reaches Vision, which has no C API. ARC is on so the one .m file
# does not carry manual retain/release.
add_project_arguments(objc.get_supported_arguments(warnings), language : 'objc')
add_project_arguments(version_define, '-fobjc-arc', language : 'objc')

if get_option('buildtype') == 'debug'
    add_project_arguments(cc.get_supported_arguments(debug_args), language : 'c')
    add_project_arguments(objc.get_supported_arguments(debug_args), language : 'objc')
    # Both languages need these: meson drives the link with whichever compiler
    # covers the target, and the sanitizer runtime must be on that line.
    add_project_link_arguments(cc.get_supported_link_arguments(debug_link_args), language : 'c')
    add_project_link_arguments(objc.get_supported_link_arguments(debug_link_args), language : 'objc')
else
    add_project_arguments(cc.get_supported_arguments(release_args), language : 'c')
    add_project_arguments(objc.get_supported_arguments(release_args), language : 'objc')
    add_project_link_arguments(cc.get_supported_link_arguments(release_link_args), language : 'c')
    add_project_link_arguments(objc.get_supported_link_arguments(release_link_args), language : 'objc')
endif

inc = include_directories('include')

# Everything but main, so tests can link the same objects the binary uses.
lib_sources = files(
    'src/arena.c',
    'src/backend/backend.c',
    'src/backend/apple/apple.c',
    'src/backend/apple/cgimage.c',
    'src/backend/apple/vision.m',
    'src/cli.c',
    'src/da.c',
    'src/dedup.c',
    'src/embedding.c',
    'src/image.c',
    'src/mask.c',
    'src/overlay.c',
    'src/photo.c',
    'src/progress.c',
    'src/score.c',
    'src/status.c',
    'src/technical.c',
    'src/walk.c',
)

# ImageIO decodes every format we accept, including HEIC and WebP, and carries
# the EXIF with it. Vision supplies the aesthetics model and is Objective-C.
frameworks = dependency('appleframeworks',
    modules : ['CoreFoundation', 'CoreGraphics', 'ImageIO', 'Foundation', 'Vision', 'CoreVideo'])

core = static_library('photoscore_core',
    lib_sources,
    include_directories : inc,
    dependencies        : [frameworks],
)

executable('photoscore',
    files('src/main.c'),
    include_directories : inc,
    dependencies        : [frameworks],
    link_with           : core,
    install             : true,
)

foreach t : ['image', 'backend', 'core', 'io', 'main']
    test(t, executable('test_' + t,
        files('tests/test_' + t + '.c'),
        include_directories : inc,
        dependencies        : [frameworks],
        link_with           : core,
    ))
endforeach
