# docker build --network host -t build:debian-bookworm .

FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y gnupg wget \
  && wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor -o /etc/apt/trusted.gpg.d/kitware.gpg \
  && echo > /etc/apt/sources.list.d/kitware.list "deb https://apt.kitware.com/ubuntu/ jammy main" \
  && apt-get update && apt-get install -y --no-install-recommends g++ nasm make patch cmake sudo git xz-utils meson manpages- \
  && apt-get remove -y gnupg && apt-get autoremove -y \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN apt-get update && apt-get install -y --no-install-recommends libwayland-dev libxkbcommon-dev xorg-dev libdbus-1-dev libssl-dev \
    libluajit-5.1-dev libass-dev libwebp-dev libv4l-dev libpulse-dev libuchardet-dev libva-dev libvdpau-dev \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN adduser --gecos '' --disabled-password debian \
  && echo 'debian ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/debian

USER debian
