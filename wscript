import os
import sys

from waflib.Build import BuildContext
from waflib.Tools import waf_unit_test

top = '.'
out = 'build'


class TestContext(BuildContext):
    cmd = 'test'
    fun = 'test'

def get_version_hash():
    import subprocess
    try:
        return subprocess.check_output([
            'git', 'rev-parse', '--verify', '--short', 'HEAD'
        ]).decode().strip()
    except subprocess.CalledProcessError:
        return ''

def options(opt):
    opt.load('compiler_c compiler_cxx waf_unit_test')
    opt.add_option('--desktop', action='store_true', default=False)
    opt.add_option('--release', action='store_true', default=False,
        help='enable specific arm target architecture optimizations')
    opt.add_option('--enable-debug', action='store_true', default=False,
        help='enable debug code and emit symbols')
    opt.add_option('--enable-profile', action='store_true', default=False,
        help='emit gprof profiling data')
    opt.add_option('--enable-ableton-link', action='store_true', default=True)
    opt.add_option('--enable-lua-cjson', action='store_true', default=True)
    opt.add_option('--profile-matron', action='store_true', default=False)
    # ensure doctest prints success lines by default
    opt.parser.set_defaults(testcmd='%s --success')
    opt.add_option(
        '--test-dry-run',
        action='store_true',
        default=False,
        help='preview discovered tests without building',
    )
    opt.add_option(
        '--skip-self-test',
        action='store_true',
        default=False,
        help='skip test runner self-test validation',
    )

    opt.recurse('maiden-repl')

def configure(conf):
    conf.load('compiler_c compiler_cxx waf_unit_test')

    conf.define('VERSION_MAJOR', 0)
    conf.define('VERSION_MINOR', 0)
    conf.define('VERSION_PATCH', 0)
    conf.define('VERSION_HASH', get_version_hash())

    conf.env.append_unique('CFLAGS', ['-std=gnu11', '-Wall', '-Wextra', '-Werror'])
    conf.env.append_unique('CFLAGS', ['-g'])
    conf.env.append_unique('CXXFLAGS', ['-std=c++14', '-faligned-new'])
    conf.define('_GNU_SOURCE', 1)

    conf.env.NORNS_MACOS = sys.platform == 'darwin'
    if conf.env.NORNS_MACOS:
        configure_macos(conf)
    else:
        conf.check_cfg(package='alsa', args=['--cflags', '--libs'])
        conf.check_cfg(package='libudev', args=['--cflags', '--libs'])
        conf.check_cfg(package='libevdev', args=['--cflags', '--libs'])
        conf.check_cfg(package='libgpiod', args=['--cflags', '--libs'])
    conf.check_cfg(package='liblo', args=['--cflags', '--libs'])
    conf.check_cfg(package='cairo', args=['--cflags', '--libs'])
    conf.check_cfg(package='cairo-ft', args=['--cflags', '--libs'])
    conf.check_cfg(package='lua53', args=['--cflags', '--libs'])

    if not conf.env.NORNS_MACOS:
        # macOS has dns_sd in libSystem
        conf.check_cfg(package='avahi-compat-libdns_sd', args=['--cflags', '--libs'])
    conf.check_cfg(package='sndfile', args=['--cflags', '--libs'])
    conf.check_cfg(package='jack', args=['--cflags', '--libs'])

    conf.check_cc(msg='Checking for libmonome',
        define_name='HAVE_LIBMONOME',
        mandatory=True,
        lib='monome',
        header_name='monome.h',
        uselib_store='LIBMONOME')

    conf.check_cc(msg='Checking for nng',
        define_name='HAVE_NNG',
        mandatory=True,
        lib='nng',
        header_name='nng/nng.h',
        uselib_store='NNG')

    if not conf.env.NORNS_MACOS:
        conf.check_cc(msg='Checking for atomic',
            define_name='HAVE_ATOMIC',
            mandatory=True,
            lib='atomic',
            uselib_store='ATOMIC')

    if conf.options.desktop:
        conf.check_cfg(package='sdl2', args=['--cflags', '--libs'])
        conf.define('NORNS_DESKTOP', True)

    conf.env.NORNS_DESKTOP = conf.options.desktop

    if conf.options.release:
        conf.define('NORNS_RELEASE', True)
    conf.env.NORNS_RELEASE = conf.options.release

    if conf.options.enable_debug:
        conf.define('NORNS_DEBUG', True)
    conf.env.NORNS_DEBUG = conf.options.enable_debug

    conf.env.NORNS_PROFILE = conf.options.enable_profile

    conf.env.ENABLE_ABLETON_LINK = conf.options.enable_ableton_link
    conf.define('HAVE_ABLETON_LINK', conf.options.enable_ableton_link)

    conf.env.ENABLE_LUA_CJSON = conf.options.enable_lua_cjson
    conf.define('HAVE_LUA_CJSON', conf.options.enable_lua_cjson)

    conf.recurse('maiden-repl')

def configure_macos(conf):
    # the macOS port is always a desktop build (screen:sdl)
    conf.options.desktop = True
    # extra dependency prefix (lua 5.3, nng 1.x, libmonome); see readme-macos.md
    deps = os.environ.get('NORNS_DEPS')
    if not deps:
        # nearest deps/prefix above the source tree
        d = conf.path.abspath()
        while d != os.path.dirname(d) and not os.path.isdir(os.path.join(d, 'deps', 'prefix')):
            d = os.path.dirname(d)
        deps = os.path.join(d, 'deps', 'prefix')
    brew = os.environ.get('HOMEBREW_PREFIX',
                          '/opt/homebrew' if os.uname().machine == 'arm64' else '/usr/local')
    pkg = [os.path.join(deps, 'lib', 'pkgconfig'), os.path.join(brew, 'lib', 'pkgconfig')]
    if os.environ.get('PKG_CONFIG_PATH'):
        pkg.append(os.environ['PKG_CONFIG_PATH'])
    os.environ['PKG_CONFIG_PATH'] = ':'.join(pkg)
    conf.env.append_value('INCLUDES', [os.path.join(deps, 'include'), os.path.join(brew, 'include')])
    conf.env.append_value('LIBPATH', [os.path.join(deps, 'lib'), os.path.join(brew, 'lib')])
    conf.env.append_value('RPATH', [os.path.join(deps, 'lib')])
    # keep build paths (they contain the user name) out of the binaries
    for flags in ('CFLAGS', 'CXXFLAGS'):
        conf.env.append_value(flags, ['-ffile-prefix-map=%s=/build/norns' % conf.path.abspath()])
    # the oldest macOS the app runs on (macos/MACOS_MIN, also used by setup-deps.sh and
    # make-app.sh)
    with open(os.path.join(conf.path.abspath(), 'macos', 'MACOS_MIN')) as f:
        conf.env.MACOS_MIN = f.read().strip()
    for flags in ('CFLAGS', 'CXXFLAGS', 'LINKFLAGS'):
        conf.env.append_value(flags, ['-mmacosx-version-min=' + conf.env.MACOS_MIN])
    conf.msg('oldest macOS', conf.env.MACOS_MIN)
    conf.msg('macOS dependency prefix', deps)

def build(bld):
    #bld.recurse('matron')
    bld.recurse('maiden-repl')
    bld.recurse('ws-wrapper')
    #bld.recurse('crone')
    bld.recurse('third-party')
    if bld.env.NORNS_MACOS:
        bld.recurse('macos')
    else:
        # pi-only: watches the hardware power/keys
        bld.recurse('watcher')
    bld.recurse('norns')

def test(bld):
    bld.recurse('tests')
    bld.add_post_fun(waf_unit_test.summary)
    bld.add_post_fun(waf_unit_test.set_exit_code)
