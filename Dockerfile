# HyperNova — Public Demo Deployment Image
#
# DEPLOYMENT-ONLY FILE. Serves the existing HyperNova web console
# (console/index.html + console/server.py) backed by the HyperNova C++
# solver compiled from this repository's source via CMake.
#
# Target platform: Render Web Service (Docker).
#   - Binds 0.0.0.0 on the PORT environment variable supplied by Render.
#   - CPU-only execution path. No CUDA and no external solver runtime.
#
# This file contains no solver source. The solver implementation is
# frozen and is consumed exactly as it exists in the repository.

# ---------------------------------------------------------------------------
# Stage 1 — builder: compile HyperNova from source with CMake
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# build-essential -> gcc/g++ (C++20)
# cmake           -> build system
# git             -> REQUIRED by CMake FetchContent to retrieve nlohmann/json
# ca-certificates -> REQUIRED by git/CMake to verify the TLS certificate of
#                    github.com when cloning nlohmann/json. Without this the
#                    clone aborts with "server certificate verification failed"
#                    and the whole image build fails at configure time.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

# Build the HyperNova CLI and its shared libraries.
#
# HYPERNOVA_BUILD_TESTS=OFF deliberately:
#   - the public demo serves the web console, not the test suite;
#   - it avoids fetching GoogleTest over the network at image build time,
#     which removes a build-time failure mode on the Render platform.
#   The test suite (345 cases / 22 CTest targets) is unaffected on the
#   developer's machine and can still be run with the default -DON.
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DBUILD_SHARED_LIBS=ON \
             -DHYPERNOVA_BUILD_TESTS=OFF \
             -DHYPERNOVA_BUILD_CLI=ON \
             -DHYPERNOVA_ENABLE_CUDA=OFF \
             -DHYPERNOVA_ENABLE_HIP=OFF \
             -DHYPERNOVA_ENABLE_SYCL=OFF && \
    cmake --build . --parallel $(nproc)

# ---------------------------------------------------------------------------
# Stage 2 — runner: minimal runtime image
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runner

ENV DEBIAN_FRONTEND=noninteractive

# python3     -> runs the existing console/server.py backend (stdlib only)
# libstdc++6  -> C++ runtime for the HyperNova CLI
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# The hypernova executable is dynamically linked against the HyperNova
# shared libraries (BUILD_SHARED_LIBS=ON). These are copied to the SAME
# absolute path used in the builder so the build-tree RPATH recorded in
# the binaries still resolves. LD_LIBRARY_PATH is set as a redundant
# fallback for transitive inter-library resolution.
COPY --from=builder /app/build/bin/hypernova /usr/local/bin/hypernova
COPY --from=builder /app/build/lib /app/build/lib

# Existing web console: backend + frontend. server.py derives the repo
# root as the parent of this directory, so /app/console must stay here.
COPY --from=builder /app/console /app/console

# Point the backend at the compiled solver. This is the documented
# HYPERNOVA_CLI override read by console/server.py.
ENV HYPERNOVA_CLI=/usr/local/bin/hypernova

# Redundant fallback for the shared-library search path (see COPY above).
ENV LD_LIBRARY_PATH=/app/build/lib

# Unbuffered stdout/stderr so the server banner and any solver output
# reach the platform log stream immediately.
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1

# Use the PORT supplied by the platform. Defaults to 8080 only so the
# image also runs unattended under a bare `docker run` with no PORT set.
ENV PORT=8080

# Container-local liveness probe. Render ignores this and performs its
# own checks against the public HTTPS URL; kept for standalone/docker use.
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD python3 -c "import os,urllib.request; urllib.request.urlopen('http://127.0.0.1:%s/api/v1/health' % os.environ.get('PORT','8080'), timeout=4)" || exit 1

# NOTE: no EXPOSE is declared. Render assigns the public port dynamically
# via the PORT environment variable, so a hardcoded port would be
# misleading. The server binds 0.0.0.0:$PORT as required.

# Shell form would leave /bin/sh as PID 1 and python3 as a child process, so
# the platform's SIGTERM would be delivered to the shell and never reach the
# server (verified: the container keeps running after SIGTERM to PID 1).
# 'exec' replaces the shell with python3, making the server PID 1 so SIGTERM
# is delivered directly and Render can perform clean rolling deploys.
# ${PORT:-8080} is expanded by sh at container start; the fallback keeps the
# image runnable under a bare `docker run` with no PORT set.
CMD ["sh", "-c", "exec python3 console/server.py --host 0.0.0.0 --port ${PORT:-8080}"]
