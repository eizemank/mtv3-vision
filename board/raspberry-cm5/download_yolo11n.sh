#!/bin/sh
set -eu

DEST_DIR="${1:-.}"
CONFIG_PATH="${2:-$DEST_DIR/config.json}"
VENV="$DEST_DIR/.yolo-export"
MODEL_URL="https://github.com/ultralytics/assets/releases/download/v8.4.0/yolo11n.pt"
LABELS_URL="https://raw.githubusercontent.com/pjreddie/darknet/master/data/coco.names"

mkdir -p "$DEST_DIR"
python3 -m venv "$VENV"
"$VENV/bin/pip" install --disable-pip-version-check --quiet ultralytics onnx
curl --fail --location --retry 3 --output "$DEST_DIR/yolo11n.pt" "$MODEL_URL"
curl --fail --location --retry 3 --output "$DEST_DIR/coco.names" "$LABELS_URL"

"$VENV/bin/python" - "$DEST_DIR" <<'PY'
import sys
from pathlib import Path
from ultralytics import YOLO

destination = Path(sys.argv[1]).resolve()
model = YOLO(str(destination / "yolo11n.pt"))
exported = Path(model.export(format="onnx", imgsz=640, opset=12,
                             simplify=True, dynamic=False, nms=False))
target = destination / "yolo11n.onnx"
if exported.resolve() != target.resolve():
    target.write_bytes(exported.read_bytes())
PY

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
