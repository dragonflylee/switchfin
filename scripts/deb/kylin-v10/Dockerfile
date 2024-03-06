# docker build --network host -t dragonflylee/build:kylin-v10 .
# docker run --rm --network host -v $PWD:/opt -w /opt -it dragonflylee/ubuntu:kylin-v10 bash -l

FROM kylin:v10
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y wget apt-transport-https \
  && wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor -o /etc/apt/trusted.gpg.d/kitware.gpg \
  && echo > /etc/apt/sources.list.d/kitware.list "deb https://apt.kitware.com/ubuntu/ xenial main" \
  && wget -qO- "http://keyserver.ubuntu.com/pks/lookup?op=get&search=0x60c317803a41ba51845e371a1e9377a2ba9ef27f" | gpg --dearmor -o /etc/apt/trusted.gpg.d/toolchain-r.gpg \
  && echo > /etc/apt/sources.list.d/toolchain-r.list "deb http://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu xenial main" \
  && apt-get update && apt-get install -y --no-install-recommends gcc-8 g++-8 nasm make cmake sudo git python3 \
  && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-8 60 --slave /usr/bin/cc cc /usr/bin/gcc-8 \
  && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-8 60 --slave /usr/bin/cpp cpp /usr/bin/g++-8 \
  && apt-get remove -y apt-transport-https && apt-get autoremove -y \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/* /etc/apt/sources.list.d/kitware.list

RUN apt-get update && apt-get install -y --no-install-recommends xorg-dev libdbus-1-dev \
    libass-dev libssl-dev libv4l-dev libpulse-dev libuchardet-dev libva-dev libgl1-mesa-dev \
  && rm -rf /var/lib/apt/lists/* /usr/share/man/*

RUN adduser --gecos '' --disabled-password kylin \
  && echo 'kylin ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/kylin

USER kylin
