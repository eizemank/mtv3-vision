# Запуск SeeSharp с системной камерой на чистой машине

Руководство описывает запуск отладочной host-версии SeeSharp с реальной USB или
встроенной камерой. Поддерживаются Ubuntu/Debian и Windows 10/11 с WSL2.

Host-версия предоставляет:

- видеопоток и настройку детекторов через веб-интерфейс;
- передачу полных метаданных через WebSocket;
- передачу бинарных метаданных через USB–UART;
- ArUco, blob, line, circle и YOLO-детекцию.

## 1. Требования

- компьютер `x86_64` с Ubuntu/Debian либо Windows с WSL2;
- UVC-совместимая USB-камера или встроенная камера;
- доступ в интернет для установки пакетов и загрузки YOLO;
- репозиторий `mtv3-vision`;
- USB–UART адаптер — только если требуется проверка UART.

## 2. Установка на Ubuntu/Debian

Установите компилятор, CMake, OpenCV и средства диагностики камеры:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev \
  nlohmann-json3-dev python3 python3-pip curl v4l-utils
```

Добавьте пользователя в группы доступа к видео и последовательным портам:

```bash
sudo usermod -aG video,dialout "$USER"
```

Выйдите из пользовательского сеанса и войдите снова. Проверьте группы:

```bash
groups
```

В списке должны присутствовать `video` и `dialout`.

## 3. Подготовка Windows и WSL2

### 3.1. Установка WSL2

Откройте PowerShell от имени администратора:

```powershell
wsl --install -d Ubuntu
wsl --update
```

Перезагрузите Windows, запустите Ubuntu и создайте Linux-пользователя. Затем
установите пакеты из раздела 2 внутри Ubuntu.

Проверьте версию WSL:

```powershell
wsl --list --verbose
```

Для Ubuntu должна быть указана версия `2`.

### 3.2. Установка usbipd-win

В PowerShell:

```powershell
winget install --exact dorssel.usbipd-win --source winget
```

Если `winget` недоступен, установите `usbipd-win` из раздела Releases проекта
`dorssel/usbipd-win` на GitHub.

### 3.3. Проброс камеры в WSL2

Оставьте терминал WSL открытым. В PowerShell найдите камеру:

```powershell
usbipd list
```

Запомните её `BUSID`, например `1-4`. Первичное разрешение выполняется из
PowerShell от имени администратора:

```powershell
usbipd bind --busid 1-4
```

Подключение выполняется из обычного PowerShell:

```powershell
usbipd attach --wsl --busid 1-4
```

В Ubuntu загрузите UVC-драйвер и проверьте устройство:

```bash
sudo modprobe uvcvideo
lsusb
ls -l /dev/video*
v4l2-ctl --list-devices
```

Если `/dev/video0` не появился, выполните `wsl --update`, `wsl --shutdown` и
повторите подключение. Некоторые встроенные камеры не работают через USB/IP;
в этом случае используйте внешнюю UVC-камеру или обычный Linux.

## 4. Проверка камеры

Покажите поддерживаемые режимы камеры:

```bash
v4l2-ctl --device /dev/video0 --list-formats-ext
```

Проверьте получение одного кадра в MJPEG:

```bash
v4l2-ctl --device /dev/video0 \
  --set-fmt-video=width=640,height=480,pixelformat=MJPG \
  --stream-mmap=3 --stream-count=1 --stream-to=/tmp/camera.mjpg
ls -lh /tmp/camera.mjpg
```

Если камера имеет другой номер, далее замените `0` на номер из `/dev/videoN`.

## 5. Получение репозитория

На Linux рекомендуется хранить проект в домашней файловой системе. В WSL
допустим путь `/mnt/c/...`, поскольку launcher размещает сборку в `~/.cache`.

```bash
git clone <URL-репозитория> mtv3-vision
cd mtv3-vision
```

Если репозиторий уже открыт из Windows, перейдите к нему из WSL:

```bash
cd /mnt/c/<путь>/mtv3-vision
```

## 6. Первый запуск

Запускайте скрипт обычным пользователем, без `sudo`:

```bash
sh board/host/run_camera_debug.sh \
  --camera 0 \
  --uart /dev/ttyUSB0 \
  --ws-port 5002 \
  --download-yolo
```

При отсутствии USB–UART будет выведено предупреждение, но камера,
веб-интерфейс и WebSocket продолжат работать. Для запуска без реального UART
можно оставить `/dev/ttyUSB0`: транспорт автоматически не откроется.

Первый запуск:

1. конфигурирует host-сборку через CMake;
2. собирает `mainCV` в `~/.cache/seesharp/build-host-camera`;
3. загружает `yolo11n.onnx` и `coco.names`;
4. открывает камеру в MJPEG `640×480`, 15 FPS;
5. запускает веб-сервер на порту `8081` и WebSocket на порту `5002`.

Последующие запуски не требуют `--download-yolo`:

```bash
sh board/host/run_camera_debug.sh \
  --camera 0 --uart /dev/ttyUSB0 --ws-port 5002
```

## 7. Открытие интерфейса

Launcher выводит адрес, например:

```text
Web UI: http://192.168.1.20:8081/
Metadata: ws://192.168.1.20:5002/metadata
```

На этой же машине откройте:

```text
http://127.0.0.1:8081/
```

С другого компьютера используйте выведенный IP. В Windows также обычно
доступен `http://127.0.0.1:8081/` благодаря перенаправлению портов WSL.

В интерфейсе выберите детектор, измените параметры и нажмите **Применить**.
Кнопка **Сохранить** записывает конфигурацию в build-каталог.

## 8. Запуск YOLO

После загрузки ресурсов выберите **Объекты YOLO**. Файлы должны существовать:

```bash
ls -lh ~/.cache/seesharp/build-host-camera/yolo11n.onnx \
       ~/.cache/seesharp/build-host-camera/coco.names
```

YOLO на CPU обрабатывает кадры медленнее простых детекторов. Для повышения
частоты уменьшите размер входа в параметрах `object_detection` либо задайте
меньший режим камеры:

```bash
sh board/host/run_camera_debug.sh \
  --camera 0 --uart /dev/ttyUSB0 --ws-port 5002 \
  --width 320 --height 240 --fps 10
```

При переключении старый детектор продолжает работать, пока загружается модель.

## 9. Приём WebSocket-метаданных

Установите клиент:

```bash
python3 -m pip install --user websocket-client
```

Запустите приёмник, подставив IP из сообщения launcher:

```bash
python3 board/host/websocket_metadata_receiver.py \
  ws://127.0.0.1:5002/metadata
```

## 10. Подключение USB–UART

На Linux адаптер обычно появляется как `/dev/ttyUSB0` или `/dev/ttyACM0`.
Проверьте:

```bash
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

В WSL пробросьте адаптер через `usbipd` так же, как камеру, но с его `BUSID`.

Подключение передатчика:

- TX USB–UART host-машины → RX принимающего устройства;
- GND → GND;
- уровни UART должны быть совместимы, обычно 3,3 В;
- RX передающего адаптера не требуется для односторонней передачи метаданных.

Для проверки вторым адаптером:

```bash
python3 -m pip install --user pyserial
python3 board/host/uart_metadata_receiver.py /dev/ttyUSB1
```

Формат пакетов описан в `board/raspberry-cm5/TRANSPORTS.md`.

## 11. Параметры launcher

| Параметр | Назначение | По умолчанию |
|---|---|---|
| `--camera N` | Номер `/dev/videoN` | `0` |
| `--uart PATH` | USB–UART устройство | `/dev/ttyUSB0` |
| `--ws-port PORT` | Порт WebSocket | `5002` |
| `--width PX` | Ширина захвата | `640` |
| `--height PX` | Высота захвата | `480` |
| `--fps N` | Частота камеры | `15` |
| `--no-mjpeg` | Не запрашивать MJPEG у камеры | выключено |
| `--download-yolo` | Загрузить модель и COCO labels | выключено |

## 12. Диагностика

### Камера не найдена

```bash
lsusb
ls -l /dev/video*
v4l2-ctl --list-devices
dmesg | tail -n 50
```

В WSL убедитесь, что `usbipd list` показывает `Attached`, а не только `Shared`.

### Нижняя часть кадра зелёная или кадр повреждён

Используйте MJPEG и уменьшите нагрузку USB:

```bash
sh board/host/run_camera_debug.sh --camera 0 --uart /dev/ttyUSB0 \
  --width 320 --height 240 --fps 10
```

Не используйте `--no-mjpeg`, пока не убедитесь, что камера не поддерживает
MJPEG. Не подключайте камеру через перегруженный USB-хаб.

### Нет доступа к камере или UART

Проверьте группы и владельца устройства:

```bash
groups
ls -l /dev/video0 /dev/ttyUSB0
```

После `usermod` требуется новый пользовательский сеанс. Не запускайте SeeSharp
через `sudo`, иначе build-каталог станет принадлежать root.

### Build-каталог принадлежит root

```bash
sudo chown -R "$USER:$USER" "$HOME/.cache/seesharp"
```

### YOLO не загружает `coco.names` или модель

```bash
rm -f ~/.cache/seesharp/build-host-camera/coco.names
sh board/host/run_camera_debug.sh --camera 0 --uart /dev/ttyUSB0 \
  --ws-port 5002 --download-yolo
```

### Порт уже занят

```bash
ss -ltnp | grep -E ':8081|:5002'
```

Остановите старый `mainCV` через `Ctrl+C` или выберите другой WebSocket-порт.
Порт веб-интерфейса в текущей host-сборке фиксирован: `8081`.

## 13. Завершение работы

Остановите SeeSharp сочетанием `Ctrl+C`. В WSL отключите устройства из
PowerShell:

```powershell
usbipd detach --busid <BUSID-КАМЕРЫ>
usbipd detach --busid <BUSID-UART>
```

