#!/bin/sh
# Получение Firefly/Rockchip SDK для RV1126 «с нуля» из GitLab в docker volume.
#   sh init-sdk.sh                 — полный SDK (rv1126_rv1109_linux_ai_camera_release.xml)
#   sh init-sdk.sh --bsp           — только BSP: device/rockchip, kernel, u-boot, rkbin, tools, тулчейны
#   sh init-sdk.sh --manifest X.xml — другой манифест из https://gitlab.com/firefly-linux/manifests
#   sh init-sdk.sh --sync          — только repo sync в уже инициализированном volume
#   sh init-sdk.sh --pinned FILE   — init по зафиксированному манифесту (mtv3-bsp/manifest/pinned.xml)
# Переменные: MTV3_REPO_DEPTH (1 по умолчанию, 0 = полная история), MTV3_REPO_JOBS (4),
#             MTV3_SDK_VOLUME (mtv3-rv1126-sdk), MTV3_SDK_IMAGE, MTV3_SDK_TAG.
# Место: полный SDK ~20 ГБ после sync (buildroot/dl ~ 8 ГБ), ~60 ГБ после сборки.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IMAGE=${MTV3_SDK_IMAGE:-mtv3-rv1126-sdk}
TAG=${MTV3_SDK_TAG:-latest}
SDK_VOLUME=${MTV3_SDK_VOLUME:-mtv3-rv1126-sdk}
MANIFEST_URL=https://gitlab.com/firefly-linux/manifests.git
REPO_URL=https://gitlab.com/firefly-linux/git-repo.git
MANIFEST=rv1126_rv1109_linux_ai_camera_release.xml
MODE=init
PINNED=
DEPTH=${MTV3_REPO_DEPTH:-1}
JOBS=${MTV3_REPO_JOBS:-4}

while [ $# -gt 0 ]; do
    case "$1" in
        --bsp) MANIFEST=rv1126_rv1109_linux_ai_camera_bsp_release.xml ;;
        --manifest) shift; MANIFEST=$1 ;;
        --sync) MODE=sync ;;
        --pinned) shift; PINNED=$1; [ -f "$PINNED" ] || { echo "no such file: $PINNED" >&2; exit 1; } ;;
        -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

if ! docker image inspect "$IMAGE:$TAG" >/dev/null 2>&1; then
    echo "image $IMAGE:$TAG not found; run: sh $SCRIPT_DIR/build-image.sh" >&2
    exit 1
fi

PIN_OPT=
PINNED_NAME=
if [ -n "$PINNED" ]; then
    PIN_DIR=$(CDPATH= cd -- "$(dirname -- "$PINNED")" && pwd)
    PIN_OPT="--volume=$PIN_DIR:/pinned:ro"
    PINNED_NAME=$(basename -- "$PINNED")
fi

exec docker run --rm --platform linux/amd64 --env MTV3_QUIET=1 \
    --volume "$SDK_VOLUME:/proj/rv1126_sdk_ai" $PIN_OPT \
    --env "MODE=$MODE" --env "MANIFEST=$MANIFEST" --env "PINNED_NAME=$PINNED_NAME" \
    --env "DEPTH=$DEPTH" --env "JOBS=$JOBS" \
    --env "MANIFEST_URL=$MANIFEST_URL" --env "REPO_URL=$REPO_URL" \
    "$IMAGE:$TAG" bash -eu -c '
cd /proj/rv1126_sdk_ai
if [ "$MODE" = init ]; then
    if [ -d .repo ]; then
        echo "[init-sdk] .repo already exists here; use --sync to update" >&2; exit 1
    fi
    if [ -e build.sh ]; then
        echo "[init-sdk] volume already holds an SDK without .repo (imported tarball?); refusing" >&2; exit 1
    fi
    depth_opt=; [ "$DEPTH" -gt 0 ] && depth_opt="--depth=$DEPTH"
    echo "[init-sdk] repo init -u $MANIFEST_URL -b master -m $MANIFEST $depth_opt"
    repo init --no-clone-bundle --repo-url "$REPO_URL" -u "$MANIFEST_URL" -b master -m "$MANIFEST" $depth_opt </dev/null
    if [ -n "$PINNED_NAME" ]; then
        cp "/pinned/$PINNED_NAME" .repo/manifests/mtv3-pinned.xml
        echo "[init-sdk] switching to pinned manifest $PINNED_NAME"
        repo init -m mtv3-pinned.xml </dev/null
    fi
fi
[ -d .repo ] || { echo "[init-sdk] no .repo in volume; run without --sync first" >&2; exit 1; }
echo "[init-sdk] repo sync -c --no-tags -j$JOBS ..."
repo sync -c --no-tags --no-clone-bundle -j"$JOBS" --fail-fast </dev/null
echo "[init-sdk] done: $(repo --version 2>/dev/null | head -n1)"
ls -d */ | tr "\n" " "; echo
du -sh . 2>/dev/null | sed "s/^/[init-sdk] size: /"
'
