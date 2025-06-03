ARG IMG=ubuntu
ARG TAG=noble
FROM $IMG:$TAG
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends wget xz-utils \
    ca-certificates debhelper cmake build-essential nasm meson git fakeroot lzip \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN apt-get update && apt-get install -y --no-install-recommends xorg-dev libdbus-1-dev libxpresent-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols libpulse-dev libwebp-dev libass-dev libv4l-dev \
    libuchardet-dev libva-dev libvdpau-dev libjpeg-dev libdrm-dev libegl-dev libdisplay-info-dev \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*