# syntax=docker/dockerfile:1
# Среда сборки MTV3 (RV1126): Rockchip Linux SDK rv1126_rv1109_linux_ai_camera
# (release 20210306: buildroot 2018.02, kernel 4.19, u-boot 2017.09).
#
# База — Ubuntu 18.04 (glibc 2.27, gcc 7.5, python 2.7/3.6). Это требование SDK,
# а не прихоть: host-пакеты buildroot 2018.02 (m4, bison, gettext, fakeroot)
# не собираются на glibc >= 2.28 без бэкпортов, dtc в kernel/u-boot ломается на
# gcc >= 10 (yylloc), а wheels rknn-toolkit 1.6.1 требуют python 3.6 x86_64.
#
# Стадии:
#   sdk   — всё для ./build.sh (u-boot/kernel/rootfs/updateimg), кросс-сборки
#           SeeSharp/демона, прошивки (rkdeveloptool, upgrade_tool из SDK);
#   rknn  — то же + RKNN Toolkit 1.6.1 (конвертация onnx -> rknn на x86_64).
# Сам SDK в образ не входит: он живёт в docker volume или bind-mount,
# см. import-sdk.sh / run-sdk.sh.

FROM --platform=linux/amd64 ubuntu:18.04 AS sdk

ARG DEBIAN_FRONTEND=noninteractive
ARG USER_ID=1000
ARG GROUP_ID=1000
ARG CMAKE_VERSION=3.27.9

# Пакеты: список из Rockchip_Developer_Guide_Linux_Software + то, что buildroot
# 2018.02 и kernel 4.19 просят по факту (texinfo для host-autotools, lz4/lzop для
# образов ядра, i386-библиотеки для 32-битных утилит rkbin, libusb для rkdeveloptool).
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        autoconf automake bc binfmt-support bison bsdmainutils build-essential \
        bzip2 ca-certificates ccache chrpath cpio curl device-tree-compiler \
        diffstat dosfstools e2fsprogs expect fakeroot file flex gawk gcc-multilib \
        g++-multilib gdb-multiarch genext2fs gettext git git-lfs gperf help2man \
        kmod less lib32stdc++6 lib32z1 libc6:i386 libelf-dev libglib2.0-dev \
        libgmp-dev libmpc-dev libmpfr-dev libncurses5-dev libssl-dev libtool \
        libusb-1.0-0-dev liblz4-tool locales lzop make mtools nano ninja-build \
        openssh-client openssl patch patchelf pkg-config procps python python-dev \
        python-pip python-pyelftools python-setuptools python3 python3-dev \
        python3-pip python3-setuptools python3-venv python3-wheel rsync screen \
        squashfs-tools sudo swig texinfo time tree u-boot-tools unzip \
        vim-common wget xz-utils zip zlib1g-dev && \
    locale-gen en_US.UTF-8 && \
    rm -rf /var/lib/apt/lists/*

# CMake из Kitware: системный 3.10 не умеет `cmake -S/-B` (3.13+), которым
# пользуются README и host-скрипты. Проверка SHA-256 по файлу из того же релиза.
RUN set -eu; cd /tmp; \
    base="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}"; \
    tgz="cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz"; \
    curl -fsSL -o "$tgz" "$base/$tgz"; \
    curl -fsSL -o SHA-256.txt "$base/cmake-${CMAKE_VERSION}-SHA-256.txt"; \
    grep " $tgz\$" SHA-256.txt | sha256sum -c -; \
    tar -xzf "$tgz" -C /opt; \
    ln -s "/opt/cmake-${CMAKE_VERSION}-linux-x86_64" /opt/cmake; \
    rm -f "$tgz" SHA-256.txt
ENV PATH=/opt/cmake/bin:/usr/lib/ccache:$PATH

# repo launcher (Firefly git-repo, REPO_REV=stable) — для repo init/sync SDK из GitLab.
RUN curl -fsSL -o /usr/local/bin/repo \
        https://gitlab.com/firefly-linux/git-repo/-/raw/master/repo && \
    chmod 0755 /usr/local/bin/repo && \
    sed -n '1p;/^REPO_REV\|^MIN_PYTHON_VERSION/p' /usr/local/bin/repo

# rkdeveloptool — прошивка по USB (maskrom/loader) без host-утилит.
RUN set -eu; cd /tmp; \
    curl -fsSL -o rkdeveloptool.tar.gz \
        https://github.com/rockchip-linux/rkdeveloptool/archive/refs/heads/master.tar.gz; \
    mkdir rkdeveloptool; tar -xzf rkdeveloptool.tar.gz -C rkdeveloptool --strip-components=1; \
    cd rkdeveloptool; autoreconf -i; \
    ./configure CXXFLAGS="-O2 -Wno-format-truncation"; \
    make -j"$(nproc)"; install -m 0755 rkdeveloptool /usr/local/bin/rkdeveloptool; \
    cd /tmp; rm -rf rkdeveloptool rkdeveloptool.tar.gz

# Пользователь с UID/GID хоста, чтобы файлы в bind-mount не становились root.
RUN groupadd --gid "${GROUP_ID}" builder && \
    useradd --uid "${USER_ID}" --gid "${GROUP_ID}" --create-home --shell /bin/bash builder && \
    echo 'builder ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/builder && \
    chmod 0440 /etc/sudoers.d/builder && \
    git config --system safe.directory '*' && \
    mkdir -p /proj/rv1126_sdk_ai /work/mtv3-vision /work/mtv3-bsp && \
    chown builder:builder /proj/rv1126_sdk_ai /work/mtv3-vision /work/mtv3-bsp

COPY --chmod=0755 entrypoint.sh /usr/local/bin/mtv3-entrypoint
COPY --chmod=0755 mtv3-bsp.sh /usr/local/bin/mtv3-bsp
COPY --chmod=0644 bashrc.sdk /etc/profile.d/mtv3-sdk.sh

ENV LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8 \
    RV1126_SDK=/proj/rv1126_sdk_ai \
    MTV3_REPO=/work/mtv3-vision \
    MTV3_BSP=/work/mtv3-bsp \
    CCACHE_DIR=/home/builder/.ccache \
    BR2_CCACHE_DIR=/home/builder/.buildroot-ccache

USER builder
WORKDIR /work/mtv3-vision
ENTRYPOINT ["/usr/local/bin/mtv3-entrypoint"]
CMD ["/bin/bash", "-l"]

# ---------------------------------------------------------------------------
# Опционально: RKNN Toolkit 1.6.1 (python 3.6, x86_64, ~700 МБ скачивания,
# ~3 ГБ в образе). build-image.sh --rknn
FROM sdk AS rknn
USER root
RUN set -eu; cd /tmp; \
    curl -fsSL -o rknn-packages.tar.gz \
        https://github.com/rockchip-linux/rknn-toolkit/releases/download/v1.6.1/rknn-toolkit-v1.6.1-packages.tar.gz; \
    mkdir rknn; tar -xzf rknn-packages.tar.gz -C rknn; \
    python3 -m pip install --no-cache-dir --upgrade "pip<22" "setuptools<60" wheel; \
    req="$(find rknn -name 'requirements-cpu.txt' | head -n1)"; \
    whl="$(find rknn -name 'rknn_toolkit-1.6.1-cp36-*.whl' | head -n1)"; \
    [ -n "$req" ] && [ -n "$whl" ]; \
    python3 -m pip install --no-cache-dir -r "$req"; \
    python3 -m pip install --no-cache-dir "$whl"; \
    rm -rf /tmp/rknn /tmp/rknn-packages.tar.gz
USER builder
