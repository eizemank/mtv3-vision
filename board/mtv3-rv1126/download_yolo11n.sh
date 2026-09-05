#!/bin/sh
set -eu

DEST_DIR="${1:-../../common/models}"
MODEL_URL="https://huggingface.co/webnn/yolo11n/resolve/main/onnx/yolo11n.onnx?download=true"
MODEL_SHA256="7d8fd1717d9d5bbab6986cd134afb620649c7a394303d55b1e09fc00804cc5c1"
LABELS_URL="https://raw.githubusercontent.com/pjreddie/darknet/master/data/coco.names"

mkdir -p "$DEST_DIR"
MODEL="$DEST_DIR/yolo11n.onnx"
PART="$MODEL.part"

curl --fail --location --retry 8 --retry-all-errors --connect-timeout 20 \
    --continue-at - --output "$PART" "$MODEL_URL"
echo "$MODEL_SHA256  $PART" | sha256sum -c -
mv "$PART" "$MODEL"

curl --fail --location --retry 8 --retry-all-errors --connect-timeout 20 \
    --output "$DEST_DIR/coco.names" "$LABELS_URL"

echo "Downloaded $MODEL and $DEST_DIR/coco.names"
