# Builds the WebRTC-enabled dedicated server for a Debian 13 host, with the
# install prefix baked in (AA_PREFIX decides where the binary looks for its data
# and config). The result is /out/server.tar.gz, laid out relative to the prefix:
#   bin/armagetronad-dedicated          the server (rpath -> deps/libdatachannel/lib)
#   share/... etc/...                   as "make install" lays them out
#   deps/libdatachannel/lib/*.so*       libdatachannel (no media)
#   MANIFEST                            version, prefix, runtime packages, ldd output
# ZThread is linked statically, so the host only needs the packages MANIFEST
# lists; the last stage checks that on a fresh debian:13.
#
# Build context: this directory, with the sources as src.tar:
#   git archive -o web/deploy/build/src.tar HEAD
#   docker build -f web/deploy/build/server.Dockerfile -t aa-server-build web/deploy/build
#   docker create --name aa-out aa-server-build && docker cp aa-out:/out/server.tar.gz . && docker rm aa-out
# AA_PLATFORM=1 adds libcurl and is meant to go with --enable-platform in
# AA_CONFIGURE_EXTRA.
ARG BASE=debian:13

########################################
FROM ${BASE} AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake libtool pkg-config bison patch bzip2 make g++ python3 git ca-certificates \
    libxml2-dev libzthread-dev libboost-dev libprotobuf-dev protobuf-compiler \
    cmake ninja-build libssl-dev file \
    && rm -rf /var/lib/apt/lists/*
# static ZThread: without the .so dev link, -lZThread resolves to libZThread.a
RUN a="$(ls /usr/lib/*/libZThread.a 2>/dev/null | head -1)" && [ -n "$a" ] \
    && rm -f "$(dirname "$a")/libZThread.so" && echo "static zthread: $a"

ARG AA_PREFIX=/home/arma/armagetronad
ARG LDC_VERSION=v0.24.5
# libdatachannel (DataChannel + WebSocket only) installed at its final path so
# the rpath configure adds (-Wl,-rpath,<prefix>/deps/libdatachannel/lib) is right
RUN git clone --depth 1 --branch ${LDC_VERSION} --recursive \
        https://github.com/paullouisageneau/libdatachannel.git /tmp/libdatachannel \
    && cmake -S /tmp/libdatachannel -B /tmp/libdatachannel/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${AA_PREFIX}/deps/libdatachannel \
        -DNO_MEDIA=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON \
    && cmake --build /tmp/libdatachannel/build -j"$(nproc)" \
    && cmake --install /tmp/libdatachannel/build \
    && rm -rf /tmp/libdatachannel

# the gPlatform layer's libcurl, only with AA_PLATFORM=1
ARG AA_PLATFORM=0
RUN if [ "$AA_PLATFORM" = 1 ]; then \
        apt-get update && apt-get install -y --no-install-recommends libcurl4-openssl-dev \
        && rm -rf /var/lib/apt/lists/* && echo "platform build: libcurl4-openssl-dev installed"; \
    else echo "platform build: off"; fi

# the sources: src.tar has no .git inside, so the version string is passed in
# (batch/make/version honours CI_COMMIT_TAG)
ARG AA_VERSION=0.2.9_alpha_webrtc
ARG AA_COMMIT=unknown
ARG AA_BRANCH=unknown
# extra configure flags; empty = the community flags + --enable-webrtc only
ARG AA_CONFIGURE_EXTRA=
ADD src.tar /src
WORKDIR /src
# batch/make/version runs at bootstrap and again from make (nTrueVersion.h): the CI
# variables make it print AA_VERSION instead of a date stamp
ENV CI_COMMIT_REF_PROTECTED=true CI_COMMIT_TAG=${AA_VERSION}
RUN ./bootstrap.sh >/tmp/bootstrap.log 2>&1 || { tail -50 /tmp/bootstrap.log; exit 1; } \
    && mkdir -p /build && cd /build \
    && /src/configure --prefix=${AA_PREFIX} \
        --disable-glout --enable-authentication --with-zthread \
        --disable-sysinstall --disable-useradd --disable-etc --disable-desktop --disable-uninstall \
        --enable-webrtc --with-libdatachannel=${AA_PREFIX}/deps/libdatachannel ${AA_CONFIGURE_EXTRA} \
        > /tmp/configure.log 2>&1 || { tail -50 /tmp/configure.log; exit 1; } \
    && grep -E 'installed in|Configuration|gPlatform layer' /tmp/configure.log \
    && if [ "$AA_PLATFORM" = 1 ]; then grep -q 'Build the gPlatform layer: yes' /tmp/configure.log || { echo "AA_PLATFORM=1 but configure did not enable the gPlatform layer (AA_CONFIGURE_EXTRA: ${AA_CONFIGURE_EXTRA})"; exit 1; }; fi \
    && make -j"$(nproc)" >/tmp/make.log 2>&1 || { tail -80 /tmp/make.log; exit 1; } \
    && make install DESTDIR=/out >/tmp/install.log 2>&1 || { tail -50 /tmp/install.log; exit 1; }

# the tarball: prefix tree + libdatachannel + MANIFEST
RUN set -e; P="/out${AA_PREFIX}"; BIN="$P/bin/armagetronad-dedicated"; \
    [ -x "$BIN" ]; \
    mkdir -p "$P/deps/libdatachannel/lib" && cp -a "${AA_PREFIX}"/deps/libdatachannel/lib/*.so* "$P/deps/libdatachannel/lib/"; \
    ver="$("$BIN" --version | head -1 | sed -n 's/.* version \(.*\)\.$/\1/p')"; echo "version: $ver"; [ -n "$ver" ]; \
    grep -aq WEBRTC_SIGNAL_BIND "$BIN"; \
    if grep -aq PLATFORM_API_URL "$BIN"; then plat=yes; else plat=no; fi; echo "platform: $plat"; \
    if [ "$AA_PLATFORM" = 1 ]; then [ "$plat" = yes ]; else [ "$plat" = no ]; fi; \
    # the config directory (settings.cfg etc.) as configure laid it out, relative to the prefix
    cfgdir="$(sed -n 's/^ *Configuration will be installed in *: *//p' /tmp/configure.log | head -1 | sed "s|\${prefix}|${AA_PREFIX}|")"; \
    cfgrel="${cfgdir#${AA_PREFIX}/}"; echo "config dir: $cfgdir -> $cfgrel"; \
    [ -n "$cfgrel" ] && [ "$cfgrel" != "$cfgdir" ] && [ -d "$P/$cfgrel" ] || { echo "config dir not under the prefix / not installed"; ls "$P"; exit 1; }; \
    datadir="$(sed -n 's/^ *Static data will be installed in *: *//p' /tmp/configure.log | head -1 | sed "s|\${prefix}|${AA_PREFIX}|")"; \
    # runtime packages: every shared library the binary + libdatachannel load that is not shipped under the prefix
    libs="$(LD_LIBRARY_PATH="${AA_PREFIX}/deps/libdatachannel/lib" ldd "$BIN" "${AA_PREFIX}"/deps/libdatachannel/lib/libdatachannel.so | awk '/=> \//{print $3}' | sort -u)"; \
    ! echo "$libs" | grep -q "${AA_PREFIX}" || libs="$(echo "$libs" | grep -v "${AA_PREFIX}")"; \
    pkgs="$(for l in $libs; do dpkg -S "$(readlink -f "$l")" 2>/dev/null | cut -d: -f1; done | sort -u | tr '\n' ' ')"; \
    [ -n "$pkgs" ]; \
    zt="$(ldd "$BIN" | grep -i zthread || true)"; [ -z "$zt" ] && zt="static" || zt="SHARED: $zt"; \
    { echo "AA_VERSION=$ver"; echo "AA_BRANCH=${AA_BRANCH}"; echo "AA_COMMIT=${AA_COMMIT}"; \
      echo "AA_PREFIX=${AA_PREFIX}"; echo "AA_CONFIG_DIR=$cfgrel"; echo "AA_DATA_DIR=${datadir#${AA_PREFIX}/}"; \
      echo "AA_BUILT=$(date -u +%FT%TZ)"; echo "AA_BASE=$(. /etc/os-release && echo "$PRETTY_NAME") $(uname -m)"; \
      echo "AA_LIBDATACHANNEL=${LDC_VERSION}"; echo "AA_ZTHREAD=$zt"; echo "AA_PLATFORM=$plat"; \
      echo "AA_CONFIGURE_EXTRA=${AA_CONFIGURE_EXTRA}"; \
      echo "AA_RUNTIME_PACKAGES=${pkgs% } ca-certificates"; \
      echo "# ldd $BIN"; LD_LIBRARY_PATH="${AA_PREFIX}/deps/libdatachannel/lib" ldd "$BIN" | sed 's/^/#   /'; \
      echo "# files"; (cd "$P" && find . -type f -o -type l | sort | sed 's|^\./||'); \
    } > "$P/MANIFEST"; \
    cat "$P/MANIFEST" | head -12; \
    tar -C "$P" -czf /out/server.tar.gz . && ls -l /out/server.tar.gz

########################################
# proof: a stock debian:13 with only the MANIFEST's packages runs the binary
FROM ${BASE} AS verify
ENV DEBIAN_FRONTEND=noninteractive
ARG AA_PREFIX=/home/arma/armagetronad
COPY --from=builder /out/server.tar.gz /out/server.tar.gz
RUN mkdir -p "${AA_PREFIX}" && tar -xzf /out/server.tar.gz -C "${AA_PREFIX}" \
    && pkgs="$(sed -n 's/^AA_RUNTIME_PACKAGES=//p' "${AA_PREFIX}/MANIFEST")" && echo "runtime packages: $pkgs" \
    && apt-get update && apt-get install -y --no-install-recommends $pkgs && rm -rf /var/lib/apt/lists/* \
    && ! ldd "${AA_PREFIX}/bin/armagetronad-dedicated" | grep -q 'not found' \
    && "${AA_PREFIX}/bin/armagetronad-dedicated" --version | head -1 \
    && echo "verified on $(. /etc/os-release && echo "$PRETTY_NAME")" \
    && rm -rf "${AA_PREFIX}"
