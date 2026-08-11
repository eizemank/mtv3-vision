#!/usr/bin/env python3
"""onnx2rknn.py — конвертация simple_classifier.onnx -> .rknn (rknn-toolkit 1.6.1).

Запуск в окружении toolkit (conda rknn, python 3.6, AVX обязателен):
    python onnx2rknn.py ../models/simple_classifier.onnx dataset.txt
    python onnx2rknn.py ../models/simple_classifier.onnx    # без квантования

dataset.txt — по одному пути к jpg/png на строку (50-200 реальных кадров с
камеры). Кадры из nv12-дампов демона:
    ffmpeg -f rawvideo -pix_fmt nv12 -s 1056x784 -i cam.nv12 q_%03d.jpg
    ls $PWD/q_*.jpg > dataset.txt

Контракт препроцессинга (совпадает с обучением и инференсом движков):
  RGB, x/255 -> channel_mean_value '0 0 0 255', reorder_channel '0 1 2'.
На борту вход подаётся uint8 RGB NHWC как есть (нормализация зашита в модель).
"""

import sys

from rknn.api import RKNN


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    onnx = sys.argv[1]
    dataset = sys.argv[2] if len(sys.argv) > 2 else None
    out = onnx.rsplit(".", 1)[0] + ".rknn"

    rknn = RKNN(verbose=False)
    rknn.config(
        mean_values=[[0, 0, 0]],          # (x - 0)/255
        std_values=[[255, 255, 255]],
        reorder_channel="0 1 2",          # вход уже RGB — не переставлять
        target_platform=["rv1126"],       # без этого rknn_init -13 на борту!
        quantized_dtype="asymmetric_quantized-u8",
    )

    if rknn.load_onnx(model=onnx) != 0:
        sys.exit("load_onnx failed")

    if dataset:
        ret = rknn.build(do_quantization=True, dataset=dataset)
    else:
        ret = rknn.build(do_quantization=False)
        print("WARN: без квантования (fp16) — медленнее на NPU; дайте dataset.txt")
    if ret != 0:
        sys.exit("build failed")

    if rknn.export_rknn(out) != 0:
        sys.exit("export failed")
    print("saved:", out)

    # быстрый smoke-тест на симуляторе
    import numpy as np
    if rknn.init_runtime() == 0:
        fake = np.random.randint(0, 255, (64, 64, 3), dtype=np.uint8)
        outs = rknn.inference(inputs=[fake])
        print("sim ok, out shape:", [o.shape for o in outs])
    rknn.release()


if __name__ == "__main__":
    main()
