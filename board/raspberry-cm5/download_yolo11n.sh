#!/bin/sh
set -eu

DEST_DIR="${1:-.}"
CONFIG_PATH="${2:-$DEST_DIR/config.json}"
MODEL_URL="https://huggingface.co/webnn/yolo11n/resolve/main/onnx/yolo11n.onnx?download=true"
LABELS_URL="https://raw.githubusercontent.com/pjreddie/darknet/master/data/coco.names"

mkdir -p "$DEST_DIR"
MODEL_PART="$DEST_DIR/yolo11n.onnx.part"
curl --fail --location --retry 8 --retry-all-errors --connect-timeout 20 \
    --continue-at - --output "$MODEL_PART" "$MODEL_URL"
if [ "$(wc -c < "$MODEL_PART")" -lt 1000000 ]; then
    echo "Downloaded ONNX file is unexpectedly small" >&2
    exit 1
fi
mv "$MODEL_PART" "$DEST_DIR/yolo11n.onnx"
curl --fail --location --retry 8 --retry-all-errors --connect-timeout 20 \
    --output "$DEST_DIR/coco.names" "$LABELS_URL"

python3 - "$CONFIG_PATH" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
config = json.loads(path.read_text())
config["object_detection"] = {
    "model_onnx": "yolo11n.onnx",
    "class_names_file": "coco.names",
    "class_names": [],
    "input_width": 640,
    "input_height": 640,
    "confidence_threshold": 0.35,
    "nms_threshold": 0.45,
    "max_objects": 10
}
config["general_params"]["processing_mode"] = "object_detection"
path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
PY

echo "YOLO11n ONNX, COCO labels and object_detection config installed in $DEST_DIR"
