#!/bin/bash
# Точка входа контейнера: проверить SDK и подсказать, что делать дальше.
set -eu

sdk="${RV1126_SDK:-/proj/rv1126_sdk_ai}"

if [ -w "$(dirname "$sdk")" ] && [ ! -d "$sdk" ]; then
    mkdir -p "$sdk"
fi

if [ ! -x "$sdk/build.sh" ] && [ "${MTV3_QUIET:-0}" != 1 ]; then
    cat >&2 <<MSG
[mtv3-sdk] SDK not found in $sdk (no build.sh).
[mtv3-sdk] Import it from the host:
[mtv3-sdk]   sh board/mtv3-rv1126/docker/import-sdk.sh /path/to/rv1126_rv1109_linux_ai_camera_release_20210306.tgz
[mtv3-sdk] or mount an unpacked tree: RV1126_SDK=/path/to/rv1126_sdk_ai sh board/mtv3-rv1126/docker/run-sdk.sh
MSG
fi

# repo init / git am требуют identity; домашний каталог — volume, запись одноразовая.
if ! git config --global user.email >/dev/null 2>&1; then
    git config --global user.email "${GIT_AUTHOR_EMAIL:-builder@mtv3-sdk}"
    git config --global user.name  "${GIT_AUTHOR_NAME:-MTV3 builder}"
    git config --global color.ui auto
fi

exec "$@"
