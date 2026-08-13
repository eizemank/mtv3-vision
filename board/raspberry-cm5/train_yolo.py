#!/usr/bin/env python3
import json
import shutil
import sys
from pathlib import Path

from ultralytics import YOLO


config_path = Path(sys.argv[1] if len(sys.argv) > 1 else "config.json").resolve()
config = json.loads(config_path.read_text())
training = config.get("training", {})
dataset = Path(training.get("data_yaml", "dataset.yaml"))
epochs = int(training.get("epochs", 50))
image_size = int(training.get("image_size", 640))
base_model = training.get("base_model", "yolo11n.pt")
output = Path(training.get("output_onnx", "custom_yolo.onnx"))
if not dataset.is_absolute():
    dataset = config_path.parent / dataset
if not output.is_absolute():
    output = config_path.parent / output

model = YOLO(base_model)
model.train(data=str(dataset), epochs=epochs, imgsz=image_size)
best_model = YOLO(str(model.trainer.best))
exported = Path(best_model.export(format="onnx", imgsz=image_size, opset=12,
                                  simplify=True, dynamic=False, nms=False))
output.parent.mkdir(parents=True, exist_ok=True)
if exported.resolve() != output.resolve():
    shutil.copy2(exported, output)

config["object_detection"]["model_onnx"] = str(output.name)
config["object_detection"]["input_width"] = image_size
config["object_detection"]["input_height"] = image_size
config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
print(f"trained YOLO model: {output}")
