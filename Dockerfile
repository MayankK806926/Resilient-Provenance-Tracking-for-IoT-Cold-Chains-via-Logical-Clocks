# Multi-stage build for Cold Chain Vector Clock Ledger Node
# Stage 1: Build environment
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Configure and build in Release mode
RUN cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target sim sweep_runner

# Stage 2: Minimal runtime image for edge gateway
FROM ubuntu:22.04 AS runner

WORKDIR /app
COPY --from=builder /app/build/sim /app/sim
COPY --from=builder /app/build/sweep_runner /app/sweep_runner

# Default command runs the comparative simulation demo
ENTRYPOINT ["/app/sim"]
CMD ["--shipments", "3", "--duration-hours", "12", "--seed", "42"]

