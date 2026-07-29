// SeeSharp — composition root (по образцу main.py из SeeSharpPy):
//   1. Конфиг  2. Процессор через фабрику  3. ProcessingManager
//   4. IFrameSource  5. Sink  6. Pipeline.start()
//
// MTV3_BOARD (CMake-опция): источник — shm-ринг демона mtv3_cam_daemon --shm,
// sink — headless (fps-лог, позже — отправка метаданных). Десктоп: камера,
// imshow + запись combined.avi (TEST CODE).

#include <chrono>
#include <iostream>

#include <opencv2/opencv.hpp>

#include "config/config_reader.hpp"
#include "pipeline/pipeline.hpp"
#include "processing/processing_manager.hpp"

#ifdef MTV3_BOARD
#include "pipeline/shm_source.hpp"
#else
#include "pipeline/camera_source.hpp"
#endif

constexpr const char* CONFIG_PATH = "config.json";

int main()
{
    // 1. Конфиг
    ConfigReader reader;
    if (!reader.loadFromFile(CONFIG_PATH))
    {
        std::cerr << "Failed to open config file: " << CONFIG_PATH << std::endl;
        return -1;
    }

    // 2-3. Процессор (фабрика внутри менеджера)
    ProcessingManager manager(reader.getRawConfig());

    // 4. Источник кадров
#ifdef MTV3_BOARD
    ShmSource source;                    // ждёт /dev/shm/mtv3cam до 5 с
    if (!source.isOpened())
    {
        std::cerr << "Failed to open /dev/shm/mtv3cam (daemon not running?)" << std::endl;
        return -1;
    }
    std::cout << "shm source: " << source.width() << "x" << source.height() << std::endl;
#else
    CameraSource source(0);
    if (!source.isOpened())
    {
        std::cerr << "Failed to open video source" << std::endl;
        return -1;
    }
#endif

    // 5. Sink
#ifdef MTV3_BOARD
    // headless: fps раз в 100 кадров; сюда же встанет отправка metadata наружу
    auto t0 = std::chrono::steady_clock::now();
    long frames = 0;
    Pipeline pipeline(source, manager, [&](const ProcessedItem& item) {
        if (++frames % 100 == 0)
        {
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - t0).count();
            std::cout << "fps=" << 100.0 / dt
                      << " frames=" << frames
                      << " objects=" << item.metadata.size() << std::endl;
            t0 = now;
        }
        return true;
    });
#else
    // TEST CODE: показ + запись до/после в combined.avi
    cv::VideoWriter writer;
    double fps = source.fps();
    Pipeline pipeline(source, manager, [&](const ProcessedItem& item) {
        cv::Mat combined;
        cv::hconcat(item.frame, item.result, combined);
        if (!writer.isOpened())
            writer.open("combined.avi",
                        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                        fps, combined.size());
        writer.write(combined);
        cv::imshow("Result", combined);
        return cv::waitKey(1) != 27;     // ESC — остановка
    });
#endif

    // 6. Запуск (блокируется до остановки)
    pipeline.start();

    source.release();
#ifndef MTV3_BOARD
    cv::destroyAllWindows();
#endif
    return 0;
}
