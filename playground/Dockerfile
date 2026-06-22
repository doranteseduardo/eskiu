# Eskiu playground image.
#
# Build context must be the REPO ROOT so the compiler can be built from source:
#   docker build -f playground/Dockerfile -t eskiu-playground .
#
# Stage 1 builds `eskiuc`; stage 2 ships the binary + stdlib + a C linker
# (clang) + the FastAPI app. eskiuc resolves the stdlib via $ESKIU_ROOT and
# links the temporary executable with $CC.

# ── Stage 1: build the compiler ─────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

ARG LLVM_VERSION=22
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake build-essential wget gnupg lsb-release software-properties-common ca-certificates \
    && wget -q https://apt.llvm.org/llvm.sh && chmod +x llvm.sh \
    && ./llvm.sh ${LLVM_VERSION} \
    && apt-get install -y --no-install-recommends llvm-${LLVM_VERSION}-dev \
    && rm -rf /var/lib/apt/lists/* llvm.sh

COPY . /src
RUN cmake -S /src -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=clang++-${LLVM_VERSION} \
        -DLLVM_DIR=/usr/lib/llvm-${LLVM_VERSION}/lib/cmake/llvm \
    && cmake --build /build -j"$(nproc)" --target eskiuc

# ── Stage 2: runtime ────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

ARG LLVM_VERSION=22
ENV DEBIAN_FRONTEND=noninteractive

# Runtime needs: LLVM shared libs (eskiuc links against them), clang as the C
# linker driver, and Python for the API.
RUN apt-get update && apt-get install -y --no-install-recommends \
        wget gnupg lsb-release software-properties-common ca-certificates \
        python3 python3-pip \
    && wget -q https://apt.llvm.org/llvm.sh && chmod +x llvm.sh \
    && ./llvm.sh ${LLVM_VERSION} \
    && apt-get purge -y wget gnupg software-properties-common \
    && apt-get autoremove -y \
    && rm -rf /var/lib/apt/lists/* llvm.sh

# Compiler + stdlib under a single prefix that $ESKIU_ROOT points at.
COPY --from=builder /build/eskiuc /opt/eskiu/bin/eskiuc
COPY --from=builder /src/stdlib  /opt/eskiu/stdlib

ENV ESKIU_ROOT=/opt/eskiu \
    ESKIUC=/opt/eskiu/bin/eskiuc \
    CC=clang-${LLVM_VERSION} \
    PATH=/opt/eskiu/bin:/usr/bin:/bin \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

# API
WORKDIR /app
COPY playground/requirements.txt .
RUN pip3 install --no-cache-dir --break-system-packages -r requirements.txt
COPY playground/app.py .

# Run unprivileged. User code inherits this UID under the per-request rlimits.
RUN useradd --create-home --uid 10001 runner
USER runner

EXPOSE 8080
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8080", "--workers", "2"]
