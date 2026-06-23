# Eskiu playground image.
#
# The build context is THIS directory (playground/), so the prebuilt Linux
# compiler is vendored in ./dist (the repo is private — we can't pull the
# release asset during the build). Refresh it with ./update-dist.sh.
#
#   docker build -t eskiu-playground .
#
# dist/ layout (from the eskiuc-linux-x86_64 release tarball):
#   dist/bin/eskiuc
#   dist/lib/eskiu/stdlib/...
#
# The release binary statically links LLVM, so runtime only needs a few shared
# libs (libstdc++, z3, zstd, zlib) plus a C linker driver (gcc) — `eskiuc run`
# links the emitted object into a temporary executable with $CC.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        python3 python3-pip \
        gcc libc6-dev \
        libstdc++6 zlib1g libz3-4 libzstd1 libgcc-s1 \
    && rm -rf /var/lib/apt/lists/*

# Compiler + stdlib under /opt/eskiu so the app's defaults line up:
#   ESKIUC=/opt/eskiu/bin/eskiuc, ESKIU_ROOT=/opt/eskiu (holds stdlib/)
COPY dist/bin/eskiuc           /opt/eskiu/bin/eskiuc
COPY dist/lib/eskiu/stdlib     /opt/eskiu/stdlib

ENV ESKIU_ROOT=/opt/eskiu \
    ESKIUC=/opt/eskiu/bin/eskiuc \
    CC=gcc \
    PATH=/opt/eskiu/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app
COPY requirements.txt .
RUN pip3 install --no-cache-dir --break-system-packages -r requirements.txt
COPY app.py .

# Run unprivileged. User code inherits this UID under the per-request rlimits.
RUN useradd --create-home --uid 10001 runner
USER runner

EXPOSE 8080
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8080", "--workers", "2"]
