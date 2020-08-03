FROM ubuntu:18.04 as build

RUN apt-get update && \
    apt-get install -y \
        binutils-mips-linux-gnu \
        bsdmainutils \
        build-essential \
        libaudiofile-dev \
        libsdl2-dev \
        libusb-1.0 \
        libx11-dev \
        pkg-config \
        python3 \
        wget \
        zlib1g-dev

# linux build
RUN wget \
        https://github.com/n64decomp/qemu-irix/releases/download/v2.11-deb/qemu-irix-2.11.0-2169-g32ab296eef_amd64.deb \
        -O qemu.deb && \
    echo 8170f37cf03a08cc2d7c1c58f10d650ea0d158f711f6916da9364f6d8c85f741 qemu.deb | sha256sum --check && \
    dpkg -i qemu.deb && \
    rm qemu.deb

# 3ds build
RUN wget https://github.com/devkitPro/pacman/releases/download/v1.0.2/devkitpro-pacman.amd64.deb \
  -O devkitpro.deb && \
  echo ebc9f199da9a685e5264c87578efe29309d5d90f44f99f3dad9dcd96323fece3 devkitpro.deb | sha256sum --check && \
  apt install -y ./devkitpro.deb && \
  rm devkitpro.deb
RUN dkp-pacman -Syu 3ds-dev --noconfirm

RUN mkdir /sm64
WORKDIR /sm64

ENV PATH="/opt/devkitpro/tools/bin/:/sm64/tools:${PATH}"
ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV DEVKITPPC=/opt/devkitpro/devkitPPC

CMD echo 'usage: docker run --rm --mount type=bind,source="$(pwd)",destination=/sm64 sm64 make VERSION=${VERSION:-us} -j4\n' \
         'see https://github.com/n64decomp/sm64/blob/master/README.md for advanced usage'
