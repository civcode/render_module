# Test-only Ubuntu 24.04 runtime. Source/build and Node/Playwright caches are mounted.
# A bridge network supplies a real ICE interface; NET_ADMIN is only needed for netem.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates cmake iproute2 coturn openssl xvfb xauth \
    libnice10 libssl3t64 libboost-json1.83.0 libboost-system1.83.0 libjpeg-turbo8 \
    libegl1 libgl1 libopengl0 libgl1-mesa-dri libavcodec60 libavutil58 \
    libnss3 libnspr4 libatk1.0-0t64 libatk-bridge2.0-0t64 libatspi2.0-0t64 \
    libx11-6 libx11-xcb1 libxcb1 libxcomposite1 libxdamage1 libxext6 libxfixes3 \
    libxrandr2 libxcursor1 libxi6 libxinerama1 libxkbcommon0 libgbm1 \
    libgtk-3-0t64 libcups2t64 libasound2t64 libpango-1.0-0 libcairo2 \
    fonts-liberation fonts-dejavu-core fonts-noto-color-emoji \
    && rm -rf /var/lib/apt/lists/*
ENV TMPDIR=/tmp HOME=/tmp LIBGL_ALWAYS_SOFTWARE=1
