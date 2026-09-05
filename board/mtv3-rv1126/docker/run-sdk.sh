#!/bin/sh
set -eu

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SDK_DIR=${RV1126_SDK:-$HOME/proj/rv1126_sdk_ai}

[ -d "$SDK_DIR" ] || {
    echo "RV1126 SDK directory not found: $SDK_DIR" >&2
    echo "Set RV1126_SDK=/absolute/path/to/rv1126_sdk_ai" >&2
    exit 1
}

exec docker run --rm -it \
    --hostname mtv3-sdk \
    --volume "$SDK_DIR:/proj/rv1126_sdk_ai" \
    --volume "$REPO_DIR:/work/mtv3-vision" \
    --workdir /work/mtv3-vision \
    mtv3-rv1126-sdk:ubuntu20.04 "$@"
