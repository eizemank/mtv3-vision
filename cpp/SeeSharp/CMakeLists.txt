cmake_minimum_required(VERSION 3.10)
project(MainCV)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Сборка под модуль MTV3 (RV1126): источник кадров — shm-ринг демона
# mtv3_cam_daemon, headless (без imshow/VideoWriter).
#   cmake -DCMAKE_TOOLCHAIN_FILE=<...>/board/mtv3-rv1126/toolchain-rv1126.cmake \
#         -DMTV3_BOARD=ON ..
option(MTV3_BOARD "Build for MTV3 board (ShmSource + headless)" OFF)

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
        ${CMAKE_SOURCE_DIR}/src/nn/rknn_classifier.cpp)
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
target_include_directories(mainCV PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include)

# конфиг: единый sw/common/config/config.json; локальный config/ — fallback
set(COMMON_CONFIG ${CMAKE_SOURCE_DIR}/../../common/config/config.json)
if(EXISTS ${COMMON_CONFIG})
    configure_file(${COMMON_CONFIG} ${CMAKE_BINARY_DIR}/config.json COPYONLY)
else()
    configure_file(${CMAKE_SOURCE_DIR}/config/config.json ${CMAKE_BINARY_DIR}/config.json COPYONLY)
endif()
