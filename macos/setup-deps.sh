#!/bin/bash
# setup-deps.sh: build everything norns needs on macOS that homebrew does not provide.
#
# result: <deps>/prefix (lua 5.3, nng 1.x, libmonome, SuperCollider.app with the jack
# backend), <deps>/bin/waf (waf 2.1; the bundled waf 2.0 needs python's removed `imp`)
# sc3-plugins in the norns home's SuperCollider extensions, and maiden in <norns home>/maiden.
#
# usage: macos/setup-deps.sh [deps dir, default: <norns home>/../../deps]
# paths must not contain spaces (lua's makefile); use a symlink if the disk name has one.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
NORNS_HOME="$(dirname "$REPO")"
DEPS="${1:-$(cd "$NORNS_HOME/../.." && pwd)/deps}"
P="$DEPS/prefix"
SRC="$DEPS/src"
if [ "$(uname -m)" = arm64 ]; then BREW_DEFAULT=/opt/homebrew; else BREW_DEFAULT=/usr/local; fi
BREW="${HOMEBREW_PREFIX:-$BREW_DEFAULT}"
J=$(sysctl -n hw.ncpu)

JACK_VERSION=1.9.22
JACK_SHA256=1e42b9fc4ad7db7befd414d45ab2f8a159c0b30fcd6eee452be662298766a849
LUA_VERSION=5.3.6
LUA_SHA256=fc5fd69bb8736323f026672b1b7235da613d7177e72558893a0bdcd320466d60
WAF_VERSION=2.1.9
SC_TAG=Version-3.14.1
# exact sources of what is built here (macos/licenses.py lists them in the app)
NNG_TAG=v1.12.4
LIBMONOME_COMMIT=9055ea22fd824839be821c10f6e9b13e9d2aa3cd
SC3_PLUGINS_COMMIT=b80a41b731922feac7755df5ba359d14dfb8f4d9
MAIDEN_COMMIT=2e64f7d59284962ac68055dd556e3fd8515462f8
# maiden's web app (react-scripts 1.x) needs the node its own tooling pins
NODE_VERSION=v16.20.2

case "$DEPS" in *" "*) echo "setup-deps: '$DEPS' contains a space"; exit 1 ;; esac
# the oldest macOS the app runs on: clang and cmake take it from here
export MACOSX_DEPLOYMENT_TARGET="$(cat "$REPO/macos/MACOS_MIN")"
# keep build paths (they contain the user name) out of the binaries
MAP="-ffile-prefix-map=$DEPS=/build/deps -ffile-prefix-map=$NORNS_HOME=/build/home"
mkdir -p "$P/lib/pkgconfig" "$SRC" "$DEPS/bin"

# a repository at one commit (with its submodules), shallow
clone_at() {
    git init -q "$2"
    git -C "$2" remote add origin "$1"
    git -C "$2" fetch -q --depth 1 origin "$3"
    git -C "$2" checkout -q FETCH_HEAD
    git -C "$2" submodule -q update --init --recursive --depth 1
}

if [ -z "${NORNS_NEUTRAL_ENV:-}" ]; then
    echo "== homebrew packages"
    brew install cairo boost cmake pkgconf readline jack liblo libsndfile sdl2 fftw yaml-cpp \
        coreutils findutils gnu-sed grep gawk go
    # the rest in a neutral environment: build tools write their environment (home, user name,
    # PATH, every variable) into their logs and caches, and git writes its identity into the
    # clones' reflogs, all inside this folder
    NEUTRAL_HOME="$DEPS/.home"
    mkdir -p "$NEUTRAL_HOME"
    # download and build caches (go, npm, yarn: tens of thousands of files) in the temporary folder
    CACHE="${TMPDIR:-/tmp}/norns-build-cache"
    exec env -i NORNS_NEUTRAL_ENV=1 HOME="$NEUTRAL_HOME" PATH="$BREW/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
        GOCACHE="$CACHE/go-build" GOMODCACHE="$CACHE/go-mod" npm_config_cache="$CACHE/npm" \
        YARN_CACHE_FOLDER="$CACHE/yarn" npm_config_update_notifier=false \
        TMPDIR="${TMPDIR:-/tmp}" LANG=en_US.UTF-8 TERM="${TERM:-dumb}" \
        GIT_AUTHOR_NAME="norns-macos contributors" GIT_AUTHOR_EMAIL="noreply@norns-macos.invalid" \
        GIT_COMMITTER_NAME="norns-macos contributors" GIT_COMMITTER_EMAIL="noreply@norns-macos.invalid" \
        HOMEBREW_PREFIX="$BREW" /bin/bash "$0" "$DEPS"
fi

echo "== homebrew bottles for macOS $MACOSX_DEPLOYMENT_TARGET (what make-app.sh bundles)"
python3 -I "$REPO/macos/fetch_bottles.py" "$DEPS/bottles"

BOTTLES="$(python3 -I "$REPO/macos/fetch_bottles.py" --where "$DEPS/bottles")"

# built for another oldest macOS: build it again
STAMP="$P/.macos-min"
if [ "$(cat "$STAMP" 2>/dev/null)" != "$MACOSX_DEPLOYMENT_TARGET" ]; then
    echo "== oldest macOS is now $MACOSX_DEPLOYMENT_TARGET: rebuilding lua, nng, libmonome, SuperCollider, sc3-plugins"
    rm -rf "$P/lib/liblua.a" "$P/lib/libnng.a" "$P/include/monome.h" "$P/SuperCollider" \
        "$SRC/lua-$LUA_VERSION" "$SRC/nng/build" "$SRC/libmonome/build" "$SRC/supercollider/build" \
        "$SRC/sc3-plugins/build" "$NORNS_HOME/Library/Application Support/SuperCollider/Extensions/SC3plugins" \
        "$BOTTLES/jack"/*-source "$SRC/jack2-$JACK_VERSION/build"
fi

echo "== jack $JACK_VERSION for macOS $MACOSX_DEPLOYMENT_TARGET (homebrew's bottle needs a newer one)"
JACK_OUT="$BOTTLES/jack/$JACK_VERSION-source"
if [ ! -x "$JACK_OUT/bin/jackd" ]; then
    cd "$SRC"
    if [ ! -d "jack2-$JACK_VERSION" ]; then
        curl -sSfL -o "jack2-$JACK_VERSION.tar.gz" "https://github.com/jackaudio/jack2/archive/refs/tags/v$JACK_VERSION.tar.gz"
        echo "$JACK_SHA256  jack2-$JACK_VERSION.tar.gz" | shasum -a 256 -c -
        tar xzf "jack2-$JACK_VERSION.tar.gz" && rm "jack2-$JACK_VERSION.tar.gz"
    fi
    cd "jack2-$JACK_VERSION"
    # as homebrew builds it (no metadata: it needs Berkeley DB). a neutral prefix: it ends up
    # in the binaries, and make-app.sh points the libraries at the bundle anyway
    # its waf 2.0 needs python's imp module, gone since 3.12: the system python has it
    CFLAGS="-I$BREW/include $MAP" CXXFLAGS="-I$BREW/include $MAP" LDFLAGS="-L$BREW/lib" \
        PKG_CONFIG_PATH="$BREW/lib/pkgconfig" /usr/bin/python3 ./waf configure --prefix=/build/jack --db=no
    /usr/bin/python3 ./waf build -j "$J"
    rm -rf "$SRC/jack-stage" && /usr/bin/python3 ./waf install --destdir="$SRC/jack-stage"
    rm -rf "$JACK_OUT" && mkdir -p "$(dirname "$JACK_OUT")" && mv "$SRC/jack-stage/build/jack" "$JACK_OUT"
    rm -rf "$SRC/jack-stage" "$JACK_OUT/include" "$JACK_OUT/share"
fi

echo "== waf $WAF_VERSION"
[ -x "$DEPS/bin/waf" ] || { curl -sSfL -o "$DEPS/bin/waf" "https://waf.io/waf-$WAF_VERSION" && chmod +x "$DEPS/bin/waf"; }

echo "== lua $LUA_VERSION (C linkage)"
if [ ! -f "$P/lib/liblua.a" ]; then
    cd "$SRC"
    curl -sSfLO "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
    echo "$LUA_SHA256  lua-$LUA_VERSION.tar.gz" | shasum -a 256 -c -
    tar xzf "lua-$LUA_VERSION.tar.gz"
    make -C "lua-$LUA_VERSION" macosx MYCFLAGS="-fPIC -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
    make -C "lua-$LUA_VERSION" install INSTALL_TOP="$P"
fi
cat > "$P/lib/pkgconfig/lua53.pc" <<EOF
prefix=$P
Name: Lua
Description: Lua 5.3
Version: $LUA_VERSION
Libs: -L\${prefix}/lib -llua -lm
Cflags: -I\${prefix}/include
EOF

echo "== nng 1.x (norns uses the 1.x api; homebrew has 2.x)"
if [ ! -f "$P/lib/libnng.a" ]; then
    cd "$SRC"
    [ -d nng ] || git clone -q --depth 1 -b "$NNG_TAG" https://github.com/nanomsg/nng.git
    cmake -S nng -B nng/build -DCMAKE_INSTALL_PREFIX="$P" -DNNG_TESTS=OFF -DNNG_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build nng/build -j "$J"
    cmake --install nng/build
fi

echo "== libmonome"
if [ ! -f "$P/include/monome.h" ]; then
    cd "$SRC"
    [ -d libmonome ] || clone_at https://github.com/monome/libmonome.git libmonome "$LIBMONOME_COMMIT"
    cd libmonome
    # embedded protocols: otherwise libmonome dlopens them from this build's absolute prefix
    CFLAGS="-I$BREW/include $MAP" LDFLAGS="-L$BREW/lib" PKG_CONFIG_PATH="$P/lib/pkgconfig:$BREW/lib/pkgconfig" \
        python3 ./waf configure --prefix="$P" --enable-embedded-protos
    python3 ./waf
    python3 ./waf install
fi

echo "== SuperCollider $SC_TAG (jack backend, no IDE)"
SC_APP="$P/SuperCollider/SuperCollider.app/Contents"
if [ ! -x "$SC_APP/MacOS/sclang" ]; then
    cd "$SRC"
    [ -d supercollider ] || git clone -q --depth 1 -b "$SC_TAG" --recurse-submodules --shallow-submodules https://github.com/supercollider/supercollider.git
    # system boost/yaml-cpp: homebrew's headers are on the include path anyway, so the
    # bundled copies would be compiled against the wrong headers
    cmake -S supercollider -B supercollider/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
        -DAUDIOAPI=jack -DSC_QT=OFF -DSC_IDE=OFF -DSUPERNOVA=OFF -DSC_EL=OFF -DSC_VIM=OFF -DSC_ED=OFF \
        -DNO_X11=ON -DNATIVE=OFF -DSC_ABLETON_LINK=OFF -DINSTALL_HELP=OFF -DENABLE_TESTSUITE=OFF \
        -DSYSTEM_BOOST=ON -DSYSTEM_YAMLCPP=ON -DCMAKE_PREFIX_PATH="$BREW;$BREW/opt/readline" \
        -DCMAKE_C_FLAGS="$MAP" -DCMAKE_CXX_FLAGS="$MAP"
    cmake --build supercollider/build -j "$J"
    cmake --install supercollider/build
fi
# without the qt ide the install skips the class library
[ -d "$SC_APP/Resources/SCClassLibrary" ] || cp -R "$SRC/supercollider/SCClassLibrary" "$SC_APP/Resources/"

echo "== sc3-plugins"
EXT="$NORNS_HOME/Library/Application Support/SuperCollider/Extensions"
if [ ! -d "$EXT/SC3plugins" ]; then
    cd "$SRC"
    [ -d sc3-plugins ] || clone_at https://github.com/supercollider/sc3-plugins.git sc3-plugins "$SC3_PLUGINS_COMMIT"
    # `0<=bufnum<= n` is always true, and clang 21 rejects it; the intended bounds check
    sed -i '' 's/	if(0<=bufnum<= world->mNumSndBufs) {/	if(bufnum < (int)world->mNumSndBufs) {/' \
        sc3-plugins/source/NCAnalysisUGens/SMS.cpp
    cmake -S sc3-plugins -B sc3-plugins/build -DSC_PATH="$SRC/supercollider" -DCMAKE_BUILD_TYPE=Release \
        -DSUPERNOVA=OFF -DQUARKS=OFF -DSCX=ON -DCMAKE_PREFIX_PATH="$BREW" \
        -DCMAKE_C_FLAGS="$MAP" -DCMAKE_CXX_FLAGS="$MAP"
    cmake --build sc3-plugins/build -j "$J"
    # on macOS the install step always lands in build/SC3plugins
    cmake --install sc3-plugins/build
    mkdir -p "$EXT"
    cp -R sc3-plugins/build/SC3plugins "$EXT/"
    # help files are most of the size and unused at runtime
    find "$EXT/SC3plugins" -type d -name HelpSource -prune -exec rm -rf {} +
fi

echo "== maiden"
M="$NORNS_HOME/maiden"
if [ ! -x "$M/maiden" ]; then
    cd "$SRC"
    if [ ! -x node16/bin/node ]; then
        case "$(uname -m)" in arm64) NODE_ARCH=arm64 ;; *) NODE_ARCH=x64 ;; esac
        F="node-$NODE_VERSION-darwin-$NODE_ARCH.tar.gz"
        curl -sSfLO "https://nodejs.org/dist/$NODE_VERSION/$F"
        curl -sSfL "https://nodejs.org/dist/$NODE_VERSION/SHASUMS256.txt" | grep " $F\$" | shasum -a 256 -c -
        mkdir -p node16 && tar xzf "$F" -C node16 --strip-components 1 && rm "$F"
    fi
    [ -d maiden ] || clone_at https://github.com/monome/maiden.git maiden "$MAIDEN_COMMIT"
    # --host: listen on 127.0.0.1 only (maiden edits files and fronts code-running REPLs)
    git -C maiden apply --check "$REPO/macos/patches/maiden-host-flag.patch" 2>/dev/null &&
        git -C maiden apply "$REPO/macos/patches/maiden-host-flag.patch"
    (cd maiden && GOFLAGS=-mod=mod go build -trimpath -ldflags="-X github.com/monome/maiden/cmd.version=$(git describe --tags 2>/dev/null || echo head)-macos" -o maiden .)
    (cd maiden/web && PATH="$SRC/node16/bin:$PATH" npx -y yarn@1.22.22 install --frozen-lockfile &&
        PATH="$SRC/node16/bin:$PATH" npx -y yarn@1.22.22 build)
    mkdir -p "$M/app" "$M/dist/sources"
    cp maiden/maiden maiden/maiden.yaml "$M/"
    rm -rf "$M/app/build" && cp -R maiden/web/build "$M/app/"
    cp maiden/sources/*.json "$M/dist/sources/"
fi

echo "== licenses and sources of the bundled packages (macos/licenses.py)"
python3 -I "$REPO/macos/licenses.py" fetch "$DEPS"

echo "$MACOSX_DEPLOYMENT_TARGET" > "$STAMP"
echo "== done. build norns: $REPO/macos/build.sh"
