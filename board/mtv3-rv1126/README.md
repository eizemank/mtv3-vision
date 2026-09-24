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
* Общие транспорты: полные метаданные JSON по UDP, облегчённые бинарные
  метаданные по UART и составной USB gadget (CDC ACM + RNDIS). DXL остаётся
  специфичным для CM5; SystemAdmin доступен на обеих платформах.

## Нейросеть-классификатор (sw/common/nn/)

Контракт препроцессинга везде одинаков: RGB, 64x64, x/255.
Артефакты — в `sw/common/models/`.

1. `simple_classifier.py train --data <data_dir> --out models --export` —
   обучение (папка на класс) и экспорт `models/simple_classifier.onnx`
   (opset 10). Требует PyTorch на хосте. Датасет можно собрать во вкладке
   «Обучение» веб-интерфейса и скачать zip-архивом (`/training/export`).
2. Десктоп-проверка: положить onnx рядом с mainCV, режим classification.
3. `onnx2rknn.py simple_classifier.onnx dataset.txt` — в окружении
   rknn-toolkit 1.6.1 (conda rknn, AVX); dataset.txt — 50-200 jpg с камеры
   (из nv12-дампов, команды в шапке скрипта). → `simple_classifier.rknn`.
4. На модуль: `/opt/seesharp/simple_classifier.rknn` (+ config.json с
   классами). NPU должен быть жив (galcore, частота ограничена 600 МГц).

## YOLO на RKNN

Режим `object_detection` использует `RknnYoloProcessor` и не требует OpenCV
DNN на модуле. Поддерживается одна выходная тензорная матрица с уже
декодированными координатами `cx, cy, w, h`:

* YOLOv8/11: `[1, 4 + classes, predictions]`, `output_layout` =
  `channels_first`, `output_has_objectness` = `false`;
* YOLOv5: `[1, predictions, 5 + classes]`, `output_layout` =
  `predictions_first`, `output_has_objectness` = `true`.

Конвертация выполняется в окружении RKNN Toolkit 1.6.1:

```sh
cd common/config/nn
python onnx2rknn_yolo.py /path/to/yolo11n.onnx dataset.txt --size 640
```

Готовую ONNX-модель и COCO labels можно скачать, а затем преобразовать командами:

```sh
cd board/mtv3-rv1126
make yolo-download
make yolo-rknn DATASET=/absolute/path/to/dataset.txt \
  RKNN_PYTHON=/path/to/rknn-toolkit/bin/python
```

Результаты сохраняются в `common/models/yolo11n.onnx`, `common/models/coco.names`
и `common/models/yolo11n.rknn`. Конвертер находится в
`common/config/nn/onnx2rknn_yolo.py` и должен запускаться на x86_64-хосте в
окружении RKNN Toolkit 1.6.1, а не на RV1126.
Проверка через x86-симулятор по умолчанию отключена, поскольку старые сборки
RKNN Toolkit могут завершаться с `Illegal instruction` на CPU без нужного набора
инструкций. Для явной проверки можно добавить конвертеру `--check-runtime`.

В `dataset.txt` должно быть 50–200 репрезентативных изображений с камеры.
После конвертации проверить напечатанную форму выхода. Для COCO и YOLO11
ожидается один выход на 84 атрибута; для YOLOv5 COCO — 85 атрибутов.

На устройство установить:

```sh
install -m 0755 mainCV /opt/seesharp/mainCV
install -m 0644 yolo11n.rknn /opt/seesharp/yolo11n.rknn
install -m 0644 coco.names /opt/seesharp/coco.names
install -m 0644 config.json /opt/seesharp/config.json
```

В конфиге MTV3 заданы `model_rknn`, `output_layout`, `output_attributes` и
`output_has_objectness`. Если Toolkit 1.6.1 не принимает конкретный экспорт
YOLO11, следует использовать совместимый одно-выходный ONNX либо YOLOv5 и
изменить три параметра выхода согласно форме модели.

## Порядок действий

### Сборка SDK в Docker

На Windows Docker Desktop всё равно использует WSL2/Hyper-V, но отдельную Linux VM
создавать и обслуживать не требуется. Для высокой скорости SDK и репозиторий лучше
хранить внутри файловой системы WSL (`~/proj`), а не на `/mnt/c`.

Из WSL собрать образ и открыть shell:

```sh
cd ~/proj/mtv3-vision
sh board/mtv3-rv1126/docker/build-image.sh
RV1126_SDK=~/proj/rv1126_sdk_ai \
  sh board/mtv3-rv1126/docker/run-sdk.sh
```

Внутри контейнера SDK доступен как `/proj/rv1126_sdk_ai`, а этот репозиторий —
как `/work/mtv3-vision`. Результаты сохраняются непосредственно в примонтированных
каталогах и не исчезают после удаления контейнера:

```sh
cd /proj/rv1126_sdk_ai
./build.sh rootfs

cd /work/mtv3-vision
cmake -S cpp/SeeSharp -B cpp/SeeSharp/build-rv1126 \
  -DCMAKE_TOOLCHAIN_FILE=/work/mtv3-vision/board/mtv3-rv1126/toolchain-rv1126.cmake \
  -DMTV3_BOARD=ON
cmake --build cpp/SeeSharp/build-rv1126 -j"$(nproc)"
```

Запуск одной команды без интерактивного shell:

```sh
RV1126_SDK=~/proj/rv1126_sdk_ai \
  sh board/mtv3-rv1126/docker/run-sdk.sh \
  bash -lc 'cd /proj/rv1126_sdk_ai && ./build.sh rootfs'
```

1. **Rootfs (defconfig, сборка вне conda).** Python-строки, если добавлялись,
   убрать (`BR2_PACKAGE_PYTHON3`, `BR2_PACKAGE_PYTHON_NUMPY`,
   `BR2_PACKAGE_OPENCV3_LIB_PYTHON`). Оставить/добавить:
   ```
   BR2_PACKAGE_OPENCV3=y
   BR2_PACKAGE_OPENCV3_LIB_CALIB3D=y
   BR2_PACKAGE_OPENCV3_LIB_IMGCODECS=y
   BR2_PACKAGE_OPENCV3_WITH_JPEG=y
   BR2_PACKAGE_OPENCV3_WITH_PNG=y
   BR2_PACKAGE_IPROUTE2=y
   BR2_PACKAGE_WPA_SUPPLICANT=y
   BR2_PACKAGE_WPA_SUPPLICANT_PASSPHRASE=y
   BR2_PACKAGE_HOSTAPD=y
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
   Для `MTV3_BOARD` начальный конфиг берётся из
   `common/config/config/config.json`. Другой профиль можно передать явно:
   ```
   cmake -DSEESHARP_CONFIG_FILE=/absolute/path/to/config.json ...
   ```

3. **Демон:** `make` в board/mtv3-rv1126 → `mtv3_cam_daemon`.

4. **Ручной прогон на борту:**
   ```
   ./init_mp.sh 2112 1568
   ./mtv3_cam_daemon --shm --ccm auto -s 108 --hz 50 -w 1056 -h 784 &
   cd /opt/seesharp && ./mainCV        # печатает fps каждые 100 кадров
   ```
   CV-разрешение задаёт `-w/-h` демона (скейлит resizer mainpath; сенсор
   остаётся в 2112x1568).

5. **Прошивка:** overlay rootfs:
   `/usr/bin/{mtv3_cam_daemon,init_mp.sh,mtv3_network.sh,mtv3_usb_gadget.sh}`,
   `/opt/seesharp/{mainCV,config.json}`, `/etc/init.d/S60mtv3` (chmod +x).
   Автостарты ispserver/aiserver/uvc_app убрать из defconfig (S60mtv3 их
   дополнительно убивает).

6. **Проверка:** полная перепрошивка → автостарт → `/tmp/mtv3.*.log`, fps из
   лога mainCV; отсюда финальное CV-разрешение.

## Передача метаданных

В профиле MTV3 оба транспорта изначально выключены. Это предотвращает захват
UART консоли и отправку UDP на неподтверждённый адрес при первом запуске.

UDP JSON включается в секции `transports.udp_metadata`:

```json
{
  "enabled": true,
  "format": "json",
  "host": "192.168.1.2",
  "port": 5000
}
```

Для UART сначала определить свободный порт на целевой прошивке:

```sh
cat /proc/cmdline
ls -l /dev/ttyS* /dev/ttyFIQ* 2>/dev/null
dmesg | grep -Ei 'tty|uart|serial'
```

Нельзя использовать устройство, указанное в `console=`. После проверки задать
реальный порт в `transports.uart_binary.device` и установить `enabled: true`.
Формат кадра описан в `board/raspberry-cm5/TRANSPORTS.md` в разделе
`UART lightweight binary protocol`; реализация одинакова для CM5 и RV1126.

Настройки транспортов читаются при запуске. После изменения через веб-интерфейс
конфигурацию нужно сохранить и перезапустить `mainCV` или `S60mtv3`.

## USB CDC ACM и RNDIS

`mtv3_usb_gadget.sh` создаёт составное USB-устройство: `/dev/ttyGS0` передаёт
метаданные и JPEG-кадры протокола `usb_stream`, а `usb0` предоставляет веб-интерфейс
по адресу `http://192.168.7.2:8081`. Профиль MTV3 включает USB-поток с ограничением
640 пикселей и 5 FPS, чтобы не перегружать CPU.

Для ядра требуются `CONFIG_USB_GADGET`, `CONFIG_USB_CONFIGFS`,
`CONFIG_USB_CONFIGFS_ACM`, `CONFIG_USB_CONFIGFS_RNDIS` и gadget-режим USB-контроллера.
В rootfs также нужны configfs, `libcomposite` и `ip` из iproute2. Скрипт отвязывает
штатный UVC gadget от UDC: одновременно использовать штатный UVC и этот составной
gadget на одном контроллере нельзя.

Проверка на модуле:

```sh
/usr/bin/mtv3_usb_gadget.sh status
ls -l /dev/ttyGS0
ip address show usb0
tail -f /tmp/mtv3.usb.log
```

Формат записей и платформонезависимый тестовый receiver описаны в
`board/raspberry-cm5/TRANSPORTS.md` и `board/raspberry-cm5/usb_stream_receiver.py`.

## Системное администрирование в Buildroot

HTTP API системного статуса, процессов, файлов и терминала доступен и на MTV3.
Статус интерфейсов читается через `getifaddrs`, а процессы — непосредственно из
`/proc`, поэтому GNU `ps`, `nmcli` и systemd не требуются.

Для доступа задать непустой `system_admin.token` в целевом `config.json`.
Терминал по умолчанию выключен и включается отдельным параметром
`terminal_enabled`. Файловый API ограничен каталогом `file_root`.

Изменение сети выполняет `/usr/bin/mtv3_network.sh`. Поддерживаются:

* `dhcp` — адрес через BusyBox `udhcpc`;
* `static` — статический адрес, шлюз и DNS;
* `client` — `wpa_supplicant` и DHCP;
* `ap` — `hostapd`, адрес `192.168.4.1/24` и `udhcpd`, если он установлен.

Скрипт должен принадлежать root и не быть доступен для записи пользователю
веб-приложения:

```sh
install -o root -g root -m 0755 mtv3_network.sh /usr/bin/mtv3_network.sh
```

## Веб-видеопоток

MJPEG-кодирование выполняется одним общим worker-потоком. Исходное и
аннотированное изображения кодируются один раз, после чего все подключённые
браузеры читают один JPEG-кэш. Медленный клиент не запускает дополнительное
кодирование и не блокирует pipeline обработки.

Профиль MTV3 использует безопасные для CPU значения:

```json
"web_preview": {
  "enabled": true,
  "jpeg_quality": 65,
  "max_fps": 8,
  "max_width": 640
}
```

`max_width` уменьшает изображение только для веб-просмотра и не влияет на
разрешение детектора или координаты метаданных. Изменения параметров применяются
без перезапуска. При высокой загрузке сначала уменьшать `max_fps` до 5, затем
`max_width` до 480 и `jpeg_quality` до 55.

## Протокол shm (v1)

`/dev/shm/mtv3cam`: заголовок 64 Б little-endian
`u32 magic("M3SH"=0x4D335348), version, w, h, stride, fmt(0=NV12), nslot(3),
slot_size, seq, ts_ms, reserved[6]`, далее слоты подряд.
`seq` — число завершённых кадров, последний кадр в слоте `(seq-1) % nslot`;
писатель инкрементирует seq после memcpy (с барьером), читатель перечитывает
seq после копии — расхождение ≥ nslot-1 означает порванный кадр (ретрай).
Реализации: демон (писатель), `SeeSharp .../pipeline/shm_source.{hpp,cpp}` (C++),
`SeeSharpPy .../pipeline/shm_source.py` (python, для отладки на хосте).
