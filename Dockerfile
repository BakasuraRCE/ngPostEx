# Usage:
# $ docker build -t ngpostex .
# $ docker run -it -v $PWD/files:/root/files -v $PWD/ngPostEx.conf:/root/.ngPostEx ngpostex ARGUMENTS
#
# Or pull from GitHub Container Registry:
# $ docker pull ghcr.io/bakasurarce/ngpostex:latest
# $ docker run -it -v $PWD/files:/root/files -v $PWD/ngPostEx.conf:/root/.ngPostEx ghcr.io/bakasurarce/ngpostex ARGUMENTS

FROM debian:12-slim

RUN apt-get update && apt-get install --no-install-recommends -y \
    build-essential \
    qt6-base-dev \
    qt6-tools-dev \
    qt6-tools-dev-tools \
    libqt6core6 \
    libqt6network6 \
    libssl-dev \
    par2 \
    p7zip-full \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY . /usr/src/ngPostEx
WORKDIR /usr/src/ngPostEx/src

RUN mkdir -p /usr/src/ngPostEx/build && cd /usr/src/ngPostEx/build \
    && qmake6 ../src/ngPost_cmd.pro CONFIG+=release \
    && make -j$(nproc) \
    && cp ngPostEx /usr/local/bin/ngPostEx \
    && rm -rf /usr/src/ngPostEx/build

WORKDIR /root
VOLUME /root/files

ENTRYPOINT ["ngPostEx"]
