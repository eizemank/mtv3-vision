#!/usr/bin/env python3
"""simple_classifier.py — простая CNN-классификация областей кадра (PyTorch).

Обучение:  python3 simple_classifier.py train --data <data_dir> --out <models_dir>
               [--epochs 20] [--image-size 64] [--batch 32] [--workers 0]
               [--threads 2] [--val-split 0.1] [--seed 0] [--name simple_classifier]
               [--stamp] [--progress] [--export]
           data_dir/<class_name>/*.jpg — папка на класс (ImageFolder).
           Старая форма `train <data_dir> [epochs]` тоже работает
           (артефакты тогда в ../models/).
Экспорт:   python3 simple_classifier.py export [--weights F] [--classes F] [--out F.onnx]
           -> ../models/simple_classifier.onnx (opset 10 — rknn-toolkit 1.6.1)
Проверка:  python3 simple_classifier.py test <image.jpg> [--weights F] [--classes F]

Машиночитаемый прогресс (--progress, читает TrainingService в SeeSharp):
  PROGRESS {"epoch":1,"epochs":20,"loss":0.61,"acc":0.72,"val_loss":..,"val_acc":..}
  RESULT   {"onnx":"...","weights":"...","classes_file":"...","classes":[...],
            "input_size":64,"best_val_acc":0.95}

Контракт препроцессинга (обязан совпадать с onnx2rknn.py и C++ инференсом):
  вход 1x3xSxS, RGB, нормализация x/255 (ToTensor), без mean/std.

Архитектура сознательно из "скучных" операторов (Conv/BN/ReLU/AvgPool/Gemm) —
все штатно поддержаны rknn-toolkit 1.6.1, квантование в uint8 переживают без
сюрпризов. ~30k параметров, на NPU RV1126 инференс <1 мс.
Синтаксис держим совместимым с Python 3.6 (conda-окружение rknn-toolkit).
"""

import argparse
import json
import os
import random
import sys
import time
from datetime import datetime
from pathlib import Path

MODELS = Path(__file__).resolve().parent.parent / "models"
DEFAULT_NAME = "simple_classifier"
DEFAULT_INPUT = 64


def _need_torch():
    try:
        import torch  # noqa: F401
        import torch.nn  # noqa: F401
    except ImportError as error:
        sys.stderr.write(
            "PyTorch is not installed: %s\n"
            "Install CPU wheels: pip install --index-url https://download.pytorch.org/whl/cpu torch torchvision\n"
            % error)
        sys.exit(3)


def build_net(num_classes, input_size):
    import torch.nn as nn

    class SimpleNet(nn.Module):
        def __init__(self):
            super(SimpleNet, self).__init__()

            def block(cin, cout):
                return nn.Sequential(
                    nn.Conv2d(cin, cout, 3, stride=2, padding=1, bias=False),
                    nn.BatchNorm2d(cout),
                    nn.ReLU(inplace=True),
                )
            self.features = nn.Sequential(
                block(3, 16),    # S -> S/2
                block(16, 32),   # S/2 -> S/4
                block(32, 64),   # S/4 -> S/8
            )
            self.pool = nn.AvgPool2d(input_size // 8)   # S/8 -> 1x1 (без Adaptive*)
            self.fc = nn.Linear(64, num_classes)

        def forward(self, x):
            x = self.features(x)
            x = self.pool(x)
            x = x.flatten(1)
            return self.fc(x)                # логиты; softmax на стороне движка

    if input_size % 8 != 0 or input_size < 16:
        raise SystemExit("image size must be a multiple of 8 and >= 16, got %d" % input_size)
    return SimpleNet()


def emit(kind, payload, enabled):
    if enabled:
        print(kind + " " + json.dumps(payload, ensure_ascii=False), flush=True)


def artefact_paths(out_dir, name, stamp):
    suffix = ("_" + datetime.now().strftime("%Y%m%d_%H%M%S")) if stamp else ""
    base = Path(out_dir) / (name + suffix)
    return {
        "weights": base.with_suffix(".pt"),
        "classes": Path(str(base) + ".classes.json"),
        "onnx": base.with_suffix(".onnx"),
    }


def train(args):
    _need_torch()
    import torch
    import torch.nn as nn
    from torch.utils.data import DataLoader, Subset
    from torchvision import datasets, transforms

    torch.set_num_threads(max(1, args.threads))
    random.seed(args.seed)
    torch.manual_seed(args.seed)

    size = args.image_size
    train_tf = transforms.Compose([
        transforms.Resize((size, size)),
        transforms.ColorJitter(0.2, 0.2, 0.2),
        transforms.RandomHorizontalFlip(),
        transforms.ToTensor(),               # /255, RGB — контракт
    ])
    eval_tf = transforms.Compose([
        transforms.Resize((size, size)),
        transforms.ToTensor(),
    ])
    data_dir = str(args.data)
    try:
        full = datasets.ImageFolder(data_dir, train_tf)
        full_eval = datasets.ImageFolder(data_dir, eval_tf)
    except (FileNotFoundError, RuntimeError) as error:
        sys.stderr.write("dataset error: %s\n" % error)
        sys.exit(2)
    classes = list(full.classes)
    if len(classes) < 2:
        sys.stderr.write("dataset must contain at least 2 class folders with images, got %s\n" % classes)
        sys.exit(2)
    counts = [0] * len(classes)
    for _, label in full.samples:
        counts[label] += 1
    print("classes:", classes, " samples:", len(full), " per class:", counts, flush=True)

    # hold-out валидация: из каждого класса откладываем долю val_split
    # (минимум 1 при >= 4 примерах), чтобы acc в UI была честной
    train_idx, val_idx = [], []
    by_class = {}
    for index, (_, label) in enumerate(full.samples):
        by_class.setdefault(label, []).append(index)
    for label, indices in by_class.items():
        random.shuffle(indices)
        keep = int(round(len(indices) * args.val_split)) if args.val_split > 0 else 0
        if len(indices) >= 4 and args.val_split > 0:
            keep = max(1, keep)
        keep = min(keep, len(indices) - 1)
        val_idx.extend(indices[:keep])
        train_idx.extend(indices[keep:])
    loader = DataLoader(Subset(full, train_idx), batch_size=args.batch, shuffle=True,
                        num_workers=args.workers)
    val_loader = DataLoader(Subset(full_eval, val_idx), batch_size=args.batch,
                            shuffle=False, num_workers=args.workers) if val_idx else None

    net = build_net(len(classes), size)
    opt = torch.optim.Adam(net.parameters(), lr=1e-3)
    lossf = nn.CrossEntropyLoss()

    best_val = None
    best_state = None
    started = time.time()
    for ep in range(args.epochs):
        net.train()
        tot = correct = 0
        loss_sum = 0.0
        for x, y in loader:
            opt.zero_grad()
            out = net(x)
            loss = lossf(out, y)
            loss.backward()
            opt.step()
            loss_sum += loss.item() * y.size(0)
            correct += (out.argmax(1) == y).sum().item()
            tot += y.size(0)
        record = {"epoch": ep + 1, "epochs": args.epochs,
                  "loss": round(loss_sum / max(1, tot), 4),
                  "acc": round(correct / max(1, tot), 4),
                  "elapsed_s": round(time.time() - started, 1)}
        if val_loader is not None:
            net.eval()
            vtot = vcorrect = 0
            vloss = 0.0
            with torch.no_grad():
                for x, y in val_loader:
                    out = net(x)
                    vloss += lossf(out, y).item() * y.size(0)
                    vcorrect += (out.argmax(1) == y).sum().item()
                    vtot += y.size(0)
            record["val_loss"] = round(vloss / max(1, vtot), 4)
            record["val_acc"] = round(vcorrect / max(1, vtot), 4)
            if best_val is None or record["val_acc"] >= best_val:
                best_val = record["val_acc"]
                best_state = {k: v.clone() for k, v in net.state_dict().items()}
        print("epoch %2d  loss=%.4f  acc=%.3f%s" % (
            record["epoch"], record["loss"], record["acc"],
            ("  val_acc=%.3f" % record["val_acc"]) if "val_acc" in record else ""), flush=True)
        emit("PROGRESS", record, args.progress)

    if best_state is not None:
        net.load_state_dict(best_state)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = artefact_paths(out_dir, args.name, args.stamp)
    torch.save(net.state_dict(), str(paths["weights"]))
    meta = {"classes": classes, "input_size": size, "created_ms": int(time.time() * 1000),
            "samples": len(full), "per_class": counts, "epochs": args.epochs,
            "best_val_acc": best_val, "weights": str(paths["weights"])}
    paths["classes"].write_text(json.dumps(meta, ensure_ascii=False, indent=1))
    print("saved:", paths["weights"], paths["classes"], flush=True)

    result = dict(meta)
    result["classes_file"] = str(paths["classes"])
    if args.export:
        export_onnx(net, size, paths["onnx"])
        result["onnx"] = str(paths["onnx"])
        latest = {"onnx": str(paths["onnx"]), "classes_file": str(paths["classes"]),
                  "classes": classes, "input_size": size, "created_ms": meta["created_ms"],
                  "best_val_acc": best_val}
        (out_dir / "latest.json").write_text(json.dumps(latest, ensure_ascii=False, indent=1))
    else:
        print('-> впишите классы в config.json ("class_names": %s)' % classes)
    emit("RESULT", result, args.progress)


def load_net(weights, classes_file, input_size=None):
    import torch

    classes = ["c0", "c1"]
    if classes_file and Path(classes_file).exists():
        loaded = json.loads(Path(classes_file).read_text())
        classes = loaded["classes"] if isinstance(loaded, dict) else loaded
        if isinstance(loaded, dict) and input_size is None:
            input_size = loaded.get("input_size")
    net = build_net(len(classes), input_size or DEFAULT_INPUT)
    if weights and Path(weights).exists():
        net.load_state_dict(torch.load(str(weights), map_location="cpu"))
    else:
        print("WARN: весов нет, экспортирую необученную сеть (для прогона тракта)")
    net.eval()
    return net, classes, input_size or DEFAULT_INPUT


def export_onnx(net, input_size, onnx_path):
    import torch

    Path(onnx_path).parent.mkdir(parents=True, exist_ok=True)
    dummy = torch.zeros(1, 3, input_size, input_size)
    kwargs = dict(opset_version=10, input_names=["input"], output_names=["logits"])
    # torch>=2.6 по умолчанию экспортирует dynamo-экспортёром (opset 18,
    # НЕ уважает opset_version) — rknn-toolkit 1.6.1 такое не читает.
    # Принудительно легаси-экспортёр:
    try:
        torch.onnx.export(net, dummy, str(onnx_path), dynamo=False, **kwargs)
    except TypeError:  # старый torch, параметра dynamo ещё нет
        torch.onnx.export(net, dummy, str(onnx_path), **kwargs)
    try:
        import onnx
        m = onnx.load(str(onnx_path))
        print("opset:", m.opset_import[0].version, "(нужно <= 11)")
    except ImportError:
        pass
    print("saved:", onnx_path, flush=True)


def export(args):
    _need_torch()
    net, classes, size = load_net(args.weights, args.classes, args.image_size)
    export_onnx(net, size, args.out)
    print("classes:", classes)


def test(args):
    _need_torch()
    import numpy as np
    import torch
    from PIL import Image

    net, classes, size = load_net(args.weights, args.classes, args.image_size)
    img = Image.open(args.image).convert("RGB").resize((size, size))
    x = torch.from_numpy(np.asarray(img)).permute(2, 0, 1).float() / 255.0
    with torch.no_grad():
        p = torch.softmax(net(x.unsqueeze(0)), 1)[0]
    for i in p.argsort(descending=True):
        print("%-20s %.3f" % (classes[i] if i < len(classes) else i, p[i]))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("train", help="train on an ImageFolder tree")
    p.add_argument("data_pos", nargs="?", help="data dir (legacy positional form)")
    p.add_argument("epochs_pos", nargs="?", type=int, help="epochs (legacy positional form)")
    p.add_argument("--data", help="data_dir/<class>/*.jpg")
    p.add_argument("--out", default=str(MODELS), help="output dir (default ../models)")
    p.add_argument("--epochs", type=int, default=20)
    p.add_argument("--image-size", type=int, default=DEFAULT_INPUT)
    p.add_argument("--batch", type=int, default=32)
    p.add_argument("--workers", type=int, default=0)
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--val-split", type=float, default=0.1)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--name", default=DEFAULT_NAME)
    p.add_argument("--stamp", action="store_true", help="append a timestamp to artefact names")
    p.add_argument("--progress", action="store_true", help="print PROGRESS/RESULT json lines")
    p.add_argument("--export", action="store_true", help="also export ONNX and write latest.json")
    p.set_defaults(func=train)

    p = sub.add_parser("export", help="export weights to ONNX (opset 10)")
    p.add_argument("--weights", default=str(MODELS / (DEFAULT_NAME + ".pt")))
    p.add_argument("--classes", default=str(MODELS / (DEFAULT_NAME + ".classes.json")))
    p.add_argument("--out", default=str(MODELS / (DEFAULT_NAME + ".onnx")))
    p.add_argument("--image-size", type=int, default=None)
    p.set_defaults(func=export)

    p = sub.add_parser("test", help="classify one image")
    p.add_argument("image")
    p.add_argument("--weights", default=str(MODELS / (DEFAULT_NAME + ".pt")))
    p.add_argument("--classes", default=str(MODELS / (DEFAULT_NAME + ".classes.json")))
    p.add_argument("--image-size", type=int, default=None)
    p.set_defaults(func=test)

    args = parser.parse_args(argv)
    if args.command is None:
        parser.print_help()
        return 1
    if args.command == "train":
        args.data = args.data or args.data_pos
        if args.epochs_pos:
            args.epochs = args.epochs_pos
        if not args.data:
            parser.error("train needs --data <dir>")
        if args.epochs < 1:
            parser.error("--epochs must be >= 1")
    args.func(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
