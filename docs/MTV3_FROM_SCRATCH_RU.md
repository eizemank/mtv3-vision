# MTV3/RV1126: полный цикл от SDK до веб-интерфейса

Пошаговое руководство для разработчиков MTV3: получить Firefly SDK, наложить изменения
платы из `mtv3-bsp`, собрать U-Boot, ядро, rootfs и SeeSharp, собрать образы, прошить
модуль через `rkdeveloptool`, подключиться по ADB, запустить SeeSharp и открыть
веб-интерфейс. Все команды выполняются из корня репозитория `mtv3-vision`, сборка идёт в
Docker-контейнере `mtv3-rv1126-sdk` (Ubuntu 18.04), поэтому хост может быть любым: Linux,
Windows с WSL2, macOS. Справочная часть про SDK и устройство `mtv3-bsp` вынесена в
`docs/MTV3_SDK_BUILD_RU.md`, здесь только последовательность действий.

## 0. Что нужно

| | |
|---|---|
| Docker | Docker Engine или Docker Desktop, около 60 ГБ свободного места в docker data-root |
| Репозитории | `mtv3-vision` (этот), `mtv3-bsp` (изменения платы; стартовое содержимое — `mtv3-bsp-clean/`) |
| Сеть | доступ к `gitlab.com` (SDK, около 20 ГБ) и `github.com` (CMake, nlohmann_json, opencv_contrib) |
| Модуль | MTV3 с кабелем USB к разъёму OTG (прошивка, ADB, веб-интерфейс), опционально UART-консоль 1500000 8N1 |
| ПО на хосте | `adb` из Android Platform Tools, браузер |

Время: `repo sync` 30–60 минут, первая сборка rootfs 1–2 часа, ядро и U-Boot по
несколько минут.

## 1. Среда сборки

```sh
git clone <url>/mtv3-vision.git && cd mtv3-vision
sh board/mtv3-rv1126/docker/build-image.sh          # образ mtv3-rv1126-sdk:latest, ~900 МБ
```

Образ содержит тулчейн-зависимости SDK, `repo`, CMake 3.27, `rkdeveloptool` и инструмент
`mtv3-bsp`. Сам SDK в образ не входит и живёт в docker volume `mtv3-rv1126-sdk`.

## 2. Клонирование SDK Firefly

```sh
sh board/mtv3-rv1126/docker/init-sdk.sh
```

Внутри контейнера выполняется `repo init` манифеста
`rv1126_rv1109_linux_ai_camera_release.xml` из `https://gitlab.com/firefly-linux/manifests`
(тег `rv1126_rv1109_ai/linux_release_v2.3.3b`: ядро 4.19.111, buildroot 2018.02, U-Boot
2017.09) и `repo sync` всех проектов, включая `external/*`, `app/*` и `buildroot/dl`.
Повторное обновление: `init-sdk.sh --sync`. Воспроизводимая ревизия:
`init-sdk.sh --pinned mtv3-bsp/manifest/pinned.xml`.

Проверка:

```sh
sh board/mtv3-rv1126/docker/run-sdk.sh bash -lc 'cd $RV1126_SDK && ls && ./build.sh check'
```

## 3. Изменения платы: mtv3-bsp

Клонировать `mtv3-bsp` рядом с `mtv3-vision`. Если репозиторий ещё не наполнен, его
содержимым становится каталог `mtv3-bsp-clean/` (overlay, patches, review, README):

```sh
export MTV3_BSP=$HOME/Work/Zarnitza/sw/mtv3-bsp
sh board/mtv3-rv1126/docker/run-sdk.sh              # интерактивный shell, mtv3-bsp смонтирован в /work/mtv3-bsp
```

Дальше все команды выполняются внутри контейнера (приглашение `mtv3-sdk:`):

```sh
mtv3-bsp apply                                      # overlay + patches -> ветка mtv3 в kernel, u-boot, buildroot, device/rockchip, rkaiq
mtv3-bsp status                                     # по репозиториям: ветка, коммиты над базой, грязные файлы
```

Что накладывается: `device/rockchip/rv1126_rv1109/mtv3-rv1126.mk` (BoardConfig),
`parameter-mtv3.txt`, `oem/oem_mtv3/`, dts и defconfig ядра `rv1126-mtv3` /
`rv1126_mtv3_defconfig`, U-Boot `rv1126_mtv3_defconfig` и `rv1126-mtv3.dts`, buildroot
`rockchip_rv1126_rv1109_mtv3_defconfig`, IQ-файл OV13850, патчи ядра (ov13850, rkcif,
rkispp, CMA), U-Boot (`rv1126-u-boot.dtsi`, eMMC tuning), buildroot (opencv3 + aruco) и
post-build-скрипт, который кладёт mtv3-vision в rootfs. Каталог `review/` не применяется.

Патч opencv3/aruco ждёт архив contrib в `buildroot/dl`:

```sh
wget -O $RV1126_SDK/buildroot/dl/opencv_contrib-3.3.0.tar.gz \
     https://github.com/opencv/opencv_contrib/archive/3.3.0.tar.gz
```

## 4. Выбор платы

```sh
sdk mtv3-rv1126.mk                                  # = cd $RV1126_SDK && ./build.sh mtv3-rv1126.mk
realpath $RV1126_SDK/device/rockchip/.BoardConfig.mk
```

Ссылка `.BoardConfig.mk` должна указывать на `mtv3-rv1126.mk`. Он задаёт
`RK_UBOOT_DEFCONFIG=rv1126_mtv3`, `RK_KERNEL_DEFCONFIG=rv1126_mtv3_defconfig`,
`RK_KERNEL_DTS=rv1126-mtv3`, `RK_CFG_BUILDROOT=rockchip_rv1126_rv1109_mtv3`,
`RK_PARAMETER=parameter-mtv3.txt`, `RK_OEM_DIR=oem_mtv3`, `RK_LOADER_UPDATE_SPL=true`.

## 5. U-Boot

```sh
sdk uboot
ls $RV1126_SDK/u-boot/uboot.img $RV1126_SDK/u-boot/rv1126_loader_v*.bin
```

Результат: `uboot.img` (FIT: U-Boot + OP-TEE) и загрузчик `rv1126_loader_v*.bin`, который
`mkfirmware.sh` позже положит в `rockdev/MiniLoaderAll.bin`. SPL берётся из этой же сборки
U-Boot (`RK_LOADER_UPDATE_SPL=true`), блобы из `rkbin` не трогаются.

## 6. Ядро

```sh
sdk kernel
ls $RV1126_SDK/kernel/zboot.img
```

`zboot.img` — FIT с zImage и `rv1126-mtv3.dtb`. Меню конфигурации при необходимости:
`make -C $RV1126_SDK/kernel ARCH=arm menuconfig`, затем
`make ARCH=arm savedefconfig` и копирование `defconfig` в
`arch/arm/configs/rv1126_mtv3_defconfig` (изменения потом выгружаются `mtv3-bsp export`).

## 7. Rootfs, первый проход

```sh
sdk rootfs                                          # лог в $RV1126_SDK/br.log
```

Первый проход собирает тулчейн, OpenCV 3.3 с aruco, rkmedia, rknpu, rkaiq и весь UVC-стек
Firefly. Post-build-скрипт из `mtv3-bsp` на этом проходе предупредит, что `mainCV` и
`mtv3_cam_daemon` ещё не собраны: они и не могут быть, им нужен staging из этой сборки.
После завершения появятся:

```
$RV1126_SDK/buildroot/output/rockchip_rv1126_rv1109_mtv3/host/bin/arm-linux-gnueabihf-gcc
$RV1126_SDK/buildroot/output/rockchip_rv1126_rv1109_mtv3/staging/usr/share/OpenCV/OpenCVConfig.cmake
$RV1126_SDK/buildroot/output/rockchip_rv1126_rv1109_mtv3/staging/usr/lib/librknn_api.so
```

## 8. SeeSharp и демон камеры

```sh
mkdir -p /work/mtv3-vision/cpp/SeeSharp/third_party/nlohmann
wget -O /work/mtv3-vision/cpp/SeeSharp/third_party/nlohmann/json.hpp \
     https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp   # один раз

mtv3-cmake                                          # cpp/SeeSharp/build-rv1126/mainCV + config.json
mtv3-daemon                                         # board/mtv3-rv1126/mtv3_cam_daemon
```

`mtv3-cmake` вызывает CMake с `toolchain-rv1126.cmake` и `-DMTV3_BOARD=ON`; тулчейн-файл сам
находит `librknn_api` в staging. Конфигурация берётся из
`common/config/config/config.json` (профиль MTV3: режим `aruco_detection`, веб-превью
640 px и 8 fps, USB-поток включён, UDP и UART выключены). Другой профиль:
`mtv3-cmake -DSEESHARP_CONFIG_FILE=/абсолютный/путь/config.json`.

Модели нейросетей не обязательны для режимов aruco, blob, line и circle. Для
`object_detection` и `classification` положите `*.rknn` и `coco.names` в
`common/models/`: post-build-скрипт скопирует их в `/opt/seesharp`. Конвертация ONNX в
RKNN выполняется в образе со стадией `--rknn` (см. `board/mtv3-rv1126/README.md`,
раздел «YOLO на RKNN»).

## 9. Rootfs, второй проход, и образы

```sh
sdk rootfs                                          # пакеты уже собраны, повторяется только post-build и упаковка
ls $RV1126_SDK/buildroot/output/rockchip_rv1126_rv1109_mtv3/target/opt/seesharp/
grep -E "/oem|/userdata" $RV1126_SDK/buildroot/output/rockchip_rv1126_rv1109_mtv3/target/etc/fstab

sdk firmware                                        # собрать rockdev/
sdk updateimg                                       # rockdev/update.img
ls -l $RV1126_SDK/rockdev/
```

В `rockdev/` должны быть `MiniLoaderAll.bin`, `parameter.txt`, `uboot.img`, `boot.img`,
`rootfs.img`, `oem.img`, `userdata.img`, `misc.img` и `update.img`. `trust.img` для
FIT-формата U-Boot не создаётся, это нормально. Разделы по `parameter-mtv3.txt`: `uboot`,
`misc`, `boot`, `recovery`, `backup`, `rootfs` (p6), `oem` (p7), `userdata` (p8).

Один прогон без интерактивного shell:

```sh
MTV3_BSP=$HOME/Work/Zarnitza/sw/mtv3-bsp sh board/mtv3-rv1126/docker/run-sdk.sh bash -lc \
  'mtv3-bsp apply && sdk mtv3-rv1126.mk && sdk uboot && sdk kernel && sdk rootfs && \
   mtv3-cmake && mtv3-daemon && sdk rootfs && sdk firmware && sdk updateimg'
```

## 10. Прошивка через rkdeveloptool

Перевести модуль в MaskROM или Loader: удерживать кнопку RECOVERY при подаче питания,
либо из уже работающей системы `reboot loader` (консоль или `adb shell reboot loader`).
Подключить USB OTG к компьютеру. На Linux-хосте контейнер получает устройство флагом
`MTV3_USB=1`; в WSL2 устройство сначала пробрасывается `usbipd attach`.

```sh
MTV3_USB=1 sh board/mtv3-rv1126/docker/run-sdk.sh
rkdeveloptool ld                                    # DevNo=1 Vid=0x2207,Pid=0x110c,LocationID=... Maskrom|Loader
cd $RV1126_SDK/rockdev
rkdeveloptool db MiniLoaderAll.bin                  # только из MaskROM: загрузить loader в ОЗУ
rkdeveloptool ul MiniLoaderAll.bin                  # записать loader во flash
rkdeveloptool gpt parameter.txt                     # таблица разделов
rkdeveloptool wlx uboot    uboot.img
rkdeveloptool wlx misc     misc.img
rkdeveloptool wlx boot     boot.img
rkdeveloptool wlx rootfs   rootfs.img
rkdeveloptool wlx oem      oem.img
rkdeveloptool wlx userdata userdata.img
rkdeveloptool rd                                    # перезагрузка
```

Для обновления одного компонента достаточно его `wlx` и `rd`; `db` нужен только в
MaskROM. Альтернативы: `cd $RV1126_SDK && ./rkflash.sh` (все разделы через `upgrade_tool`
из SDK), `./rkflash.sh boot` для одного раздела, либо `update.img` целиком:
`tools/linux/Linux_Upgrade_Tool/Linux_Upgrade_Tool/upgrade_tool uf rockdev/update.img`.
На Windows без проброса USB используется `RKDevTool` из `tools/windows` с `update.img`.

Если `rkdeveloptool ld` ничего не показывает: проверьте режим модуля, кабель на разъёме
OTG и что `lsusb` на хосте видит `2207:110c`; в Docker Desktop без `usbipd` USB
недоступен.

## 11. Первая загрузка

На UART-консоли (1500000 8N1) должна появиться загрузка U-Boot, ядра и приглашение
Buildroot. `S60mtv3` стартует автоматически: останавливает штатные `ispserver`,
`aiserver`, `uvc_app`, поднимает USB gadget (CDC ACM `/dev/ttyGS0` плюс RNDIS `usb0` с
адресом `192.168.7.2/24`), фиксирует частоту NPU, инициализирует медиа-граф
`init_mp.sh 2112 1568`, запускает `mtv3_cam_daemon` и `mainCV` с перезапуском при падении.
Журналы: `/tmp/mtv3.usb.log`, `/tmp/mtv3.init.log`, `/tmp/mtv3.cam.log`,
`/tmp/mtv3.app.log`.

На компьютере после подключения появляется USB-сетевой адаптер. Назначьте ему адрес:

```sh
ip link                                             # найти интерфейс: usb0 или enx020000000701 (MAC из mtv3_usb_gadget.sh)
sudo ip link set <usb-интерфейс> up
sudo ip address replace 192.168.7.1/24 dev <usb-интерфейс>
ping 192.168.7.2
```

Windows: `New-NetIPAddress -InterfaceAlias "USB Ethernet" -IPAddress 192.168.7.1
-PrefixLength 24` из PowerShell с правами администратора. Подробности в
`docs/MTV3_USB_GADGET_RU.md`.

## 12. ADB

`adbd` входит в rootfs (`BR2_PACKAGE_ANDROID_TOOLS` из `base_extra.config`), запускается
`S50usbdevice` ещё до `S60mtv3` и благодаря патчу Firefly слушает TCP-порт 5555 в
дополнение к USB. Нативная функция `ffs.adb` после переключения на наш gadget недоступна,
поэтому ADB работает по TCP через RNDIS:

```sh
adb kill-server
adb connect 192.168.7.2:5555
adb devices -l
adb shell
```

Если `ping` проходит, а `adb connect` получает `Connection refused`, на модуле проверить:

```sh
ps | grep '[a]dbd'
netstat -lnt | grep 5555
/etc/init.d/S50usbdevice restart
```

Быстрый цикл разработки без перепрошивки, из контейнера после `mtv3-cmake`:

```sh
adb push cpp/SeeSharp/build-rv1126/mainCV /opt/seesharp/mainCV
adb push board/mtv3-rv1126/mtv3_cam_daemon /usr/bin/mtv3_cam_daemon
adb shell /etc/init.d/S60mtv3 restart
adb shell tail -f /tmp/mtv3.app.log
```

Для `adb` внутри контейнера нужен тот же проброс USB-сети, что и на хосте; проще
выполнять `adb` на хосте из каталога `mtv3-vision`.

## 13. Запуск SeeSharp вручную

Для отладки остановите автостарт и запустите тракт руками:

```sh
adb shell
/etc/init.d/S60mtv3 stop
init_mp.sh 2112 1568
mtv3_cam_daemon --shm --ccm auto -s 108 --hz 50 -w 1056 -h 784 &
cd /opt/seesharp && ./mainCV --config /opt/seesharp/config.json
```

`mainCV` печатает fps каждые 100 кадров. Демон публикует NV12-кадры в `/dev/shm/mtv3cam`,
`-w/-h` задают разрешение для CV (сенсор остаётся в 2112x1568). Вернуть автостарт:
`/etc/init.d/S60mtv3 start`.

## 14. Веб-интерфейс

В браузере на компьютере:

```
http://192.168.7.2:8081/
```

Страница управления: превью, выбор анализатора, редактор конфигурации, вкладка
«Обучение». Прямые адреса: `/source.mjpg` и `/preview.mjpg` (видео), `/source.jpg` и
`/preview.jpg` (кадры), `/config` (JSON). Описание вкладок — `docs/USER_GUIDE_RU.md`,
`docs/DETECTOR_UI_GUIDE_RU.md`, `docs/TRAINING_UI_GUIDE_RU.md`.

Системное администрирование (статус, процессы, файлы, сеть, терминал) требует непустого
`system_admin.token` в `/opt/seesharp/config.json`; терминал включается отдельно
`terminal_enabled`. Сеть переключается через `mtv3_network.sh` из того же интерфейса:
Ethernet, Wi-Fi клиент или точка доступа `192.168.4.1`, после чего веб-интерфейс доступен
и без USB по адресу модуля на порту 8081. Изменения конфигурации сохраняются в
`/opt/seesharp/config.json`; транспорты (UDP, UART, USB) применяются после перезапуска
`mainCV` или `S60mtv3`.

## 15. Сохранить изменения

Правки в дереве SDK делаются коммитами в ветке `mtv3` соответствующего репозитория и
выгружаются в `mtv3-bsp`:

```sh
cd $RV1126_SDK/kernel && git commit -am "..."
mtv3-bsp export                                     # patches/, overlay/, manifest/pinned.xml
cd /work/mtv3-bsp && git add -A && git commit -m "..."
```

Изменения SeeSharp и скриптов платы коммитятся в `mtv3-vision`; в rootfs они попадают
через post-build-скрипт при следующем `sdk rootfs`.

## 16. Типовые проблемы

| Симптом | Что делать |
|---|---|
| `mtv3-bsp apply`: `branch mtv3 already has commits` | `mtv3-bsp export`, затем `mtv3-bsp apply --force` |
| `git am` конфликт | исправить, `git am --continue`, `mtv3-bsp export`, `mtv3-bsp apply --force` |
| `opencv3`: нет `opencv_contrib-3.3.0.tar.gz` | скачать в `buildroot/dl` (шаг 3) |
| CMake: `nlohmann_json not found` | положить `json.hpp` в `cpp/SeeSharp/third_party/nlohmann/` (шаг 8) |
| CMake: `OpenCVConfig.cmake not found` | rootfs ещё не собран или собран под другой `RK_CFG_BUILDROOT` |
| `/opt/seesharp` пуст после прошивки | второй проход `sdk rootfs` не выполнялся после `mtv3-cmake` |
| `mtv3.usb.log`: `ttyGS0 was not created` | в ядре нет `CONFIG_USB_CONFIGFS_ACM`; в `rv1126_mtv3_defconfig` из `mtv3-bsp` он включён |
| `/oem` пуст | fstab без `/dev/mmcblk0p7`: rootfs собран не через `build.sh` (переменные `RK_OEM_NODE` читает rkscript из окружения) |
| `adb connect` отказ | `adbd` не запущен, `S50usbdevice restart` на модуле |
| `rkdeveloptool ld` пусто | модуль не в MaskROM/Loader, кабель не в OTG, USB не проброшен в контейнер |
| Веб-интерфейс не открывается, `ping` есть | `mainCV` не запущен: `tail /tmp/mtv3.app.log`, нет `config.json` или модели |
