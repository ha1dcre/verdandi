#!/usr/bin/env python3
"""The licenses and sources of everything Verdandi.app bundles.

  python3 -I licenses.py fetch <deps>
      once, online (macos/setup-deps.sh): the license texts and the exact sources of what is
      built here rather than taken from homebrew (jack, lua, nng, libmonome, SuperCollider with
      the libraries in its source, sc3-plugins, maiden with its Go modules and web packages),
      into <deps>/licenses/<component>/ (license texts, source.json)

  python3 -I licenses.py collect <deps> <bottles> <repo> <app Resources> <out>
      offline (macos/make-app.sh): <out>/ with a folder of license texts for each package
      the app really contains (found by its files), PACKAGES.txt (version, license, source,
      checksum or commit) and README.txt. stops if a bundled file belongs to no known package
      or a package has no license text.
"""
import glob
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
LICENSE_RE = re.compile(r'^(copying|license|licence|copyright|notice|unlicense|lgpl|gpl|mit|bsd)', re.I)


def setup_var(name):
    """a NAME=value line of setup-deps.sh"""
    with open(os.path.join(HERE, 'setup-deps.sh')) as f:
        m = re.search(r'^%s=(\S+)' % name, f.read(), re.M)
    if not m:
        raise SystemExit('licenses: %s not in setup-deps.sh' % name)
    return m.group(1)


def get(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'norns-macos-licenses'})
    with urllib.request.urlopen(req) as r:
        return r.read()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as w:
        w.write(text if isinstance(text, bytes) else text.encode())


def license_members(tf, under=''):
    """(path, bytes) of the license files in a tar, below under"""
    out = []
    for m in tf.getmembers():
        if not m.isfile():
            continue
        name = os.path.basename(m.name)
        if LICENSE_RE.match(name) and under in m.name and not name.endswith(('.c', '.cpp', '.h', '.hpp', '.py', '.sc', '.schelp')):
            out.append((m.name.split('/', 1)[1] if '/' in m.name else m.name, tf.extractfile(m).read()))
    return sorted(out)


def concat(parts):
    """one file of several license texts, each under its path"""
    return b''.join(b'==== ' + p.encode() + b' ====\n\n' + t.rstrip() + b'\n\n' for p, t in parts)


# license texts of bottles that have none in them
BOTTLE_LICENSE_URLS = {
    'boost': 'https://raw.githubusercontent.com/boostorg/boost/boost-{version}/LICENSE_1_0.txt',
}


#--- fetch

def fetch_component(deps, name, meta, files):
    d = os.path.join(deps, 'licenses', name)
    for fname, data in files.items():
        write(os.path.join(d, fname), data)
    write(os.path.join(d, 'source.json'), json.dumps(meta, indent=1, sort_keys=True))
    print('  %-14s %s' % (name, meta['version']))


def have(deps, name):
    return os.path.isfile(os.path.join(deps, 'licenses', name, 'source.json'))


def fetch(deps):
    prefix = os.path.join(deps, 'prefix')

    if not have(deps, 'jack'):
        v = setup_var('JACK_VERSION')
        fetch_component(deps, 'jack', {
            'name': 'JACK (jack2)', 'version': v,
            'license': 'GPL-2.0-or-later (jackd, drivers); LGPL-2.1-or-later (libjack)',
            'source': 'https://github.com/jackaudio/jack2/archive/refs/tags/v%s.tar.gz' % v,
            'sha256': setup_var('JACK_SHA256'),
            'note': 'built from source by macos/setup-deps.sh: ./waf configure --db=no'}, {
            'COPYING': get('https://raw.githubusercontent.com/jackaudio/jack2/v%s/COPYING' % v)})

    if not have(deps, 'lua'):
        v = setup_var('LUA_VERSION')
        with open(os.path.join(prefix, 'include', 'lua.h')) as f:
            notice = f.read().split('/******************************************************************************', 1)[1]
        fetch_component(deps, 'lua', {
            'name': 'Lua', 'version': v, 'license': 'MIT',
            'source': 'https://www.lua.org/ftp/lua-%s.tar.gz' % v, 'sha256': setup_var('LUA_SHA256'),
            'note': 'built from source by macos/setup-deps.sh, linked into norns'}, {
            'LICENSE': notice.replace('******************************************************************************/', '').strip() + '\n'})

    if not have(deps, 'nng'):
        tag = setup_var('NNG_TAG')
        tgz = get('https://github.com/nanomsg/nng/archive/refs/tags/%s.tar.gz' % tag)
        fetch_component(deps, 'nng', {
            'name': 'nng', 'version': tag, 'license': 'MIT',
            'source': 'https://github.com/nanomsg/nng/archive/refs/tags/%s.tar.gz' % tag, 'sha256': sha256(tgz),
            'note': 'built from source by macos/setup-deps.sh, linked into norns'}, {
            'LICENSE.txt': get('https://raw.githubusercontent.com/nanomsg/nng/%s/LICENSE.txt' % tag)})

    if not have(deps, 'libmonome'):
        c = setup_var('LIBMONOME_COMMIT')
        fetch_component(deps, 'libmonome', {
            'name': 'libmonome', 'version': c[:12], 'license': 'ISC',
            'source': 'https://github.com/monome/libmonome/archive/%s.tar.gz' % c, 'commit': c,
            'note': 'built from source by macos/setup-deps.sh (--enable-embedded-protos)'}, {
            'COPYRIGHT': get('https://raw.githubusercontent.com/monome/libmonome/%s/COPYRIGHT' % c)})

    if not have(deps, 'supercollider'):
        tag = setup_var('SC_TAG')
        v = tag.replace('Version-', '')
        url = 'https://github.com/supercollider/supercollider/releases/download/%s/SuperCollider-%s-Source.tar.bz2' % (tag, v)
        data = get(url)
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:bz2') as tf:
            parts = license_members(tf)
        copying = [t for p, t in parts if p == 'COPYING']
        fetch_component(deps, 'supercollider', {
            'name': 'SuperCollider', 'version': v, 'license': 'GPL-3.0-or-later (with the libraries in its source under theirs)',
            'source': url, 'sha256': sha256(data),
            'note': 'built from source by macos/setup-deps.sh (jack backend, no IDE)'}, {
            'COPYING': copying[0] if copying else b'',
            'THIRD-PARTY.txt': concat([(p, t) for p, t in parts if p != 'COPYING'])})

    if not have(deps, 'sc3-plugins'):
        c = setup_var('SC3_PLUGINS_COMMIT')
        url = 'https://github.com/supercollider/sc3-plugins/archive/%s.tar.gz' % c
        with tarfile.open(fileobj=io.BytesIO(get(url)), mode='r:gz') as tf:
            parts = license_members(tf)
        fetch_component(deps, 'sc3-plugins', {
            'name': 'sc3-plugins', 'version': c[:12], 'license': 'GPL-2.0-or-later (some plugins under their own)',
            'source': url, 'commit': c,
            'note': 'built from source by macos/setup-deps.sh; source/NCAnalysisUGens/SMS.cpp: a bounds check '
                    'clang rejects (0<=bufnum<= n) made bufnum < n'}, {
            'license-files.txt': concat(parts)})

    # homebrew bottles that carry no license file: the text from their source
    bottles = sorted(glob.glob(os.path.join(deps, 'bottles', '*', 'manifest.json')))
    if bottles:
        with open(bottles[-1]) as f:
            manifest = json.load(f)
        for name, url in BOTTLE_LICENSE_URLS.items():
            if name in manifest and not have(deps, 'bottle-' + name):
                v = manifest[name]['version'].split('_')[0]
                fetch_component(deps, 'bottle-' + name, {'name': name, 'version': v, 'license': manifest[name]['license'],
                                                         'source': manifest[name]['source']},
                                {'LICENSE': get(url.format(version=v))})

    maiden_commit = setup_var('MAIDEN_COMMIT')
    if not have(deps, 'maiden') or not have(deps, 'maiden-go') or not have(deps, 'maiden-web'):
        url = 'https://github.com/monome/maiden/archive/%s.tar.gz' % maiden_commit
        tmp = tempfile.mkdtemp(prefix='norns-licenses-')
        try:
            with tarfile.open(fileobj=io.BytesIO(get(url)), mode='r:gz') as tf:
                tf.extractall(tmp, filter='data')
            src = glob.glob(os.path.join(tmp, 'maiden-*'))[0]
            if not have(deps, 'maiden'):
                fetch_component(deps, 'maiden', {
                    'name': 'maiden', 'version': maiden_commit[:12], 'license': 'GPL-3.0',
                    'source': url, 'commit': maiden_commit,
                    'note': 'built by macos/setup-deps.sh with macos/patches/maiden-host-flag.patch (--host: listen on 127.0.0.1)'}, {
                    'LICENSE': open(os.path.join(src, 'LICENSE'), 'rb').read()})
            if not have(deps, 'maiden-go'):
                fetch_go(deps, src, tmp)
            if not have(deps, 'maiden-web'):
                fetch_web(deps, src, tmp)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    print('licenses: in %s' % os.path.join(deps, 'licenses'))


def fetch_go(deps, src, tmp):
    """the Go modules maiden is built from (go list -m all of its source) and Go itself"""
    env = dict(os.environ, GOMODCACHE=os.path.join(tmp, 'gomod'), GOCACHE=os.path.join(tmp, 'gocache'),
               GOFLAGS='-mod=mod', GOTOOLCHAIN='local')
    out = subprocess.run(['go', 'list', '-deps', '-f', '{{with .Module}}{{.Path}} {{.Version}} {{.Dir}}{{end}}', './...'],
                         cwd=src, env=env, capture_output=True, text=True, check=True).stdout
    mods = sorted({tuple(l.split(' ', 2)) for l in out.splitlines() if l.strip() and not l.startswith('github.com/monome/maiden')})
    parts = []
    for path, ver, d in mods:
        lic = [f for f in sorted(os.listdir(d)) if LICENSE_RE.match(f)] if os.path.isdir(d) else []
        if not lic:
            raise SystemExit('licenses: Go module %s %s has no license file' % (path, ver))
        for f in lic:
            parts.append(('%s %s/%s' % (path, ver, f), open(os.path.join(d, f), 'rb').read()))
    goroot = subprocess.run(['go', 'env', 'GOROOT'], capture_output=True, text=True, check=True).stdout.strip()
    # homebrew's go keeps it next to libexec, the go distribution in GOROOT
    for cand in (os.path.join(goroot, 'LICENSE'), os.path.join(os.path.dirname(goroot), 'LICENSE')):
        if os.path.isfile(cand):
            parts.append(('Go (runtime and standard library)/LICENSE', open(cand, 'rb').read()))
            if os.path.isfile(os.path.join(os.path.dirname(cand), 'PATENTS')) or os.path.isfile(os.path.join(goroot, 'PATENTS')):
                pat = os.path.join(goroot, 'PATENTS')
                parts.append(('Go (runtime and standard library)/PATENTS', open(pat, 'rb').read()))
            break
    else:
        raise SystemExit('licenses: no LICENSE of Go in %s' % goroot)
    fetch_component(deps, 'maiden-go', {
        'name': 'maiden: Go modules', 'version': '%d modules' % len(mods), 'license': 'see GO-MODULES.txt (MIT, BSD, Apache-2.0 ...)',
        'source': 'https://proxy.golang.org (module path and version in GO-MODULES.txt)'}, {
        'GO-MODULES.txt': concat(parts)})


def fetch_web(deps, src, tmp):
    """the npm packages in maiden's web app: the runtime dependencies of web/package.json"""
    web = os.path.join(src, 'web')
    # caches in the temporary folder: tens of thousands of files, not for the project folder
    env = dict(os.environ, npm_config_cache=os.path.join(tmp, 'npm'), YARN_CACHE_FOLDER=os.path.join(tmp, 'yarn'),
               npm_config_update_notifier='false')
    # npx from the node setup-deps.sh fetched for maiden (no system node needed)
    node = os.path.join(deps, 'src', 'node16', 'bin')
    if os.path.isdir(node):
        env['PATH'] = node + os.pathsep + env.get('PATH', '')
    subprocess.run(['npx', '-y', 'yarn@1.22.22', 'install', '--frozen-lockfile', '--ignore-scripts', '--ignore-engines',
                    '--production', '--non-interactive', '--silent'], cwd=web, check=True, env=env,
                   stdout=subprocess.DEVNULL)
    with open(os.path.join(web, 'package.json')) as f:
        top = json.load(f)
    # the build tool (react-scripts) is not in the app, what it builds from is
    todo = [n for n in top.get('dependencies', {}) if n != 'react-scripts']
    seen, parts = set(), []
    while todo:
        n = todo.pop()
        if n in seen:
            continue
        seen.add(n)
        d = os.path.join(web, 'node_modules', n)
        pj = os.path.join(d, 'package.json')
        if not os.path.isfile(pj):
            continue
        with open(pj) as f:
            meta = json.load(f)
        todo += list(meta.get('dependencies', {}))
        lic = [f for f in sorted(os.listdir(d)) if LICENSE_RE.match(f) and os.path.isfile(os.path.join(d, f))]
        texts = [(f, open(os.path.join(d, f), 'rb').read()) for f in lic]
        if not texts:
            # some packages state it only in package.json
            texts = [('package.json license', ('license: %s\n' % json.dumps(meta.get('license', meta.get('licenses', '?')))).encode())]
        for f, t in texts:
            parts.append(('%s %s/%s' % (n, meta.get('version', '?'), f), t))
    fetch_component(deps, 'maiden-web', {
        'name': 'maiden: web packages', 'version': '%d packages' % len(seen), 'license': 'see WEB-PACKAGES.txt (MIT, BSD, ISC ...)',
        'source': 'https://registry.npmjs.org (name and version in WEB-PACKAGES.txt; versions from maiden\'s web/yarn.lock)'}, {
        'WEB-PACKAGES.txt': concat(sorted(parts))})


#--- collect

# parts of the norns repository with their own license, compiled into norns
REPO_PARTS = [
    ('norns', 'norns, with Verdandi (the macOS port)', 'GPL-3.0', ['LICENSE.txt']),
    ('softcut', 'softcut', 'GPL-3.0', ['crone/softcut/LICENSE.txt']),
    ('ableton-link', 'Ableton Link', 'GPL-2.0-or-later', ['third-party/link/LICENSE.md',
                                                          'third-party/link/modules/asio-standalone/asio/LICENSE_1_0.txt']),
    ('lua-cjson', 'lua-cjson', 'MIT', ['third-party/lua-cjson/LICENSE']),
    ('concurrentqueue', 'concurrentqueue', 'BSD-2-Clause or BSL-1.0', ['third-party/concurrentqueue/LICENSE.md']),
    ('readerwriterqueue', 'readerwriterqueue', 'BSD-2-Clause', ['third-party/readerwriterqueue/LICENSE.md']),
    ('fonts', 'fonts of the norns screen', 'see the files', ['resources/roboto-LICENSE.txt', 'resources/vera-LICENSE.txt',
                                                            'resources/bmp/bmp-LICENSE.txt', 'resources/bmp/tom-thumb-LICENSE.txt']),
]
# built here, by the files they put into the app
BUILT = ['jack', 'lua', 'nng', 'libmonome', 'supercollider', 'sc3-plugins', 'maiden', 'maiden-go', 'maiden-web']
# the GNU tools in deps/gnubin
GNU = ['coreutils', 'findutils', 'gnu-sed', 'grep']


def license_text(lic):
    if isinstance(lic, str):
        return lic
    return json.dumps(lic)


def bottle_index(bottles):
    """library name -> formula"""
    index = {}
    for d, _, names in os.walk(bottles):
        rel = os.path.relpath(d, bottles).split(os.sep)
        for n in names:
            if '.dylib' in n or n.endswith('.so'):
                index.setdefault(n, rel[0])
    return index


def collect(deps, bottles, repo, res, out):
    with open(os.path.join(bottles, 'manifest.json')) as f:
        manifest = json.load(f)
    index = bottle_index(bottles)
    used, unknown = set(GNU), []
    for d, _, names in os.walk(os.path.join(res, 'deps', 'lib')):
        for n in names:
            if n == 'libSDL3.dylib':
                used.add('sdl3')
            elif n.startswith('libmonome'):
                pass
            elif n in index:
                used.add(index[n])
            elif n.startswith('jack_') or n.startswith('libjack'):
                pass
            else:
                unknown.append(n)
    if unknown:
        raise SystemExit('licenses: bundled libraries of no known package: %s' % ', '.join(sorted(unknown)))
    used.discard('jack')  # built here

    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    rows = []

    for name, title, lic, files in REPO_PARTS:
        for rel in files:
            p = os.path.join(repo, rel)
            if not os.path.isfile(p):
                raise SystemExit('licenses: %s missing' % rel)
            write(os.path.join(out, name, os.path.basename(rel)), open(p, 'rb').read())
        rows.append((title, 'this release', lic, 'the source of this release (macos/, readme-macos.md)', ''))

    for name in BUILT:
        d = os.path.join(deps, 'licenses', name)
        if not os.path.isfile(os.path.join(d, 'source.json')):
            raise SystemExit('licenses: %s has not been fetched (macos/setup-deps.sh)' % name)
        with open(os.path.join(d, 'source.json')) as f:
            meta = json.load(f)
        for fn in os.listdir(d):
            if fn != 'source.json':
                write(os.path.join(out, name, fn), open(os.path.join(d, fn), 'rb').read())
        check = meta.get('sha256') and 'sha256 ' + meta['sha256'] or (meta.get('commit') and 'commit ' + meta['commit']) or ''
        rows.append((meta['name'], meta['version'], meta['license'], meta['source'] + ('  -- ' + meta['note'] if meta.get('note') else ''), check))

    for name in sorted(used):
        m = manifest.get(name)
        if m is None:
            raise SystemExit('licenses: %s is not in the bottles manifest' % name)
        src = glob.glob(os.path.join(bottles, name, '*'))[0]
        lic = [f for f in os.listdir(src) if os.path.isfile(os.path.join(src, f)) and LICENSE_RE.match(f)]
        for f in lic:
            write(os.path.join(out, name, f), open(os.path.join(src, f), 'rb').read())
        if not lic:
            # a few bottles carry no license file: the text fetched from their source
            extra = os.path.join(deps, 'licenses', 'bottle-' + name, 'LICENSE')
            if not os.path.isfile(extra):
                raise SystemExit('licenses: the %s bottle has no license file (BOTTLE_LICENSE_URLS)' % name)
            write(os.path.join(out, name, 'LICENSE'), open(extra, 'rb').read())
        rows.append((name, m['version'], license_text(m['license']),
                     '%s  -- homebrew bottle, built by %s' % (m['source'], m['formula']), 'sha256 ' + m['sha256']))

    w = max(len(r[0]) for r in rows)
    lines = ['%-*s  %-16s  %s\n%*s  source: %s%s\n' % (w, r[0], r[1], r[2], w, '', r[3], '\n%*s  %s' % (w, '', r[4]) if r[4] else '')
             for r in rows]
    write(os.path.join(out, 'PACKAGES.txt'),
          'what Verdandi.app contains, with its version, license and where its source is.\n'
          'the license texts are in the folder of the same name next to this file.\n\n' + '\n'.join(lines))
    write(os.path.join(out, 'README.txt'), README)
    print('licenses: %d packages in %s' % (len(rows), out))


README = '''licenses of Verdandi (norns on macOS)

Verdandi.app contains free software by many people, each part under its own license. PACKAGES.txt
lists every part: its version, its license and where its exact source is (with a checksum or a
commit). each folder here holds the license texts of one part.

corresponding source: the source of every part is at the address in PACKAGES.txt; the parts
from homebrew are built by the homebrew formula named there. the source of the port itself
(norns with the macOS changes, the build scripts macos/setup-deps.sh and macos/make-app.sh that
fetch, build and bundle everything listed, and the patches applied) is published with each
release. the GNU General Public License gives you the right to get, study, change and share
all of it.

Verdandi runs norns, by monome and its community. norns and monome are names of monome;
Verdandi is an independent community project, not endorsed by or affiliated with monome.
'''


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'fetch':
        fetch(os.path.abspath(sys.argv[2]))
    elif len(sys.argv) >= 7 and sys.argv[1] == 'collect':
        collect(*[os.path.abspath(a) for a in sys.argv[2:7]])
    else:
        print(__doc__)
        sys.exit(2)


main()
