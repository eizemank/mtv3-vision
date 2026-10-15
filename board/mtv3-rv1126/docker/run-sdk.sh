#!/bin/sh
# Запуск контейнера среды разработки MTV3 (RV1126).
#   sh run-sdk.sh                        — интерактивный shell
#   sh run-sdk.sh bash -lc 'sdk rootfs'  — одна команда
# Где лежит SDK:
#   RV1126_SDK=/abs/path/rv1126_sdk_ai   — bind-mount распакованного дерева с хоста;
#   иначе                                — docker volume mtv3-rv1126-sdk
#                                          (заполняется import-sdk.sh).
# Overlay/patches платы: MTV3_BSP=/abs/path/mtv3-bsp — монтируется в /work/mtv3-bsp.
# Прошивка по USB: MTV3_USB=1 (пробрасывает /dev/bus/usb; Linux-хост или WSL+usbipd).
# Образ: MTV3_SDK_IMAGE (по умолчанию mtv3-rv1126-sdk), тег MTV3_SDK_TAG (latest|rknn).
# Доп. опции docker run: MTV3_DOCKER_ARGS="-p 8081:8081".
set -eu

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
IMAGE=${MTV3_SDK_IMAGE:-mtv3-rv1126-sdk}
TAG=${MTV3_SDK_TAG:-latest}
SDK_VOLUME=${MTV3_SDK_VOLUME:-mtv3-rv1126-sdk}
HOME_VOLUME=${MTV3_HOME_VOLUME:-mtv3-rv1126-home}

if ! docker image inspect "$IMAGE:$TAG" >/dev/null 2>&1; then
    echo "image $IMAGE:$TAG not found; run: sh $(dirname -- "$0")/build-image.sh" >&2
    exit 1
fi

if [ -n "${RV1126_SDK:-}" ]; then
    [ -d "$RV1126_SDK" ] || { echo "RV1126_SDK directory not found: $RV1126_SDK" >&2; exit 1; }
    SDK_SRC=$RV1126_SDK
else
    SDK_SRC=$SDK_VOLUME
fi

BSP_OPT=
if [ -n "${MTV3_BSP:-}" ]; then
    [ -d "$MTV3_BSP" ] || { echo "MTV3_BSP directory not found: $MTV3_BSP" >&2; exit 1; }
    BSP_OPT="--volume=$MTV3_BSP:/work/mtv3-bsp"
fi

TTY_OPT=
if [ -t 0 ] && [ -t 1 ]; then
    TTY_OPT=-it
fi

USB_OPT=
if [ "${MTV3_USB:-0}" = 1 ]; then
    if [ -d /dev/bus/usb ]; then
        USB_OPT="--privileged --volume=/dev/bus/usb:/dev/bus/usb"
    else
        echo "MTV3_USB=1 but /dev/bus/usb is absent on this host (Docker Desktop? use usbipd)" >&2
    fi
fi

# seccomp=unconfined: fakeroot/qemu-user внутри buildroot иначе получают EPERM.
exec docker run --rm $TTY_OPT $USB_OPT $BSP_OPT ${MTV3_DOCKER_ARGS:-} \
    --platform linux/amd64 \
    --hostname mtv3-sdk \
    --security-opt seccomp=unconfined \
    --volume "$SDK_SRC:/proj/rv1126_sdk_ai" \
    --volume "$HOME_VOLUME:/home/builder" \
    --volume "$REPO_DIR:/work/mtv3-vision" \
    --workdir /work/mtv3-vision \
    "$IMAGE:$TAG" "$@"
