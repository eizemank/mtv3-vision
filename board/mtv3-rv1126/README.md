# MTV3 (RV1126) — портирование SeeSharp (C++) на модуль

Камерный тракт: `mtv3_cam_daemon` (MP без rkaiq/ISPP, свои AE/AWB/CCM) публикует
NV12-кадры в `/dev/shm/mtv3cam`; приложение — C++ SeeSharp
(`sw/cpp/SeeSharp`, перенесён из legacy), собранный с `-DMTV3_BOARD=ON`:
источник кадров `ShmSource` вместо `cv::VideoCapture`, headless-сток вместо
imshow/VideoWriter.

Python-ветка (SeeSharpPy + py36ify) отменена: python3 для target в buildroot
2018.02 собирается с конфликтами. Файлы (`py36ify.py`,
`SeeSharpPy/.../shm_source.py`) оставлены на случай возврата.

## Состав board/mtv3-rv1126

| Файл | Назначение |
|---|---|
| `mtv3_cam_daemon.c` | демон камеры; `--shm` (ринг NV12, 3 слота, seq); wbGain автоселекта CCM заполнены |
| `init_mp.sh` | поднятие media-графа (мост OFF, mainpath ON) — до демона |
| `S60mtv3` | автостарт: init → демон (respawn) → mainCV (respawn) |
| `Makefile` | кросс-сборка демона (вне conda!) |
| `toolchain-rv1126.cmake` | CMake-тулчейн для сборки SeeSharp под RV1126 |
| `opencv3-aruco.mk.add` | подключение contrib/aruco к opencv3 3.3.0 (инструкция в шапке) |

Дерево SeeSharp переработано по образцу SeeSharpPy:

* `pipeline/` — `IFrameSource` (интерфейс), `CameraSource` (десктоп),
  `ShmSource` (модуль), `Pipeline` (3 потока, bounded-очереди с дропом,
  sink-колбэк вместо display-потока); `main.cpp` — composition root.
* `processing/classifier_processor` — NN-классификатор кадра:
  десктоп — cv::dnn + ONNX, модуль — NPU через `nn/rknn_classifier`
  (librknn_api). Выбор: `"processing_mode": "classification"` в config.json,
  секция `classification` там же (пути моделей, классы, порог).
* CMake: опция `MTV3_BOARD` (shm-источник, headless, rknn), fallback на
  вендоренный nlohmann_json.

## Нейросеть-классификатор (sw/common/nn/)

Контракт препроцессинга везде одинаков: RGB, 64x64, x/255.
Артефакты — в `sw/common/models/`.

1. `simple_classifier.py train <data_dir>` — обучение (папка на класс);
   `export` → `models/simple_classifier.onnx` (opset 10). Требует PyTorch на хосте.
2. Десктоп-проверка: положить onnx рядом с mainCV, режим classification.
3. `onnx2rknn.py simple_classifier.onnx dataset.txt` — в окружении
   rknn-toolkit 1.6.1 (conda rknn, AVX); dataset.txt — 50-200 jpg с камеры
   (из nv12-дампов, команды в шапке скрипта). → `simple_classifier.rknn`.
4. На модуль: `/opt/seesharp/simple_classifier.rknn` (+ config.json с
   классами). NPU должен быть жив (galcore, частота ограничена 600 МГц).

## Порядок действий

1. **Rootfs (defconfig, сборка вне conda).** Python-строки, если добавлялись,
   убрать (`BR2_PACKAGE_PYTHON3`, `BR2_PACKAGE_PYTHON_NUMPY`,
   `BR2_PACKAGE_OPENCV3_LIB_PYTHON`). Оставить/добавить:
   ```
   BR2_PACKAGE_OPENCV3=y
   BR2_PACKAGE_OPENCV3_LIB_CALIB3D=y
   BR2_PACKAGE_OPENCV3_LIB_IMGCODECS=y
   BR2_PACKAGE_OPENCV3_WITH_JPEG=y
   BR2_PACKAGE_OPENCV3_WITH_PNG=y
   ```
   (calib3d нужен aruco и тянет imgproc/features2d; highgui/videoio не нужны.)
   Aruco: применить `opencv3-aruco.mk.add`. Затем `./build.sh rootfs` и
   обязательный readback: `grep OPENCV3 buildroot/output/rockchip_*/.config`.

2. **Приложение** (хост):
   ```
   cd sw/cpp/SeeSharp
   mkdir -p third_party/nlohmann
   wget -O third_party/nlohmann/json.hpp \
     https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
   mkdir build-rv1126 && cd build-rv1126
   cmake -DCMAKE_TOOLCHAIN_FILE=../../../board/mtv3-rv1126/toolchain-rv1126.cmake \
         -DMTV3_BOARD=ON ..
   make -j$(nproc)
   ```
   Требование: rootfs из п.1 уже собран (OpenCVConfig.cmake живёт в staging).

3. **Демон:** `make` в board/mtv3-rv1126 → `mtv3_cam_daemon`.

4. **Ручной прогон на борту:**
   ```
   ./init_mp.sh 2112 1568
   ./mtv3_cam_daemon --shm --ccm auto -s 108 --hz 50 -w 1056 -h 784 &
   cd /opt/seesharp && ./mainCV        # печатает fps каждые 100 кадров
   ```
   CV-разрешение задаёт `-w/-h` демона (скейлит resizer mainpath; сенсор
   остаётся в 2112x1568).

5. **Прошивка:** overlay rootfs: `/usr/bin/{mtv3_cam_daemon,init_mp.sh}`,
   `/opt/seesharp/{mainCV,config.json}`, `/etc/init.d/S60mtv3` (chmod +x).
   Автостарты ispserver/aiserver/uvc_app убрать из defconfig (S60mtv3 их
   дополнительно убивает).

6. **Проверка:** полная перепрошивка → автостарт → `/tmp/mtv3.*.log`, fps из
   лога mainCV; отсюда финальное CV-разрешение.

## Протокол shm (v1)

`/dev/shm/mtv3cam`: заголовок 64 Б little-endian
`u32 magic("M3SH"=0x4D335348), version, w, h, stride, fmt(0=NV12), nslot(3),
slot_size, seq, ts_ms, reserved[6]`, далее слоты подряд.
`seq` — число завершённых кадров, последний кадр в слоте `(seq-1) % nslot`;
писатель инкрементирует seq после memcpy (с барьером), читатель перечитывает
seq после копии — расхождение ≥ nslot-1 означает порванный кадр (ретрай).
Реализации: демон (писатель), `SeeSharp .../pipeline/shm_source.{hpp,cpp}` (C++),
`SeeSharpPy .../pipeline/shm_source.py` (python, для отладки на хосте).
