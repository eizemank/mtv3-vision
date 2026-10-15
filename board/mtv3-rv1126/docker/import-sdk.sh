#!/bin/sh
# Импорт Rockchip SDK в docker volume (хосту не нужны ни tar, ни место в $HOME).
#   sh import-sdk.sh /path/to/rv1126_rv1109_linux_ai_camera_release_20210306.tgz
# Рядом лежащий *.md5 проверяется автоматически. Если архив содержит только
# .repo (стандартная поставка Rockchip), дерево разворачивается `repo sync -l`.
# Volume: MTV3_SDK_VOLUME (по умолчанию mtv3-rv1126-sdk).
# Нужно ~25 ГБ под распаковку и ~60 ГБ после полной сборки в docker data-root.
set -eu

[ $# -eq 1 ] || { sed -n '2,8p' "$0" >&2; exit 2; }
TGZ=$1
[ -f "$TGZ" ] || { echo "no such file: $TGZ" >&2; exit 1; }

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IMAGE=${MTV3_SDK_IMAGE:-mtv3-rv1126-sdk}
TAG=${MTV3_SDK_TAG:-latest}
SDK_VOLUME=${MTV3_SDK_VOLUME:-mtv3-rv1126-sdk}

TGZ_DIR=$(CDPATH= cd -- "$(dirname -- "$TGZ")" && pwd)
TGZ_NAME=$(basename -- "$TGZ")

if ! docker image inspect "$IMAGE:$TAG" >/dev/null 2>&1; then
    echo "image $IMAGE:$TAG not found; run: sh $SCRIPT_DIR/build-image.sh" >&2
    exit 1
fi

if docker run --rm --platform linux/amd64 --env MTV3_QUIET=1 \
        --volume "$SDK_VOLUME:/proj/rv1126_sdk_ai" \
        "$IMAGE:$TAG" test -e /proj/rv1126_sdk_ai/build.sh; then
    echo "volume $SDK_VOLUME already contains an SDK (build.sh present)." >&2
    echo "Remove it first if you really want to re-import: docker volume rm $SDK_VOLUME" >&2
    exit 1
fi

exec docker run --rm --platform linux/amd64 --env MTV3_QUIET=1 \
    --volume "$SDK_VOLUME:/proj/rv1126_sdk_ai" \
    --volume "$TGZ_DIR:/import:ro" \
    --env "TGZ_NAME=$TGZ_NAME" \
    "$IMAGE:$TAG" bash -eu -c '
set -o pipefail
cd /import
if [ -f "$TGZ_NAME.md5" ]; then
    echo "[import] verifying md5 ..."
    md5sum -c "$TGZ_NAME.md5"
elif [ -f "${TGZ_NAME%.tgz}.md5" ]; then
    echo "[import] verifying md5 ..."
    md5sum -c "${TGZ_NAME%.tgz}.md5"
else
    echo "[import] no .md5 next to the archive, skipping checksum"
fi
echo "[import] extracting $TGZ_NAME into /proj/rv1126_sdk_ai ..."
tar -xzf "$TGZ_NAME" -C /proj/rv1126_sdk_ai --checkpoint=5000 --checkpoint-action=dot
echo
cd /proj/rv1126_sdk_ai
# архив с одним каталогом верхнего уровня -> поднять содержимое
if [ ! -e build.sh ] && [ ! -d .repo ]; then
    set -- * .[!.]*
    entries=0; top=
    for e in "$@"; do [ -e "$e" ] || continue; entries=$((entries+1)); top=$e; done
    if [ "$entries" -eq 1 ] && [ -d "$top" ]; then
        echo "[import] flattening $top/"
        (cd "$top" && tar -cf - .) | tar -xf - -C . && rm -rf "$top"
    fi
fi
if [ ! -e build.sh ] && [ -d .repo ]; then
    echo "[import] .repo found, checking out the tree (repo sync -l) ..."
    if [ -x .repo/repo/repo ]; then
        .repo/repo/repo sync -l -j"$(nproc)"
    else
        echo "[import] .repo/repo/repo missing; install repo launcher and run: repo sync -l" >&2
        exit 1
    fi
fi
if [ -e build.sh ]; then
    echo "[import] SDK ready: $(ls -d */ | tr "\n" " ")"
    du -sh /proj/rv1126_sdk_ai 2>/dev/null | sed "s/^/[import] size: /"
else
    echo "[import] archive extracted, but build.sh is still missing — check the layout" >&2
    exit 1
fi
'
