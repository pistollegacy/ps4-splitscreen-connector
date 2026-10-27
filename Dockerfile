# =============================================================================
# PS4SplitScreenConnector - Docker Build Environment
# =============================================================================
# Usage:
#   docker build -t ps4-splitscreen-builder .
#   docker run --rm -v ${PWD}:/src -v ps4-sdk-cache:/home/builder/openorbis-sdk ps4-splitscreen-builder
#
# Or use docker-compose.yml for easier usage
# =============================================================================

FROM ubuntu:22.04 AS builder-base

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git \
    python3 python3-pip python3-venv \
    libssl-dev pkg-config wget curl unzip \
    clang lld llvm libelf-dev \
    xorriso ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create builder user
RUN useradd -m -s /bin/bash builder
USER builder
WORKDIR /home/builder

# -----------------------------------------------------------------------------
# Stage 2: Build OpenOrbis SDK (cached layer)
# -----------------------------------------------------------------------------
FROM builder-base AS sdk-builder

# Clone and build toolchain
RUN git clone --recursive https://github.com/OpenOrbis/openorbis-sdk ~/openorbis-sdk \
    && cd ~/openorbis-sdk \
    && ./build-toolchain.sh 2>&1 | tee build-toolchain.log

# Verify
RUN . ~/openorbis-sdk/environment.sh && $CC --version

# -----------------------------------------------------------------------------
# Stage 3: Build Project
# -----------------------------------------------------------------------------
FROM sdk-builder AS project-builder

# Copy source
USER root
COPY --chown=builder:builder . /src
USER builder
WORKDIR /src

# Source environment and build
RUN . ~/openorbis-sdk/environment.sh \
    && ./scripts/build_ps4.sh Release

# -----------------------------------------------------------------------------
# Stage 4: Create .pkg (final)
# -----------------------------------------------------------------------------
FROM project-builder AS pkg-builder

# Build orbis-pub-gen if needed
RUN . ~/openorbis-sdk/environment.sh \
    && cd ~ \
    && git clone https://github.com/OpenOrbis/orbis-pub-gen 2>/dev/null || true \
    && cd orbis-pub-gen \
    && mkdir -p build && cd build \
    && cmake .. -DCMAKE_PREFIX_PATH=$PS4SDK \
    && make -j$(nproc) \
    && sudo make install

# Generate package.conf with unique Content-ID
ARG CONTENT_ID=UP0001-PS4SPLITSCREEN_00-ABCDEF1234567890
RUN . ~/openorbis-sdk/environment.sh \
    && cd /src \
    && cp scripts/package.conf.template package.conf \
    && sed -i "s|UP0000-PS4SPLITSCREEN_00-ABCDEF1234567890|${CONTENT_ID}|" package.conf \
    && orbis-pub-gen -config package.conf -output PS4SplitScreenConnector.pkg

# Verify
RUN ls -la /src/PS4SplitScreenConnector.pkg && file /src/PS4SplitScreenConnector.pkg

# -----------------------------------------------------------------------------
# Final: Minimal runtime image with just the .pkg
# -----------------------------------------------------------------------------
FROM scratch AS export
COPY --from=pkg-builder /src/PS4SplitScreenConnector.pkg /PS4SplitScreenConnector.pkg