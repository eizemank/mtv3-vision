#!/bin/sh
set -eu

DEST_DIR="${1:-.}"
CONFIG_PATH="${2:-$DEST_DIR/config.json}"
MODEL_URL="https://huggingface.co/onnxmodelzoo/mobilenetv2-12/resolve/main/mobilenetv2-12.onnx?download=true"
LABELS_URL="https://raw.githubusercontent.com/pytorch/hub/master/imagenet_classes.txt"
MODEL_SHA256="c0c3f76d93fa3fd6580652a45618618a220fced18babf65774ed169de0432ad5"

mkdir -p "$DEST_DIR"
curl --fail --location --retry 3 --output "$DEST_DIR/mobilenetv2-12.onnx" "$MODEL_URL"
printf '%s  %s\n' "$MODEL_SHA256" "$DEST_DIR/mobilenetv2-12.onnx" | sha256sum --check -
curl --fail --location --retry 3 --output "$DEST_DIR/imagenet_classes.txt" "$LABELS_URL"
LABEL_COUNT="$(awk 'END { print NR }' "$DEST_DIR/imagenet_classes.txt")"
if [ "$LABEL_COUNT" -ne 1000 ]; then
    echo "Expected 1000 ImageNet labels, downloaded $LABEL_COUNT" >&2
    exit 1
fi

python3 - "$CONFIG_PATH" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
config = json.loads(path.read_text())
config["classification"] = {
    "model_onnx": "mobilenetv2-12.onnx",
    "model_rknn": "simple_classifier.rknn",
    "input_size": 224,
    "resize_size": 256,
    "center_crop": True,
    "swap_rb": True,
    "mean": [0.485, 0.456, 0.406],
    "std": [0.229, 0.224, 0.225],
    "score_threshold": 0.1,
    "class_names_file": "imagenet_classes.txt",
    "class_names": [],
}
path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
PY

echo "MobileNetV2, ImageNet labels and classification config installed in $DEST_DIR"
