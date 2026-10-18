# Headless Blender for the stadium asset build (art/stadium/README.md).
#   docker build -t aa-blender art/stadium
FROM ubuntu:22.04
ARG BLENDER_VERSION=4.2.3
ARG BLENDER_SERIES=4.2
# download.blender.org answers scripted downloads with 403; a listed mirror serves the same file
ARG BLENDER_MIRROR=https://mirrors.ocf.berkeley.edu/blender
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates curl xz-utils libx11-6 libxi6 libxxf86vm1 libxfixes3 libxrender1 libgl1 \
        libxkbcommon0 libsm6 libice6 libegl1 python3 \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL ${BLENDER_MIRROR}/release/Blender${BLENDER_SERIES}/blender-${BLENDER_VERSION}-linux-x64.tar.xz \
        | tar -xJ -C /opt \
    && ln -s /opt/blender-${BLENDER_VERSION}-linux-x64/blender /usr/local/bin/blender
WORKDIR /art
ENTRYPOINT ["blender", "-b", "--factory-startup"]
