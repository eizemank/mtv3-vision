cmake_minimum_required(VERSION 3.10)
project(MainCV)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Сборка под модуль MTV3 (RV1126): источник кадров — shm-ринг демона
# mtv3_cam_daemon, headless (без imshow/VideoWriter).
#   cmake -DCMAKE_TOOLCHAIN_FILE=<...>/board/mtv3-rv1126/toolchain-rv1126.cmake \
#         -DMTV3_BOARD=ON ..
option(MTV3_BOARD "Build for MTV3 board (ShmSource + headless)" OFF)
option(RASPBERRY_CM5 "Build for Raspberry Pi Compute Module 5 (V4L2 + headless)" OFF)
option(HOST_WEB_UI "Build host test version with synthetic video and web UI" OFF)
option(HOST_CAMERA_SOURCE "Use a real host camera in HOST_WEB_UI" OFF)

if(HOST_CAMERA_SOURCE AND NOT HOST_WEB_UI)
    message(FATAL_ERROR "HOST_CAMERA_SOURCE requires HOST_WEB_UI=ON")
endif()

if((MTV3_BOARD AND RASPBERRY_CM5) OR
   (MTV3_BOARD AND HOST_WEB_UI) OR
   (RASPBERRY_CM5 AND HOST_WEB_UI))
    message(FATAL_ERROR "MTV3_BOARD, RASPBERRY_CM5 and HOST_WEB_UI are mutually exclusive")
endif()
# Windows: хостовая сборка запускается через WSL (board/host/run_web_ui.ps1).
# Нативная сборка не поддерживается: ветки _WIN32 в include/platform/ написаны,
# но не собирались; MSVC к тому же не примет встроенную web-страницу (один
# строковый литерал ~170 КБ > лимита 64 КБ, C1091).
if(HOST_WEB_UI AND WIN32)
    message(FATAL_ERROR "HOST_WEB_UI on Windows is built inside WSL: run board/host/run_web_ui.ps1")
endif()

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# nlohmann_json: системный пакет либо вендоренный third_party/nlohmann/json.hpp
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp)
        add_library(nlohmann_json_vendored INTERFACE)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_vendored)
        target_include_directories(nlohmann_json_vendored INTERFACE
            ${CMAKE_SOURCE_DIR}/third_party)
    else()
        message(FATAL_ERROR "nlohmann_json not found. Vendored fallback:\n"
            "  mkdir -p ${CMAKE_SOURCE_DIR}/third_party/nlohmann\n"
            "  wget -O ${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp "
            "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp")
    endif()
endif()

# Include all .cpp files in the src directory
file(GLOB_RECURSE SRC_FILES src/*.cpp)
if(NOT MTV3_BOARD)
    # shm_source/rknn — только на модуле (десктоп: VideoCapture + cv::dnn)
    list(REMOVE_ITEM SRC_FILES
        ${CMAKE_SOURCE_DIR}/src/pipeline/shm_source.cpp
        ${CMAKE_SOURCE_DIR}/src/nn/rknn_classifier.cpp
        ${CMAKE_SOURCE_DIR}/src/processing/rknn_yolo_processor.cpp)
else()
    list(REMOVE_ITEM SRC_FILES
        ${CMAKE_SOURCE_DIR}/src/processing/yolo_processor.cpp)
endif()
if(NOT RASPBERRY_CM5)
    list(REMOVE_ITEM SRC_FILES
        ${CMAKE_SOURCE_DIR}/src/transport/dxl_uart_transport.cpp
        ${CMAKE_SOURCE_DIR}/src/transport/udp_video_transport.cpp)
endif()
if(NOT RASPBERRY_CM5 AND NOT MTV3_BOARD)
    list(REMOVE_ITEM SRC_FILES
        ${CMAKE_SOURCE_DIR}/src/system/system_admin.cpp)
endif()
if(NOT RASPBERRY_CM5 AND NOT MTV3_BOARD AND NOT HOST_WEB_UI)
    list(REMOVE_ITEM SRC_FILES
        ${CMAKE_SOURCE_DIR}/src/training/training_service.cpp
        ${CMAKE_SOURCE_DIR}/src/training/trainer_job.cpp
        ${CMAKE_SOURCE_DIR}/src/training/zip_writer.cpp
        ${CMAKE_SOURCE_DIR}/src/diagnostics/self_test.cpp
        ${CMAKE_SOURCE_DIR}/src/transport/binary_uart_transport.cpp
        ${CMAKE_SOURCE_DIR}/src/transport/transport_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/transport/udp_metadata_transport.cpp
        ${CMAKE_SOURCE_DIR}/src/transport/usb_stream_transport.cpp
        ${CMAKE_SOURCE_DIR}/src/transport/websocket_metadata_transport.cpp)
endif()

add_executable(mainCV ${SRC_FILES})

if(MTV3_BOARD)
    target_compile_definitions(mainCV PRIVATE MTV3_BOARD)
    # librknn_api из свежесобранного rknpu-дерева buildroot
    # (RKNN_SYSROOT задаёт toolchain-rv1126.cmake, можно переопределить -D)
    if(NOT DEFINED RKNN_SYSROOT)
        message(FATAL_ERROR "RKNN_SYSROOT not set (use toolchain-rv1126.cmake)")
    endif()
    target_include_directories(mainCV PRIVATE ${RKNN_SYSROOT}/usr/include/rknn)
    target_link_libraries(mainCV PRIVATE ${RKNN_SYSROOT}/usr/lib/librknn_api.so dl)
endif()

target_link_libraries(mainCV PRIVATE ${OpenCV_LIBS}
    nlohmann_json::nlohmann_json Threads::Threads)
if(WIN32)
    # WinSock2 для HTTP/UDP/WebSocket; _WIN32_WINNT >= Vista ради inet_pton/WSAPoll
    target_compile_definitions(mainCV PRIVATE _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(mainCV PRIVATE ws2_32)
    # Запуск mainCV.exe вне MSYS2-оболочки: без зависимости от libgcc/libstdc++ DLL
    option(SEESHARP_STATIC_RUNTIME "Link libgcc/libstdc++ statically (MinGW)" ON)
    if(SEESHARP_STATIC_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(mainCV PRIVATE -static-libgcc -static-libstdc++)
    endif()
endif()
target_include_directories(mainCV PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include)

if(RASPBERRY_CM5)
    target_compile_definitions(mainCV PRIVATE RASPBERRY_CM5)
endif()

if(RASPBERRY_CM5 OR HOST_WEB_UI)
    add_custom_target(download_yolo11n
        COMMAND sh
            ${CMAKE_SOURCE_DIR}/../../board/raspberry-cm5/download_yolo11n.sh
            ${CMAKE_BINARY_DIR}
            ${CMAKE_BINARY_DIR}/config.json
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Downloading YOLO11n ONNX for OpenCV DNN")
endif()

if(HOST_WEB_UI)
    target_compile_definitions(mainCV PRIVATE HOST_WEB_UI)
    file(COPY ${CMAKE_SOURCE_DIR}/assets/synthetic
         DESTINATION ${CMAKE_BINARY_DIR}/assets)
endif()
if(HOST_CAMERA_SOURCE)
    target_compile_definitions(mainCV PRIVATE HOST_CAMERA_SOURCE)
endif()

# Конфиг можно переопределить абсолютным путём через -DSEESHARP_CONFIG_FILE=...
set(SEESHARP_CONFIG_FILE "" CACHE FILEPATH "Initial SeeSharp config.json")
if(SEESHARP_CONFIG_FILE)
    set(DEFAULT_CONFIG ${SEESHARP_CONFIG_FILE})
elseif(MTV3_BOARD)
    set(DEFAULT_CONFIG ${CMAKE_SOURCE_DIR}/../../common/config/config/config.json)
else()
    set(DEFAULT_CONFIG ${CMAKE_SOURCE_DIR}/../../common/config/config.json)
endif()
if(NOT EXISTS ${DEFAULT_CONFIG})
    set(DEFAULT_CONFIG ${CMAKE_SOURCE_DIR}/config/config.json)
endif()
configure_file(${DEFAULT_CONFIG} ${CMAKE_BINARY_DIR}/config.json COPYONLY)
