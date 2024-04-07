# docker build --network host -t dragonflylee/build:kylin-v10 .
# docker run --rm --network host -v $PWD:/opt -w /opt -it dragonflylee/ubuntu:kylin-v10 bash -l

FROM kylin:v10
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y wget \
  && wget -qO- "http://keyserver.ubuntu.com/pks/lookup?op=get&search=0x60c317803a41ba51845e371a1e9377a2ba9ef27f" | gpg --dearmor -o /etc/apt/trusted.gpg.d/toolchain-r.gpg \
  && echo > /etc/apt/sources.list.d/toolchain-r.list "deb http://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu xenial main" \
  && apt-get update && apt-get install -y --no-install-recommends gcc-8 g++-8 nasm make patch sudo git python3 \
  && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-8 60 --slave /usr/bin/cc cc /usr/bin/gcc-8 \
  && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-8 60 --slave /usr/bin/cpp cpp /usr/bin/g++-8 \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN wget -qO- https://gitlab.kitware.com/cmake/cmake/-/archive/v3.20.5/cmake-v3.20.5.tar.gz | tar zxf - -C /tmp \
  && mkdir /tmp/cmake-v3.20.5/build && cd /tmp/cmake-v3.20.5/build && ../configure --prefix=/usr \
  && make -j$(nproc) && make install && cd / && rm -rf /tmp/cmake-v3.20.5

RUN apt-get update && apt-get install -y --no-install-recommends xorg-dev libdbus-1-dev libssl-dev \
    libass-dev libv4l-dev libpulse-dev libuchardet-dev libva-dev libvdpau-dev libgl1-mesa-dev \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN adduser --gecos '' --disabled-password kylin \
  && echo 'kylin ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/kylin

USER kylin
