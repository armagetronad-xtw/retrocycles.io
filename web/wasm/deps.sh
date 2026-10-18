#!/usr/bin/env bash
# Builds the wasm dependencies Emscripten has no port for, into /work/deps/wasm on the
# aa-work volume. Runs inside the emsdk image, never on the host:
#   docker run --rm -v aa-work:/work -v aa-emcache:/emsdk/upstream/emscripten/cache \
#       emscripten/emsdk:latest bash /work/aa/web/wasm/deps.sh
# Idempotent: skips what is already installed. SDL2, SDL2_image, libpng and zlib come
# from --use-port at compile time (see Makefile).
set -euo pipefail

: "${AA_WASM_DEPS:=/work/deps/wasm}"
: "${AA_WASM_DEPS_SRC:=/work/deps/wasm-src}"

LIBXML2_VERSION=2.12.10
LIBXML2_URL="https://download.gnome.org/sources/libxml2/2.12/libxml2-${LIBXML2_VERSION}.tar.xz"

mkdir -p "$AA_WASM_DEPS" "$AA_WASM_DEPS_SRC"

build_libxml2()
{
    if [ -f "$AA_WASM_DEPS/lib/libxml2.a" ]; then
        echo "== libxml2 already installed in $AA_WASM_DEPS"
        return
    fi

    cd "$AA_WASM_DEPS_SRC"
    if [ ! -d "libxml2-${LIBXML2_VERSION}" ]; then
        wget -q -O "libxml2-${LIBXML2_VERSION}.tar.xz" "$LIBXML2_URL"
        tar xf "libxml2-${LIBXML2_VERSION}.tar.xz"
    fi
    cd "libxml2-${LIBXML2_VERSION}"

    # Only what gParser (xmlCtxtReadIO with DTD validation, tree accessors) and the
    # linker need: no catalogs, network, python, threads, output or schemas.
    emconfigure ./configure --host=wasm32-unknown-emscripten \
        --prefix="$AA_WASM_DEPS" --disable-shared --enable-static \
        --with-minimum --with-tree --with-valid --with-regexps --with-sax1 \
        --without-python --without-zlib --without-lzma --without-threads \
        --without-http --without-ftp --without-catalog --without-iconv \
        --without-debug --without-modules
    emmake make -j"$(nproc)" libxml2.la
    emmake make install-libLTLIBRARIES install-data-am
    emmake make -C include install
    echo "== libxml2 ${LIBXML2_VERSION} installed in $AA_WASM_DEPS"
}

build_libxml2
