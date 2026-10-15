# toolchain-rv1126.cmake — кросс-сборка CMake-проектов тулчейном buildroot SDK.
# Использование:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.../board/mtv3-rv1126/toolchain-rv1126.cmake \
#         -DMTV3_BOARD=ON ..
# Путь к SDK можно переопределить: export RV1126_SDK=/path/to/sdk

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(DEFINED ENV{RV1126_SDK})
    set(RV1126_SDK "$ENV{RV1126_SDK}")
else()
    set(RV1126_SDK "$ENV{HOME}/proj/rv1126_sdk_ai")
endif()
set(RV1126_OUT "${RV1126_SDK}/buildroot/output/rockchip_rv1126_rv1109_mtv3")

set(CMAKE_C_COMPILER   "${RV1126_OUT}/host/bin/arm-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${RV1126_OUT}/host/bin/arm-linux-gnueabihf-g++")

set(CMAKE_SYSROOT "${RV1126_OUT}/staging")
set(CMAKE_FIND_ROOT_PATH "${RV1126_OUT}/staging")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# OpenCVConfig.cmake buildroot оставляет только в staging (из target его удаляет)
set(OpenCV_DIR "${RV1126_OUT}/staging/usr/share/OpenCV")

# librknn_api/rknn_api.h: при BR2_PACKAGE_RKNPU=y в defconfig платы (Firefly SDK)
# rknpu ставит их в staging этой же сборки; в старом дереве они лежали в "другом"
# output buildroot. Берём staging, если библиотека там есть, иначе старый путь.
if(EXISTS "${RV1126_OUT}/staging/usr/lib/librknn_api.so")
    set(RKNN_SYSROOT_DEFAULT "${RV1126_OUT}/staging")
else()
    set(RKNN_SYSROOT_DEFAULT
        "${RV1126_SDK}/buildroot/output/host/arm-buildroot-linux-gnueabihf/sysroot")
endif()
set(RKNN_SYSROOT "${RKNN_SYSROOT_DEFAULT}" CACHE PATH "sysroot with librknn_api")
