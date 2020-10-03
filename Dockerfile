FROM ubuntu:20.04 as build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
  apt-get install -y \
    binutils-mips-linux-gnu \
    bison \
    bsdmainutils \
    build-essential \
    clang \
    cmake \
    flex \
    libaudiofile-dev \
    lld \
    pkg-config \
    python3 \
    zlib1g-dev

RUN mkdir /sm64
WORKDIR /sm64
ENV PATH="/sm64/tools:/usr/lib/llvm-10/bin:${PATH}"

CMD echo 'usage: docker run --rm -v $(pwd):/sm64 sm64 make VERSION=${VERSION:-us} -j4'
