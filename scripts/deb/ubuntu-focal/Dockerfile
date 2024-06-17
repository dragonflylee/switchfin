# docker build --network host -t dragonflylee/build:ubuntu-focal .

FROM ubuntu:focal
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y gpg wget gnupg- \
  && wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor -o /etc/apt/trusted.gpg.d/kitware.gpg \
  && echo > /etc/apt/sources.list.d/kitware.list "deb https://apt.kitware.com/ubuntu/ focal main" \
  && apt-get update && apt-get install -y --no-install-recommends g++ nasm make patch cmake sudo git xz-utils manpages- \
  && apt-get install -y --no-install-recommends ninja-build python3 python3-pip && pip3 install meson \
  && apt-get remove -y gnupg python3-pip && apt-get autoremove -y \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN apt-get update && apt-get install -y --no-install-recommends xorg-dev libdbus-1-dev libssl-dev \
    libass-dev libwebp-dev libv4l-dev libpulse-dev libuchardet-dev libva-dev libvdpau-dev \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/* /usr/src/tensorrt/*

RUN adduser --gecos '' --disabled-password ubuntu \
  && echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/ubuntu

USER ubuntu
