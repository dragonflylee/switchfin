ARG IMG=ubuntu
ARG TAG=noble
FROM $IMG:$TAG
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends wget xz-utils \
    ca-certificates debhelper cmake build-essential nasm meson git fakeroot \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN apt-get update && apt-get install -y --no-install-recommends xorg-dev libdbus-1-dev \
    libwayland-dev libxkbcommon-dev libwebp-dev libass-dev libv4l-dev libpulse-dev \
    libuchardet-dev libva-dev libvdpau-dev \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*