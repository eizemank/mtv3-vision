#!/usr/bin/env python3
"""Convert a single-output YOLO ONNX model for the RV1126 NPU.

Run inside the RKNN Toolkit 1.6.1 environment:
  python onnx2rknn_yolo.py yolo11n.onnx dataset.txt --size 640
  python onnx2rknn_yolo.py yolov5n.onnx dataset.txt --size 640

The exported model must return decoded boxes in one tensor:
  YOLOv8/11: [1, 4 + classes, predictions]
  YOLOv5:    [1, predictions, 5 + classes]

dataset.txt contains one calibration image path per line. The application sends
uint8 RGB NHWC input; normalization by 255 is embedded into the RKNN model.
"""

import argparse
import os
import sys

import numpy as np
from rknn.api import RKNN


def require_avx():
    cpuinfo = "/proc/cpuinfo"
    if not os.path.exists(cpuinfo):
        return
    with open(cpuinfo, "r") as stream:
        flags = stream.read().lower().split()
    if "avx" not in flags:
        sys.exit(
            "RKNN Toolkit requires AVX, but AVX is not exposed to this Linux "
            "environment. Enable host CPU passthrough/disable CPU compatibility "
            "mode in the hypervisor, or run conversion on another x86-64 host."
        )


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("onnx", help="source ONNX model")
    parser.add_argument("dataset", nargs="?", help="quantization image list")
    parser.add_argument("--output", help="output .rknn path")
    parser.add_argument("--size", type=int, default=640, help="square input size")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument(
        "--check-runtime",
        action="store_true",
        help="run the x86 RKNN simulator after export (requires a compatible CPU)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    require_avx()
    output = args.output or os.path.splitext(args.onnx)[0] + ".rknn"
    print("[1/4] configuring RKNN", flush=True)
    rknn = RKNN(verbose=args.verbose)
    result = rknn.config(
        channel_mean_value="0 0 0 255",
        reorder_channel="0 1 2",
        target_platform=["rv1126"],
        quantized_dtype="asymmetric_quantized-u8",
    )
    if result != 0:
        sys.exit("rknn.config failed: {}".format(result))
    print("[2/4] loading ONNX: {}".format(args.onnx), flush=True)
    if rknn.load_onnx(model=args.onnx) != 0:
        sys.exit("load_onnx failed")
    print("[3/4] building{}".format(
        " with quantization" if args.dataset is not None else ""), flush=True)
    result = rknn.build(do_quantization=args.dataset is not None,
                        dataset=args.dataset)
    if result != 0:
        sys.exit("build failed: {}".format(result))
    if args.dataset is None:
        print("WARN: model is not quantized; NPU inference will be slower")
    print("[4/4] exporting RKNN: {}".format(output), flush=True)
    if rknn.export_rknn(output) != 0:
        sys.exit("export_rknn failed")
    print("saved:", output, flush=True)

    if args.check_runtime and rknn.init_runtime() == 0:
        image = np.random.randint(0, 256, (args.size, args.size, 3),
                                  dtype=np.uint8)
        outputs = rknn.inference(inputs=[image])
        print("simulator output shapes:", [item.shape for item in outputs])
        if len(outputs) != 1:
            sys.exit("SeeSharp RKNN YOLO requires exactly one output tensor")
    rknn.release()


if __name__ == "__main__":
    main()
