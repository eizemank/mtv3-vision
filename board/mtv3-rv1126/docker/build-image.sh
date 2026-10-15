#!/bin/sh
# Сборка образа среды разработки MTV3 (RV1126).
#   sh build-image.sh          — стадия sdk (тулчейн, buildroot, kernel, прошивка)
#   sh build-image.sh --rknn   — плюс RKNN Toolkit 1.6.1 (тег ...:rknn, ~3 ГБ)
# Переменные: MTV3_SDK_IMAGE (имя образа), USER_ID/GROUP_ID (по умолчанию id хоста).
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IMAGE=${MTV3_SDK_IMAGE:-mtv3-rv1126-sdk}
TARGET=sdk
TAG=ubuntu18.04

for arg in "$@"; do
    case "$arg" in
        --rknn) TARGET=rknn; TAG=rknn ;;
        -h|--help) sed -n '2,6p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

# На Windows/macOS Docker Desktop id может отсутствовать — тогда 1000:1000.
UID_ARG=${USER_ID:-$(id -u 2>/dev/null || echo 1000)}
GID_ARG=${GROUP_ID:-$(id -g 2>/dev/null || echo 1000)}
# root в контейнере — отдельная история с правами на bind-mount; не допускаем.
[ "$UID_ARG" -ne 0 ] || UID_ARG=1000
[ "$GID_ARG" -ne 0 ] || GID_ARG=1000

DOCKER_BUILDKIT=1 docker build \
    --platform linux/amd64 \
    --target "$TARGET" \
    --build-arg "USER_ID=$UID_ARG" \
    --build-arg "GROUP_ID=$GID_ARG" \
    --tag "$IMAGE:$TAG" \
    "$SCRIPT_DIR"

[ "$TARGET" = sdk ] && docker tag "$IMAGE:$TAG" "$IMAGE:latest"
echo "built $IMAGE:$TAG"
