# MTV3/RV1126: сборка SDK «с нуля» и изменения платы (mtv3-bsp)

Руководство для разработчиков MTV3. Описывает, как получить Firefly/Rockchip
SDK для RV1126 из GitLab, собрать загрузчик, ядро, rootfs и прошивку в
Docker-среде проекта, а затем накладывать и сопровождать изменения нашей платы
из отдельного репозитория `mtv3-bsp` в виде overlay и patches.

Все команды выполняются из корня репозитория `mtv3-vision` на любом хосте с
Docker (Linux, Windows + WSL2, macOS). Внутри контейнера SDK лежит в
`/proj/rv1126_sdk_ai`, этот репозиторий в `/work/mtv3-vision`, `mtv3-bsp` в
`/work/mtv3-bsp`.

## 1. Какой SDK и почему

| | |
|---|---|
| Источник | группа GitLab `firefly-linux`: <https://gitlab.com/firefly-linux> |
| Манифест | `rv1126_rv1109_linux_ai_camera_release.xml` → `rv1126_rv1109/rv1126_rv1109_linux_ai_camera_release_20241012_v2.3.3b.xml` |
| Тег проектов | `refs/tags/rv1126_rv1109_ai/linux_release_v2.3.3b`, ветка `rv1126_rv1109_ai/firefly` |
| Ядро | Linux 4.19.111 (`kernel`, `arch/arm`) |
| U-Boot | 2017.09 (`u-boot`, defconfig `rv1126`) |
| Buildroot | 2018.02-rc3 (`buildroot`, тарболы в `buildroot/dl`) |
| Тулчейны | `prebuilts/gcc/linux-x86/arm/gcc-arm-8.3-2019.03-…` (rootfs и приложения), `gcc-linaro-6.3.1-2017.05-…` (U-Boot и ядро) |
| Сборочная система | `device/rockchip` (`build.sh`, `BoardConfig*.mk`, `mkfirmware.sh`, `rkflash.sh`) |

Это тот же Rockchip SDK `rv1126_rv1109_linux_ai_camera`, с которого начинался
проект (tar-архив релиза 20210306), но в виде git-репозиториев с историей и
более свежим релизом Firefly (v2.3.3b, октябрь 2024). Варианты манифеста:

* `rv1126_rv1109_linux_ai_camera_release.xml` — полный SDK: BSP + `app/*` +
  `external/*` (rkmedia, rkaiq, rknpu, rockx, mpp, rga …) + `buildroot/dl`.
  Нужен для сборки rootfs.
* `rv1126_rv1109_linux_ai_camera_bsp_release.xml` — только BSP:
  `device/rockchip`, `kernel`, `u-boot`, `rkbin`, `tools`, `docs`, тулчейны.
  Достаточно для ядра, U-Boot и прошивки без пересборки rootfs.

Группа <https://gitlab.com/rockchip_linux_sdk_6.1> к нашей плате не относится:
в ней только манифесты RK3588, RK3576, RK356x и RK3562 на ядре 6.1, RV1126
там нет. Использовать её можно лишь как справочник по новым версиям
`build.sh`, rknn-toolkit2 и прочих общих компонентов.

## 2. Среда: Docker-образ

SDK собирается только на Ubuntu 18.04 (glibc 2.27, gcc 7, python 2.7 и 3.6):
host-пакеты buildroot 2018.02 не собираются на glibc 2.28 и новее, dtc в ядре
и U-Boot ломается на gcc 10 и новее. Образ `mtv3-rv1126-sdk` описан в
`board/mtv3-rv1126/docker/` и содержит всё необходимое, включая `repo`,
CMake 3.27, ccache, `rkdeveloptool` и инструмент `mtv3-bsp`.

```sh
sh board/mtv3-rv1126/docker/build-image.sh        # один раз, ~900 МБ
```

Данные живут в docker volume, а не в образе:

| Volume | Содержимое |
|---|---|
| `mtv3-rv1126-sdk` | дерево SDK (`/proj/rv1126_sdk_ai`) |
| `mtv3-rv1126-home` | `/home/builder`: ccache, история, `.gitconfig` |

Место в docker data-root: около 20 ГБ после `repo sync` полного SDK, около
60 ГБ после полной сборки. Альтернатива volume — каталог хоста:
`RV1126_SDK=/abs/path sh board/mtv3-rv1126/docker/run-sdk.sh`.

## 3. Получение SDK с нуля

```sh
sh board/mtv3-rv1126/docker/init-sdk.sh            # полный SDK
sh board/mtv3-rv1126/docker/init-sdk.sh --bsp      # или только BSP
```

Скрипт выполняет внутри контейнера, с историей глубины 1
(`MTV3_REPO_DEPTH=0` даёт полную историю):

```sh
cd /proj/rv1126_sdk_ai
repo init --no-clone-bundle \
     --repo-url https://gitlab.com/firefly-linux/git-repo.git \
     -u https://gitlab.com/firefly-linux/manifests.git \
     -b master -m rv1126_rv1109_linux_ai_camera_release.xml --depth=1
repo sync -c --no-tags --no-clone-bundle -j4 --fail-fast
```

Повторное обновление: `sh board/mtv3-rv1126/docker/init-sdk.sh --sync`.
Воспроизводимая ревизия из `mtv3-bsp/manifest/pinned.xml`:
`init-sdk.sh --pinned /path/to/mtv3-bsp/manifest/pinned.xml`.

После sync в корне SDK появляются ссылки `build.sh`, `mkfirmware.sh`,
`rkflash.sh`, `Makefile`, `envsetup.sh` и каталоги `device/`, `kernel/`,
`u-boot/`, `buildroot/`, `rkbin/`, `tools/`, `prebuilts/`, `external/`,
`app/`, `docs/`. Проверка:

```sh
sh board/mtv3-rv1126/docker/run-sdk.sh bash -lc 'cd $RV1126_SDK && ./build.sh check && repo info -o 2>/dev/null | head'
```

## 4. Пробная сборка штатной платы Firefly

Прежде чем накладывать изменения MTV3, убедитесь, что среда собирает
«ванильную» конфигурацию. Ближайшие к нашему модулю конфигурации в
`device/rockchip/rv1126_rv1109/`: `aio-rv1126-jd4.mk` (Core-1126-JD4),
`cam-crv1126s2u-uvcc.mk` (камерный модуль, dts `rv1126-firefly-ai-cam`).

```sh
sh board/mtv3-rv1126/docker/run-sdk.sh            # интерактивный shell
sdk lunch                      # выбрать номер aio-rv1126-jd4.mk; или: sdk aio-rv1126-jd4.mk
sdk uboot                      # u-boot/ -> rockdev/uboot.img, MiniLoaderAll.bin
sdk kernel                     # kernel/ -> rockdev/boot.img (zImage + dtb, FIT)
sdk rootfs                     # buildroot: ~1-2 часа, лог в $RV1126_SDK/br.log
sdk firmware                   # собрать rockdev/ (parameter.txt, misc, oem, userdata)
sdk updateimg                  # rockdev/update.img для RKDevTool / upgrade_tool
```

`sdk` — функция оболочки контейнера, равная `cd $RV1126_SDK && ./build.sh`.
Переменные выбранной платы читаются из `device/rockchip/.BoardConfig.mk`
(символическая ссылка на выбранный `*.mk`): `RK_KERNEL_DTS`,
`RK_KERNEL_DEFCONFIG`, `RK_KERNEL_DEFCONFIG_FRAGMENT`, `RK_CFG_BUILDROOT`,
`RK_PARAMETER`, `RK_ROOTFS_TYPE`, `RK_OEM_DIR`, `RK_PACKAGE_FILE`.
Результаты: `rockdev/*.img`, `buildroot/output/<RK_CFG_BUILDROOT>/images/`,
тулчейн и staging там же в `host/` и `staging/`.

Buildroot-меню для выбранной конфигурации: `sdk-brconfig`
(`make O=output/<cfg> menuconfig`), после изменений
`make O=output/<cfg> savedefconfig` и копирование в `buildroot/configs/`.

## 5. Репозиторий mtv3-bsp: что это и как устроен

`mtv3-bsp` — отдельный git-репозиторий с изменениями именно нашей платы.
Он не содержит SDK, только дельту к нему, и накладывается инструментом
`mtv3-bsp` из образа. Контракт:

```
mtv3-bsp/
├── overlay/                 НОВЫЕ файлы, путь = путь в дереве SDK, копируются 1:1
│   ├── device/rockchip/rv1126_rv1109/mtv3-rv1126.mk       конфигурация платы (BoardConfig)
│   ├── device/rockchip/rv1126_rv1109/parameter-mtv3.txt   таблица разделов eMMC
│   ├── device/rockchip/oem/oem_mtv3/                      OEM-раздел: RkLunch.sh, usb_config.sh, sysconfig*
│   ├── kernel/arch/arm/boot/dts/rv1126-mtv3.dts           device tree модуля
│   ├── kernel/arch/arm/configs/rv1126_mtv3_defconfig      конфигурация ядра
│   ├── u-boot/arch/arm/dts/rv1126-mtv3.dts, u-boot/configs/rv1126_mtv3_defconfig
│   ├── buildroot/configs/rockchip_rv1126_rv1109_mtv3_defconfig
│   ├── buildroot/board/mtv3/rv1126/fs-overlay/            rootfs-overlay: /etc/init.d/S60mtv3,
│   │                                                      /usr/bin/init_mp.sh, mtv3_network.sh, ...
│   └── external/camera_engine_rkaiq/iqfiles/AVL_isp20/ov13850_*.xml   IQ-файл сенсора
├── patches/                 изменения СУЩЕСТВУЮЩИХ файлов, по репозиториям SDK, git format-patch
│   ├── kernel/0001-*.patch                                dts/Makefile, ov13850, rkcif, rkispp, CMA
│   ├── u-boot/0001-*.patch                                rv1126-u-boot.dtsi, rv1126.dtsi
│   ├── buildroot/0001-*.patch                             opencv3 + contrib aruco (= opencv3-aruco.mk.add)
│   ├── device/rockchip/…  external/rknpu/…               при необходимости
│   └── <repo>/base.txt                                    ревизия базы, информационно
├── review/                  спорное: отладочные патчи, бинарные SPL-блобы — не применяется
├── manifest/pinned.xml      `repo manifest -r` — точные ревизии SDK на момент export
└── README.md
```

Готовое наполнение в этой раскладке уже собрано из комплекта `mtv3-patches` (14.10.2026)
и лежит на внешнем диске в `mtv3-bsp-clean/` (там же `README.md` с описанием каждого
патча и списком отброшенных правок под Ubuntu 20.04). Это стартовое содержимое
репозитория `mtv3-bsp`.

Правила:

* **overlay — только новые файлы.** Если нужно изменить файл, который уже
  есть в SDK, это патч. Иначе при обновлении SDK изменения upstream будут
  молча затёрты.
* **patches — только коммиты.** Они генерируются из git-истории
  репозиториев SDK, руками их не редактируют.
* Имя buildroot-конфигурации `rockchip_rv1126_rv1109_mtv3` фиксировано:
  его ждут `board/mtv3-rv1126/toolchain-rv1126.cmake` и `Makefile` демона
  (`buildroot/output/rockchip_rv1126_rv1109_mtv3/{host,staging}`).

Внутри SDK инструмент работает так. В каждом затронутом репозитории
(`kernel`, `buildroot`, `device/rockchip` …) ставится тег `mtv3-base` на
ревизию манифеста и создаётся ветка `mtv3`: база + патчи + один коммит
`mtv3-bsp: overlay` с файлами overlay. Разработка ведётся обычными коммитами
в ветке `mtv3`; `export` раскладывает их обратно в `patches/` и обновляет
файлы в `overlay/`.

```
mtv3-bsp apply [--force]   наложить mtv3-bsp на SDK
mtv3-bsp export            SDK -> mtv3-bsp: patches, overlay, manifest/pinned.xml
mtv3-bsp status            ветка, число коммитов над базой и грязные файлы по репозиториям
mtv3-bsp track <path>      начать отслеживать ещё один репозиторий (например external/rknpu)
mtv3-bsp init-layout       создать пустую структуру каталогов
```

## 6. Рабочий цикл с mtv3-bsp

Клонировать `mtv3-bsp` рядом с `mtv3-vision` и передавать его путь в
`MTV3_BSP`; `run-sdk.sh` смонтирует его в `/work/mtv3-bsp`:

```sh
export MTV3_BSP=$HOME/Work/Zarnitza/sw/mtv3-bsp
sh board/mtv3-rv1126/docker/run-sdk.sh
```

Если репозиторий ещё пустой: скопировать в него содержимое `mtv3-bsp-clean/`
с внешнего диска (overlay, patches, review, README), затем добавить в `overlay/`
rootfs-overlay с `S60mtv3`, `init_mp.sh` и сетевыми скриптами из `board/mtv3-rv1126/`.

### 6.1. Наложить и собрать

```sh
mtv3-bsp apply                 # ветка mtv3 в kernel, u-boot, buildroot, device/rockchip, rkaiq
sdk mtv3-rv1126.mk             # выбрать нашу плату (build.sh принимает *.mk)
sdk uboot && sdk kernel && sdk rootfs
sdk firmware && sdk updateimg
```

Ключевые переменные `mtv3-rv1126.mk` (полная версия в overlay):

```sh
export RK_UBOOT_DEFCONFIG=rv1126_mtv3
export RK_KERNEL_DEFCONFIG=rv1126_mtv3_defconfig
export RK_KERNEL_DTS=rv1126-mtv3
export RK_CFG_BUILDROOT=rockchip_rv1126_rv1109_mtv3
export RK_PARAMETER=parameter-mtv3.txt
export RK_OEM_DIR=oem_mtv3
export RK_ROOTFS_TYPE=ext4
export RK_LOADER_UPDATE_SPL=true
```

Требования к ядру и rootfs для USB gadget, сети и OpenCV перечислены в
`board/mtv3-rv1126/README.md` (разделы «USB CDC ACM и RNDIS», «Порядок
действий», п. 1); сверять с ними `rv1126_mtv3_defconfig` и
`rockchip_rv1126_rv1109_mtv3_defconfig`.

### 6.2. Приложение и демон

После `sdk rootfs` в `buildroot/output/rockchip_rv1126_rv1109_mtv3/` есть
тулчейн и `staging` с OpenCV, поэтому:

```sh
mtv3-cmake                     # cpp/SeeSharp -> cpp/SeeSharp/build-rv1126/mainCV
mtv3-daemon                    # board/mtv3-rv1126/mtv3_cam_daemon
```

Чтобы бинарники попадали в прошивку, положите их в rootfs-overlay
`overlay/buildroot/board/mtv3/rv1126/fs-overlay/opt/seesharp/` (или
собирайте post-build-скриптом buildroot, указав его в defconfig через
`BR2_ROOTFS_POST_BUILD_SCRIPT`). Файлы вне git-истории SDK (сами бинарники)
в `mtv3-bsp` не коммитятся: это артефакты сборки.

### 6.3. Внести изменение и выгрузить

```sh
cd $RV1126_SDK/kernel                      # ветка mtv3 уже выбрана
$EDITOR drivers/media/i2c/imx415.c
git commit -am "imx415: MTV3 lane mapping"
cd $RV1126_SDK && mtv3-bsp status
mtv3-bsp export                            # -> /work/mtv3-bsp/patches/kernel/000N-imx415-....patch
cd /work/mtv3-bsp && git add -A && git commit -m "kernel: imx415 lane mapping"
```

Правка файла из `overlay/` делается там же, в дереве SDK, обычным коммитом:
`export` перепишет файл в `overlay/`, в патчи он не попадёт. Новый файл,
который должен стать частью overlay, сначала добавляют в `mtv3-bsp/overlay/`,
затем `mtv3-bsp apply --force`.

Если нужно менять репозиторий, которого ещё нет ни в `overlay/`, ни в
`patches/`: `mtv3-bsp track external/rknpu`, далее коммиты и `export`.

### 6.4. Обновление базы SDK

```sh
mtv3-bsp export                            # сохранить всё текущее
sh board/mtv3-rv1126/docker/init-sdk.sh --sync       # новые ревизии манифеста
cd $RV1126_SDK && repo forall -c 'git tag -d mtv3-base 2>/dev/null; git checkout -q --detach $REPO_LREV'
mtv3-bsp apply                             # тег mtv3-base встанет на новую базу
```

Конфликт при `git am` оставляет репозиторий в состоянии `am`, и `apply`
останавливается на нём. Порядок: исправить файлы, `git add`,
`git am --continue` до конца серии, затем `mtv3-bsp export` (патчи
перегенерируются уже от новой базы) и `mtv3-bsp apply --force`, чтобы
доложить overlay и остальные репозитории.

## 7. Прошивка

Модуль переводится в MaskROM или Loader (кнопка RECOVERY при подаче
питания, либо `reboot loader` из консоли). На Linux-хосте и в WSL2 с
`usbipd` устройство пробрасывается в контейнер флагом `MTV3_USB=1`:

```sh
MTV3_USB=1 sh board/mtv3-rv1126/docker/run-sdk.sh bash -lc '
  rkdeveloptool ld                                     # должен показать Maskrom/Loader
  cd $RV1126_SDK && ./rkflash.sh                       # весь rockdev/ по разделам
'
# либо одним образом:
#   tools/linux/Linux_Upgrade_Tool/upgrade_tool uf rockdev/update.img
# либо отдельные разделы:
#   rkdeveloptool db rockdev/MiniLoaderAll.bin && rkdeveloptool wlx boot rockdev/boot.img && rkdeveloptool rd
```

На Windows без WSL-проброса USB удобнее `tools/windows/RKDevTool` с
`rockdev/update.img`; `rv1126.cfg` для OpenOCD в `~/rockdev_mtv3` относится
к JTAG-отладке и к прошивке не требуется.

## 8. Типовые проблемы

| Симптом | Причина и действие |
|---|---|
| `host-m4`, `host-bison`: ошибки в `fseterr.c`, `libio.h` | хост не Ubuntu 18.04; собирать только в образе |
| `yylloc multiple definition` в dtc ядра или U-Boot | gcc 10 и новее на хосте; то же |
| `repo: command not found` / `repo is not installed` в пустом каталоге | норма до `repo init`; `init-sdk.sh` делает его сам |
| `repo init` просит имя и e-mail | в контейнере identity задаётся автоматически; на хосте `git config --global user.*` |
| `git am` не находит базу патча | патчи сделаны от другой ревизии: `cat patches/<repo>/base.txt`, сравнить с `repo info` |
| buildroot тянет сеть при оффлайн-сборке | проверить, что `buildroot/dl` синхронизирован (`buildroot-dl` в полном манифесте) |
| `fakeroot`, `qemu-user` получают `EPERM` | контейнер запущен не через `run-sdk.sh` (нужен `--security-opt seccomp=unconfined`) |
| медленно на Docker Desktop | SDK должен быть в volume, не в bind-mount с NTFS/`/mnt/c` |
| `No space left on device` | volume в docker data-root; проверить `docker system df`, перенести data-root |

## 9. Состояние проверки

Проверено на хосте разработчика 12.10.2026: сборка образа, версии
инструментов, `repo init` по манифесту Firefly (тег
`rv1126_rv1109_ai/linux_release_v2.3.3b`), раунд-трип `mtv3-bsp apply →
export → apply` на тестовых git-репозиториях. Полный `repo sync` и сборка
`uboot/kernel/rootfs` в этой среде ещё не выполнялись: требуют около 60 ГБ
и нескольких часов. Репозиторий `mtv3-bsp` на момент написания не заведён;
структура выше — целевой контракт, который понимает инструмент `mtv3-bsp`.
