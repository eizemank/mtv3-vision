# MTV3/RV1126: работа через USB gadget

## 1. Состав USB-устройства

После запуска `mtv3_usb_gadget.sh` модуль определяется как составное USB-устройство:

| Функция | На MTV3 | На компьютере | Назначение |
|---|---|---|---|
| CDC ACM | `/dev/ttyGS0` | Linux: `/dev/ttyACM0`; Windows: `COMx` | Поток JSON-метаданных и JPEG-кадров |
| RNDIS | `usb0`, `192.168.7.2/24` | USB Ethernet | Веб-интерфейс, HTTP API и ADB по TCP |

CDC ACM и RNDIS используют один USB-кабель. Скорость COM-порта, выбранная на
компьютере, для USB CDC не задаёт физическую скорость передачи.

Текущая конфигурация не содержит нативную USB-функцию `ffs.adb`. Поэтому ADB
подключается по TCP через USB-интерфейс RNDIS. Для этого целевая прошивка должна
содержать `adbd`, настроенный на TCP-порт 5555.

## 2. Требования к прошивке

В ядре должны быть включены:

```text
CONFIG_USB_GADGET=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_CONFIGFS_ACM=y
CONFIG_USB_CONFIGFS_RNDIS=y
```

USB-контроллер платы должен работать в device/peripheral mode. В rootfs нужны
`configfs`, `libcomposite`, `ip` и скрипт:

```sh
install -o root -g root -m 0755 \
  board/mtv3-rv1126/mtv3_usb_gadget.sh \
  /usr/bin/mtv3_usb_gadget.sh
```

`S60mtv3` запускает gadget автоматически. Для ручной проверки:

```sh
/usr/bin/mtv3_usb_gadget.sh restart
/usr/bin/mtv3_usb_gadget.sh status
ls -l /dev/ttyGS0
ip address show usb0
tail -f /tmp/mtv3.usb.log
```

Скрипт отвязывает штатный UVC gadget от USB Device Controller. Штатный UVC и
gadget SeeSharp нельзя одновременно использовать на одном UDC.

## 3. Подключение компьютера

Подключите USB device/OTG-разъём MTV3 к компьютеру кабелем с линиями данных.
Кабель только для зарядки не подходит.

### 3.1. Linux

Проверьте появившиеся устройства:

```sh
dmesg --follow
ls -l /dev/ttyACM*
ip link
```

Если адрес USB Ethernet не назначен автоматически:

```sh
sudo ip link set <usb-интерфейс> up
sudo ip address replace 192.168.7.1/24 dev <usb-интерфейс>
ping 192.168.7.2
```

Для доступа к CDC ACM добавьте пользователя в группу `dialout`, затем войдите
в систему повторно:

```sh
sudo usermod -aG dialout "$USER"
```

### 3.2. Windows

В диспетчере устройств должны появиться COM-порт и сетевой адаптер RNDIS.
Узнать COM-порт можно командой:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name
```

Если Windows не назначила USB-сети адрес `192.168.7.1`, найдите имя адаптера и
задайте адрес из PowerShell с правами администратора:

```powershell
Get-NetAdapter
New-NetIPAddress -InterfaceAlias "USB Ethernet" `
  -IPAddress 192.168.7.1 -PrefixLength 24
Test-Connection 192.168.7.2
```

## 4. Веб-интерфейс и поток детекции

Откройте:

```text
http://192.168.7.2:8081/
```

Прямые адреса потоков:

| URL | Содержимое |
|---|---|
| `http://192.168.7.2:8081/source.mjpg` | Исходное видео |
| `http://192.168.7.2:8081/preview.mjpg` | Видео с результатами детекции |
| `http://192.168.7.2:8081/source.jpg` | Последний исходный кадр |
| `http://192.168.7.2:8081/preview.jpg` | Последний размеченный кадр |
| `http://192.168.7.2:8081/config` | Текущая конфигурация JSON |

В конфигурации MTV3 веб-предпросмотр ограничен по FPS и ширине для снижения
нагрузки CPU. Эти ограничения не меняют разрешение, используемое детектором.

## 5. Метаданные и видео через CDC ACM

В `config.json` должен быть включён транспорт:

```json
"usb_stream": {
  "enabled": true,
  "device": "/dev/ttyGS0",
  "metadata": true,
  "video": true,
  "jpeg_quality": 60,
  "max_fps": 5,
  "max_width": 640
}
```

После изменения секции транспорта перезапустите SeeSharp:

```sh
/etc/init.d/S60mtv3 restart
```

Установите зависимости receiver на компьютере:

```sh
python3 -m pip install pyserial numpy opencv-python
```

Linux:

```sh
python3 board/raspberry-cm5/usb_stream_receiver.py /dev/ttyACM0
```

Windows:

```powershell
py board/raspberry-cm5/usb_stream_receiver.py COM7
```

Receiver печатает `frame_id`, тип детектора и количество объектов, а JPEG-кадры
показывает в окне `SeeSharp USB stream`. Для выхода нажмите `Esc`.

Поток состоит из записей с 16-байтовым заголовком big-endian:

| Смещение | Размер | Поле | Значение |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `MTVU` |
| 4 | 1 | version | `1` |
| 5 | 1 | message type | `1` — JSON, `2` — JPEG |
| 6 | 2 | flags | `0` |
| 8 | 4 | frame ID | Связывает JSON и JPEG |
| 12 | 4 | payload size | Размер следующих данных |

Один JPEG передаётся одной записью без UDP-фрагментации. Формат JSON совпадает
с полным форматом метаданных UDP/Ethernet.

## 6. ADB через USB Ethernet

### 6.1. Подготовка прошивки

Добавьте `adbd` в Buildroot и настройте его запуск в TCP-режиме на адресе
`0.0.0.0:5555` или `192.168.7.2:5555`. Конкретная команда запуска зависит от
варианта `adbd` в используемом Rockchip SDK. После запуска проверьте на MTV3:

```sh
command -v adbd
ps | grep '[a]dbd'
ss -lntp | grep ':5555'
```

Если в rootfs нет `ss`, используйте:

```sh
netstat -lntp | grep ':5555'
```

Не публикуйте порт 5555 в обычную LAN/Wi-Fi сеть: старые Buildroot-версии `adbd`
могут не требовать подтверждения RSA-ключа. Ограничьте его интерфейсом `usb0` или
правилами firewall.

### 6.2. Подключение отладчика

Установите Android Platform Tools на компьютере и выполните:

```sh
adb kill-server
adb connect 192.168.7.2:5555
adb devices -l
adb shell
```

Передача файлов и просмотр журнала:

```sh
adb push cpp/SeeSharp/build-rv1126/mainCV /opt/seesharp/mainCV
adb pull /tmp/mtv3.app.log .
adb shell tail -f /tmp/mtv3.app.log
```

Отключение:

```sh
adb disconnect 192.168.7.2:5555
```

Если `ping 192.168.7.2` работает, но `adb connect` получает `Connection refused`,
USB gadget исправен, однако `adbd` не запущен или не слушает TCP-порт 5555.

## 7. Диагностика

| Симптом | Проверка |
|---|---|
| Нет COM-порта и RNDIS | OTG-разъём, data-кабель, `/sys/class/udc`, лог gadget |
| Есть COM, но receiver молчит | `usb_stream.enabled`, `/dev/ttyGS0`, перезапуск `S60mtv3` |
| Не открывается веб-интерфейс | адрес хоста `192.168.7.1/24`, `ping`, процесс `mainCV` |
| Видео прерывается | уменьшить `max_fps`, `max_width`, `jpeg_quality` |
| ADB `Connection refused` | наличие `adbd` и listener на `:5555` |
| ADB timeout | адреса RNDIS, firewall компьютера и модуля |

Основные журналы:

```sh
tail -f /tmp/mtv3.usb.log
tail -f /tmp/mtv3.app.log
dmesg | tail -n 100
```
