FROM devkitpro/devkita64:20241023
RUN sed -i 's/deb.debian.org/mirrors.aliyun.com/g' /etc/apt/sources.list && apt-get update \
   && apt-get install -y --no-install-recommends fakeroot zstd bison flex file libtool python3-pip \
   && pip install meson && rm -rf /var/lib/apt/lists/* /usr/share/man/*
RUN adduser --gecos '' --home /work --disabled-password builder \
   && echo 'builder ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/builder
USER builder
WORKDIR /work 
