#!/usr/bin/env python3
"""simple_classifier.py — простая CNN-классификация кадров MTV3 (PyTorch).

Обучение:  python3 simple_classifier.py train  <data_dir> [epochs]
           data_dir/<class_name>/*.jpg — папка на класс (ImageFolder).
Экспорт:   python3 simple_classifier.py export
           -> ../models/simple_classifier.onnx (opset 10 — rknn-toolkit 1.6.1)
Проверка:  python3 simple_classifier.py test <image.jpg>

Артефакты (веса/классы/onnx) складываются в sw/common/models/.

Контракт препроцессинга (обязан совпадать с onnx2rknn.py и C++ инференсом):
  вход 1x3x64x64, RGB, нормализация x/255 (ToTensor), без mean/std.

Архитектура сознательно из "скучных" операторов (Conv/BN/ReLU/AvgPool/Gemm) —
все штатно поддержаны rknn-toolkit 1.6.1, квантование в uint8 переживают без
сюрпризов. ~30k параметров, на NPU RV1126 инференс <1 мс.
"""

import json
import sys
from pathlib import Path

import torch
import torch.nn as nn

INPUT = 64
MODELS = Path(__file__).resolve().parent.parent / "models"
WEIGHTS = MODELS / "simple_classifier.pt"
CLASSES = MODELS / "simple_classifier_classes.json"
ONNX = MODELS / "simple_classifier.onnx"


class SimpleNet(nn.Module):
    def __init__(self, num_classes):
        super().__init__()
        def block(cin, cout):
            return nn.Sequential(
                nn.Conv2d(cin, cout, 3, stride=2, padding=1, bias=False),
                nn.BatchNorm2d(cout),
                nn.ReLU(inplace=True),
            )
        self.features = nn.Sequential(
            block(3, 16),    # 64 -> 32
            block(16, 32),   # 32 -> 16
            block(32, 64),   # 16 -> 8
        )
        self.pool = nn.AvgPool2d(8)          # 8x8 -> 1x1 (без Adaptive*)
        self.fc = nn.Linear(64, num_classes)

    def forward(self, x):
        x = self.features(x)
        x = self.pool(x)
        x = torch.flatten(x, 1)
        return self.fc(x)                    # логиты; softmax на стороне движка


def train(data_dir, epochs=20):
    from torch.utils.data import DataLoader
    from torchvision import datasets, transforms

    tf = transforms.Compose([
        transforms.Resize((INPUT, INPUT)),
        transforms.ColorJitter(0.2, 0.2, 0.2),
        transforms.RandomHorizontalFlip(),
        transforms.ToTensor(),               # /255, RGB — контракт
    ])
    ds = datasets.ImageFolder(data_dir, tf)
    print("classes:", ds.classes, " samples:", len(ds))
    dl = DataLoader(ds, batch_size=32, shuffle=True, num_workers=2)

    net = SimpleNet(len(ds.classes))
    opt = torch.optim.Adam(net.parameters(), lr=1e-3)
    lossf = nn.CrossEntropyLoss()

    for ep in range(epochs):
        net.train()
        tot = correct = 0
        loss_sum = 0.0
        for x, y in dl:
            opt.zero_grad()
            out = net(x)
            loss = lossf(out, y)
            loss.backward()
            opt.step()
            loss_sum += loss.item() * y.size(0)
            correct += (out.argmax(1) == y).sum().item()
            tot += y.size(0)
        print("epoch %2d  loss=%.4f  acc=%.3f" %
              (ep + 1, loss_sum / tot, correct / tot))

    MODELS.mkdir(parents=True, exist_ok=True)
    torch.save(net.state_dict(), WEIGHTS)
    CLASSES.write_text(json.dumps(ds.classes, ensure_ascii=False))
    print("saved:", WEIGHTS, CLASSES)
    print('-> впишите классы в common/config/config.json ("class_names": %s)' % ds.classes)


def load_net():
    classes = json.loads(CLASSES.read_text()) if CLASSES.exists() else ["c0", "c1"]
    net = SimpleNet(len(classes))
    if WEIGHTS.exists():
        net.load_state_dict(torch.load(WEIGHTS, map_location="cpu"))
    else:
        print("WARN: весов нет, экспортирую необученную сеть (для прогона тракта)")
    net.eval()
    return net, classes


def export():
    net, classes = load_net()
    MODELS.mkdir(parents=True, exist_ok=True)
    dummy = torch.zeros(1, 3, INPUT, INPUT)
    torch.onnx.export(net, dummy, str(ONNX), opset_version=10,
                      input_names=["input"], output_names=["logits"])
    print("saved:", ONNX, " classes:", classes)


def test(img_path):
    import numpy as np
    from PIL import Image

    net, classes = load_net()
    img = Image.open(img_path).convert("RGB").resize((INPUT, INPUT))
    x = torch.from_numpy(np.asarray(img)).permute(2, 0, 1).float() / 255.0
    with torch.no_grad():
        p = torch.softmax(net(x.unsqueeze(0)), 1)[0]
    for i in p.argsort(descending=True):
        print("%-20s %.3f" % (classes[i] if i < len(classes) else i, p[i]))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
    elif sys.argv[1] == "train":
        train(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 20)
    elif sys.argv[1] == "export":
        export()
    elif sys.argv[1] == "test":
        test(sys.argv[2])
    else:
        print(__doc__)
