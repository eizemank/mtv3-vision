#!/usr/bin/env python3
"""train_yolo.py — дообучение YOLO (Ultralytics) на хосте и экспорт ONNX для SeeSharp.

Два способа задать параметры:
  1. Из config.json (секция training: data_yaml, base_model, epochs, image_size,
     output_onnx); путь к модели записывается обратно в object_detection:
       python3 train_yolo.py build-cm5/config.json
  2. Явно, без config.json — например, для zip, скачанного из вкладки
     «Обучение» (yolo/dataset.yaml):
       python3 train_yolo.py --data yolo/dataset.yaml --epochs 50 --imgsz 640 \
           --base-model yolo11n.pt --out custom_yolo.onnx
     Затем custom_yolo.onnx загружается через вкладку «Обучение» (тип YOLO,
     классы — из dataset.yaml, печатаются в конце).

Экспорт: opset 12, статические размеры, без встроенного NMS — так ждёт
YoloProcessor (cv::dnn) на CM5.
"""
import argparse
import json
import shutil
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("config", nargs="?", help="config.json (режим 1)")
    parser.add_argument("--data", help="dataset.yaml (переопределяет training.data_yaml)")
    parser.add_argument("--epochs", type=int)
    parser.add_argument("--imgsz", type=int, help="размер входа (training.image_size)")
    parser.add_argument("--base-model", help="стартовые веса (training.base_model)")
    parser.add_argument("--out", help="выходной ONNX (training.output_onnx)")
    parser.add_argument("--no-config", action="store_true",
                        help="не трогать config.json даже если он указан")
    args = parser.parse_args()

    from ultralytics import YOLO

    config = {}
    config_path = None
    if args.config and not args.no_config:
        config_path = Path(args.config).resolve()
        config = json.loads(config_path.read_text())
    training = config.get("training", {})
    base_dir = config_path.parent if config_path else Path.cwd()

    dataset = Path(args.data or training.get("data_yaml", "dataset.yaml"))
    epochs = int(args.epochs or training.get("epochs", 50))
    image_size = int(args.imgsz or training.get("image_size", 640))
    base_model = args.base_model or training.get("base_model", "yolo11n.pt")
    output = Path(args.out or training.get("output_onnx", "custom_yolo.onnx"))
    if not dataset.is_absolute():
        dataset = base_dir / dataset
    if not output.is_absolute():
        output = base_dir / output
    if not dataset.exists():
        sys.exit("dataset.yaml not found: %s" % dataset)

    model = YOLO(base_model)
    model.train(data=str(dataset), epochs=epochs, imgsz=image_size)
    best_model = YOLO(str(model.trainer.best))
    exported = Path(best_model.export(format="onnx", imgsz=image_size, opset=12,
                                      simplify=True, dynamic=False, nms=False))
    output.parent.mkdir(parents=True, exist_ok=True)
    if exported.resolve() != output.resolve():
        shutil.copy2(exported, output)

    names = getattr(best_model, "names", None) or {}
    class_names = [names[k] for k in sorted(names)] if isinstance(names, dict) else list(names)
    if config_path is not None:
        config.setdefault("object_detection", {})
        config["object_detection"]["model_onnx"] = str(output.name)
        config["object_detection"]["input_width"] = image_size
        config["object_detection"]["input_height"] = image_size
        if class_names:
            config["object_detection"]["class_names"] = class_names
            config["object_detection"]["class_names_file"] = ""
        config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
    print("trained YOLO model: %s" % output)
    print("input size: %d, classes: %s" % (image_size, json.dumps(class_names, ensure_ascii=False)))
    print("upload it in the Training tab with kind=yolo and these class names.")


if __name__ == "__main__":
    main()
