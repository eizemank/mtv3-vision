FROM ubuntu:20.04

ARG DEBIAN_FRONTEND=noninteractive
ARG USER_ID=1000
ARG GROUP_ID=1000

RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        autoconf automake bc bison build-essential ca-certificates cpio curl \
        device-tree-compiler dosfstools expect fakeroot file flex gawk gcc-multilib \
        git git-lfs lib32stdc++6 lib32z1 libc6:i386 libglib2.0-dev libncurses5-dev \
        libssl-dev libtool locales make mtools nano patch pkg-config python2 \
        python3 python3-pip rsync sudo swig unzip vim wget xz-utils zip && \
    ln -s /usr/bin/python2 /usr/local/bin/python && \
    locale-gen en_US.UTF-8 && \
    groupadd --gid "${GROUP_ID}" builder && \
    useradd --uid "${USER_ID}" --gid "${GROUP_ID}" --create-home --shell /bin/bash builder && \
    echo 'builder ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/builder && \
    rm -rf /var/lib/apt/lists/*

ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8
ENV RV1126_SDK=/proj/rv1126_sdk_ai

USER builder
WORKDIR /work/mtv3-vision
CMD ["/bin/bash"]
