FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libc6 \
        libstdc++6 \
        libgcc-s1 \
        libvulkan1 \
        libglfw3 \
        libx11-6 \
        libxcb1 \
        libxau6 \
        libxdmcp6 \
        libbsd0 \
        libmd0 \
    && rm -rf /var/lib/apt/lists/*

ARG PUBLISHER_BIN=build/examples/topic_publisher
COPY ${PUBLISHER_BIN} /usr/local/bin/spectra-topic-publisher

ENTRYPOINT ["/usr/local/bin/spectra-topic-publisher"]
