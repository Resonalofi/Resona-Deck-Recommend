# syntax=docker/dockerfile:1.7@sha256:a57df69d0ea827fb7266491f2813635de6f17269be881f696fbfdf2d83dda33e

FROM debian:bookworm-slim@sha256:abd67ffcfa541b485a3dff59865ab629aa048a6c613e639d36e7456b0b229241 AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libssl-dev \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src/service

RUN cmake -S /src/service -B /src/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /src/build --parallel "$(nproc)" \
    && strip /src/build/resona-deck-recommend \
    && install -d -o 65532 -g 65532 /runtime/var/lib/resona-deck-recommend

FROM gcr.io/distroless/cc-debian12:nonroot@sha256:adcd20c7b4c988b73cbfbddb26d2eee574571e6d7c9ffea29b3821e0690efb77 AS runtime

COPY --from=build /src/build/resona-deck-recommend /usr/local/bin/resona-deck-recommend
COPY --from=build /src/service/sekai-deck-recommend-cpp/data /usr/local/share/resona-deck-recommend
COPY --from=build --chown=65532:65532 /runtime/var/lib/resona-deck-recommend /var/lib/resona-deck-recommend

WORKDIR /var/lib/resona-deck-recommend
EXPOSE 23457
STOPSIGNAL SIGTERM

ENTRYPOINT ["/usr/local/bin/resona-deck-recommend", \
    "--config", "/etc/resona-deck-recommend/config.toml", \
    "--data", "/usr/local/share/resona-deck-recommend"]
