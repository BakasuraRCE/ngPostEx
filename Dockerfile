# Usage:
# $ docker build -t ngpostex .
# $ docker run -it -v $PWD/files:/root/files -v $PWD/ngPostEx.conf:/root/.ngPostEx ngpostex ARGUMENTS
#
# Or pull from GitHub Container Registry:
# $ docker pull ghcr.io/bakasurarce/ngpostex:latest
# $ docker run -it -v $PWD/files:/root/files -v $PWD/ngPostEx.conf:/root/.ngPostEx ghcr.io/bakasurarce/ngpostex ARGUMENTS

# ===========================================
# Stage 1: Build with Qt 6.8 LTS
# ===========================================
FROM ubuntu:24.04 AS builder

ARG QT_SPEC=6.8
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install --no-install-recommends -y \
    build-essential \
    python3 \
    python3-pip \
    libgl1-mesa-dev \
    libglib2.0-dev \
    libssl-dev \
    libdbus-1-dev \
    libfontconfig1-dev \
    libfreetype6-dev \
    libkrb5-dev \
    libx11-dev \
    libx11-xcb-dev \
    libxcb1-dev \
    libxkbcommon-dev \
    ca-certificates \
    && pip3 install --break-system-packages uv \
    && rm -rf /var/lib/apt/lists/*

RUN uv tool install aqtinstall \
    && export PATH="$HOME/.local/bin:$PATH" \
    && aqt install-qt linux desktop ${QT_SPEC} linux_gcc_64 \
       --outputdir /opt/qt

# Resolve the actual installed version dynamically
RUN QT_DIR=$(find /opt/qt -maxdepth 1 -name "6.*" -type d | sort -V | tail -1) \
    && ln -sf "${QT_DIR}/gcc_64" /opt/qt/current

ENV PATH="/opt/qt/current/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/qt/current/lib"
ENV QT_PLUGIN_PATH="/opt/qt/current/plugins"

COPY . /usr/src/ngPostEx

RUN mkdir -p /usr/src/ngPostEx/build && cd /usr/src/ngPostEx/build \
    && qmake ../src/ngPost_cmd.pro CONFIG+=release \
    && make -j$(nproc) \
    && mkdir -p /opt/qt-runtime/lib \
    && cp -a /opt/qt/current/lib/lib*.so* /opt/qt-runtime/lib/ \
    && cp -a /opt/qt/current/plugins /opt/qt-runtime/plugins

# ===========================================
# Stage 2: Minimal runtime image
# ===========================================
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install --no-install-recommends -y \
    libssl3t64 \
    libdbus-1-3 \
    libfontconfig1 \
    libfreetype6 \
    libglib2.0-0t64 \
    libgl1 \
    libopengl0 \
    libegl1 \
    libx11-6 \
    libx11-xcb1 \
    libxcb1 \
    libxkbcommon0 \
    libgssapi-krb5-2 \
    libpcre2-16-0 \
    libharfbuzz0b \
    libpng16-16t64 \
    libmd4c0 \
    par2 \
    p7zip-full \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy Qt runtime libraries and plugins
COPY --from=builder /opt/qt-runtime/lib/ /usr/local/lib/
COPY --from=builder /opt/qt-runtime/plugins/ /usr/local/lib/qt6/plugins/

# Copy the built binary
COPY --from=builder /usr/src/ngPostEx/build/ngPostEx /usr/local/bin/ngPostEx

RUN ldconfig

# Smoke test: verify binary starts and SSL is functional
RUN ngPostEx --help 2>&1 | grep -q "SSL support: yes" \
    || { echo "FAIL: SSL not enabled in Docker image"; exit 1; }

ENV LANG=C.UTF-8
ENV QT_PLUGIN_PATH=/usr/local/lib/qt6/plugins

WORKDIR /root
VOLUME /root/files

ENTRYPOINT ["ngPostEx"]
