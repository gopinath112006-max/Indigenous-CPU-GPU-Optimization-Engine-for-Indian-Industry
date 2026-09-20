# HyperNova Sovereign Optimization Engine
# Multi-stage Docker build for high-performance C++20 solver & Web Console server

FROM ubuntu:24.04 AS builder

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    g++ \
    libgtest-dev \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY . .

# Build HyperNova binaries
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DHYPERNOVA_BUILD_TESTS=ON \
             -DHYPERNOVA_BUILD_CLI=ON && \
    cmake --build . --parallel $(nproc)

# Run test suite verification gate
RUN cd build && ctest --output-on-failure

# Production deployment stage
FROM ubuntu:24.04 AS runner

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    python3 \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled binaries from builder
COPY --from=builder /app/build/bin/hypernova /usr/local/bin/hypernova
COPY --from=builder /app/build/bin/industrial_demo /usr/local/bin/industrial_demo
COPY --from=builder /app/build/bin/hypernova-bench /usr/local/bin/hypernova-bench

# Copy console files and industrial benchmarks
COPY --from=builder /app/console /app/console
COPY --from=builder /app/benchmarks /app/benchmarks
COPY --from=builder /app/README/SIH_DEMO_PACKAGE.md /app/SIH_DEMO_PACKAGE.md

EXPOSE 8080

CMD ["python3", "console/server.py", "--host", "0.0.0.0", "--port", "8080"]
