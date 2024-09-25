# docker build --network host -t build:debian-bookworm .
# docker run --rm --network host -v $PWD:/tmp/work -w /tmp/work -it build:debian-bookworm dpkg-buildpackage --post-clean

ARG IMG=debian
ARG TAG=bookworm
FROM $IMG:$TAG
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y debhelper build-essential fakeroot nasm cmake sudo git meson \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN apt-get update && apt-get install -y --no-install-recommends libwayland-dev libxkbcommon-dev xorg-dev libdbus-1-dev \
    libcurl4-openssl-dev libwebp-dev libmpv-dev && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN adduser --gecos '' --disabled-password debian \
  && echo 'debian ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/debian

USER debian
