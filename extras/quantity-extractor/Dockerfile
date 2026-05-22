# Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
ARG RTTR_REVISION=0000000000000000000000000000000000000000

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        gettext \
        git \
        libboost-filesystem-dev \
        libboost-iostreams-dev \
        libboost-locale-dev \
        libboost-nowide-dev \
        libboost-program-options-dev \
        libbz2-dev \
        libcurl4-openssl-dev \
        liblua5.2-dev \
        libminiupnpc-dev \
        libprotobuf-dev \
        libsamplerate0-dev \
        libsdl2-dev \
        libsdl2-mixer-dev \
        libyaml-cpp-dev \
        nlohmann-json3-dev \
        protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S /src -B /src/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/rttr \
        -DRTTR_REVISION="$RTTR_REVISION" \
        -DBUILD_TESTING=OFF \
        -DRTTR_BUILD_UPDATER=OFF \
        -DRTTR_USE_SYSTEM_LIBS=ON \
    && cmake --build /src/build --target quantity-extractor --parallel

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libboost-filesystem1.83.0 \
        libboost-iostreams1.83.0 \
        libboost-locale1.83.0 \
        libboost-nowide1.83.0 \
        libbz2-1.0 \
        libcurl4t64 \
        liblua5.2-0 \
        libminiupnpc17 \
        libprotobuf32t64 \
        libsamplerate0 \
        libsdl2-2.0-0 \
        libsdl2-mixer-2.0-0 \
        libyaml-cpp0.8 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/bin/quantity-extractor /opt/rttr/bin/quantity-extractor
COPY --from=build /src/data/RTTR /opt/rttr/share/s25rttr/RTTR

WORKDIR /work
VOLUME ["/work"]

ENTRYPOINT ["/opt/rttr/bin/quantity-extractor"]
