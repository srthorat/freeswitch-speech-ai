# Build Stage
FROM golang:1.22-bookworm AS builder

# Install C dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    pkg-config \
    libspeexdsp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Cache modules
COPY go.mod go.sum ./
RUN go mod download

# Build
COPY . .
# -ldflags "-s -w" reduces binary size
RUN go build -ldflags "-s -w" -o sidecar-unified ./cmd/sidecar

# Runtime Stage
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libspeexdsp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/sidecar-unified /app/sidecar
COPY .env.example .env

# Expose ports
EXPOSE 8088 8089

# Default command
CMD ["./sidecar"]
