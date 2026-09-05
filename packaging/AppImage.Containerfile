FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install --yes --no-install-recommends \
    appstream \
    build-essential \
    ca-certificates \
    ccache \
    clang \
    curl \
    file \
    git \
    glslang-dev \
    libasound2-dev \
    libbz2-dev \
    libdbus-1-dev \
    libdecor-0-dev \
    libfreetype-dev \
    libgl1-mesa-dev \
    libglfw3-dev \
    libogg-dev \
    libopengl-dev \
    libopus-dev \
    libopusfile-dev \
    libpng-dev \
    libspdlog-dev \
    libtinyxml2-dev \
    libusb-1.0-0-dev \
    libudev-dev \
    libvorbis-dev \
    libwayland-dev \
    libx11-dev \
    libxcursor-dev \
    libxext-dev \
    libxfixes-dev \
    libxi-dev \
    libxkbcommon-dev \
    libxrandr-dev \
    libxss-dev \
    libxtst-dev \
    libzip-dev \
    lsb-release \
    ninja-build \
    nlohmann-json3-dev \
    patchelf \
    pkg-config \
    python3 \
    python3-pip \
    zipcmp \
    zipmerge \
    ziptool \
    zlib1g-dev

ARG CMAKE_VERSION=3.31.10
ARG UV_VERSION=0.10.12
RUN python3 -m pip install --no-cache-dir "cmake==${CMAKE_VERSION}" "uv==${UV_VERSION}"

# The Python source/build owner and exact revisions are shared with Linux CI.
# The release orchestrator supplies only the four audited builder files as context.
COPY launcher_bootstrap /opt/zelda3d-native/launcher_bootstrap
COPY tools/prepare_native_sources.py /opt/zelda3d-native/tools/prepare_native_sources.py
ENV CC=clang CXX=clang++
RUN python3 /opt/zelda3d-native/tools/prepare_native_sources.py \
    --root /opt/build/native --prefix /usr/local tinyxml2 glslang sdl

ARG LINUXDEPLOY_SHA256=421ca71d5c69ea97c6309276232990d43df1dcece0edfaa26bbf926ff96ed12e
RUN curl --fail --location --show-error \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
        --output /usr/local/bin/linuxdeploy \
    && echo "${LINUXDEPLOY_SHA256}  /usr/local/bin/linuxdeploy" | sha256sum --check \
    && chmod 0755 /usr/local/bin/linuxdeploy

ARG APPIMAGETOOL_SHA256=a6d71e2b6cd66f8e8d16c37ad164658985e0cf5fcaa950c90a482890cb9d13e0
RUN curl --fail --location --show-error \
        "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
        --output /usr/local/bin/appimagetool \
    && echo "${APPIMAGETOOL_SHA256}  /usr/local/bin/appimagetool" | sha256sum --check \
    && chmod 0755 /usr/local/bin/appimagetool

WORKDIR /src
