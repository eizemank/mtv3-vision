# robo-techvision — sw

Две ветки движка (общая архитектура: pipeline → factory → processors,
единая схема конфига) + общие ресурсы.

```
sw/
├── cpp/SeeSharp/          # C++ движок: десктоп и модуль MTV3 (RV1126, -DMTV3_BOARD=ON)
├── python/SeeSharpPy/     # python-движок: разработка/прототипирование алгоритмов на хосте
├── common/
│   ├── config/config.json # единый конфиг обоих движков (схема одинаковая)
│   ├── nn/                # simple_classifier.py (обучение/ONNX), onnx2rknn.py (NPU)
│   └── models/            # артефакты: .pt / .onnx / .rknn / классы
├── board/mtv3-rv1126/     # модуль RV1126: mtv3_cam_daemon, init_mp.sh, S60mtv3,
├── board/raspberry-cm5/   # образы/утилиты CM5
│                          #   toolchain-rv1126.cmake, opencv3-aruco.mk.add
└── mock/                  # фейковый видеосервер для отладки без железа
```

Правила:

* Конфиг правится только в `common/config/config.json`. C++ CMake копирует его
  в build-папку; python main.py читает его напрямую (оба с fallback на
  локальный `config/`, если общего нет).
* Контракт NN-препроцессинга (RGB, 64x64, x/255) зашит в трёх местах:
  `common/nn/simple_classifier.py`, `common/nn/onnx2rknn.py`, классификаторы
  движков. Менять — только синхронно.
* Прод-цель — C++ на модуле (см. `board/mtv3-rv1126/README.md`);
  python-ветка на модуль не ставится (python в buildroot не собирается).
# Host UI test

The CM5 web interface and detector configuration can be tested without target
hardware using the synthetic host build. See `board/host/README.md` or run:

```bash
sh board/host/run_web_ui.sh
```
