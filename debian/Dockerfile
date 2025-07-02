ARG IMG=ubuntu
ARG TAG=noble
FROM $IMG:$TAG
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends wget ca-certificates \
    xz-utils debhelper cmake build-essential nasm git fakeroot lzip ninja-build python3-pip \
  && pip3 install meson --break-system-packages --no-cache-dir \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN apt-get update && apt-get install -y --no-install-recommends xorg-dev libdbus-1-dev libxpresent-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols libpulse-dev libwebp-dev libegl-dev libv4l-dev \
    libuchardet-dev libva-dev libvdpau-dev libdrm-dev libudev-dev libgbm-dev \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*