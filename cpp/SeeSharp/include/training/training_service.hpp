#pragma once
// TrainingService — сбор датасета с живой камеры, запуск обучения
// классификатора на устройстве, реестр моделей, экспорт/загрузка для
// обучения на хосте. Обслуживает HTTP-маршруты /training/* и /metadata/last
// (см. docs/USER_GUIDE_RU.md). Конструктор и configure() без I/O — boot SLA.
//
// Раскладка (пути относительно каталога config.json):
//   <dataset_dir>/classifier/<class>/<ts>_<seq>.jpg   ImageFolder для тренера
//   <dataset_dir>/classifier/index.jsonl              происхождение crop'ов
//   <dataset_dir>/classifier/classes.json             {slug: отображаемое имя}
//   <dataset_dir>/yolo/{images,labels}/, classes.json  этап 2 (разметка рамками)
//   <models_dir>/<name>.onnx + <name>.classes.json    модели (+ latest.json)

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "model/classifier_params.hpp"
#include "platform/socket.hpp"
#include "training/frame_tap.hpp"
#include "training/trainer_job.hpp"

struct HttpReply
{
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    std::string filePath;        // если задан — сервер отдаёт файл потоково
    std::string extraHeaders;    // "Header: value\r\n"
    bool deleteFileAfterSend = false;
};

class TrainingService
{
public:
    /// Мутатор конфига: загрузить config.json, применить изменение, применить
    /// к конвейеру и сохранить. Возвращает "" или текст ошибки.
    using ConfigMutator =
        std::function<std::string(const std::function<void(nlohmann::json&)>&)>;

    TrainingService(FrameTap& tap, ConfigMutator mutateConfig);
    ~TrainingService();

    /// Дешёвое обновление настроек из секций training/classification/
    /// object_detection (вызывается при каждом apply).
    void configure(const nlohmann::json& config, const std::filesystem::path& configDir);
    bool requiresAdminToken() const;

    /// true — запрос обслужен (path с query-строкой, без хоста).
    bool route(const std::string& method, const std::string& target,
               const std::string& body, HttpReply& reply);

    /// POST /training/model: дочитать тело из сокета в файл моделей.
    /// headers — заголовки запроса, prefix — уже прочитанная часть тела.
    void receiveUpload(platform::socket_t socket, const std::string& headers, const std::string& prefix,
                       long contentLength, HttpReply& reply);

private:
    struct Settings
    {
        std::filesystem::path datasetDir;
        std::filesystem::path modelsDir;
        std::string python = "python3";
        std::string classifierScript;
        int classifierEpochs = 20;
        int classifierImageSize = 64;
        int jpegQuality = 90;
        int maxDatasetMb = 512;
        int maxSamplesPerClass = 2000;
        int maxBurst = 200;
        int minBurstIntervalMs = 50;
        int maxUploadMb = 64;
        bool requireAdminToken = false;
        // из секции classification (для захвата и подсказок UI)
        ClassifierRegionMode regionMode = ClassifierRegionMode::Whole;
        std::array<float, 4> roi{0.25f, 0.25f, 0.5f, 0.5f};
        std::vector<int> blobPatternIds;
        float cropPadding = 0.1f;
        int maxRegions = 8;
        std::string activeModel;
        std::vector<std::string> classNames;          // classification.class_names
        std::vector<std::string> yoloClassNames;      // object_detection.class_names
        std::string yoloClassNamesFile;
        std::string yoloModel;
        std::string processingMode;
    };

    struct CaptureRequest
    {
        std::string kind = "classifier";              // classifier | yolo
        std::string className;
        ClassifierRegionMode regionMode = ClassifierRegionMode::Whole;
        bool roiOverride = false;
        std::array<float, 4> roi{};
        std::vector<int> blobPatternIds;
        int count = 1;
        int intervalMs = 200;
    };

    struct CaptureStatus
    {
        bool active = false;
        std::string kind;
        std::string className;
        int done = 0;
        int total = 0;
        int saved = 0;
        std::string lastFile;
        std::string error;
        int64_t startedMs = 0;
    };

    // --- маршруты ---
    void handleStatus(HttpReply& reply);
    void handleCheck(HttpReply& reply);
    void handleClasses(const nlohmann::json& body, HttpReply& reply);
    void handleCapture(const nlohmann::json& body, HttpReply& reply);
    void handleSamples(const std::string& query, HttpReply& reply);
    void handleThumb(const std::string& query, HttpReply& reply);
    void handleDeleteSamples(const nlohmann::json& body, HttpReply& reply);
    void handleTrain(const nlohmann::json& body, HttpReply& reply);
    void handleModels(HttpReply& reply);
    void handleActivate(const nlohmann::json& body, HttpReply& reply);
    void handleExport(const std::string& query, HttpReply& reply);
    void handleMetadataLast(HttpReply& reply);
    void handleYoloSamples(const std::string& query, HttpReply& reply);
    void handleYoloImage(const std::string& query, HttpReply& reply);
    void handleYoloLabels(const std::string& query, HttpReply& reply);
    void handleYoloSaveLabels(const nlohmann::json& body, HttpReply& reply);
    void handleYoloClasses(const nlohmann::json& body, HttpReply& reply);
    void handleYoloDelete(const nlohmann::json& body, HttpReply& reply);

    // --- датасет ---
    std::filesystem::path classifierDir() const;
    std::filesystem::path yoloDir() const;
    nlohmann::json classesJson() const;
    nlohmann::json datasetSummary() const;
    uint64_t datasetBytes() const;
    std::string quotaError(const std::string& className, int extra) const;
    void appendIndex(const nlohmann::json& record) const;
    static bool validClassName(const std::string& name);
    static bool validFileName(const std::string& name);
    std::string sampleName(uint32_t sequence) const;

    // --- захват ---
    std::string startCapture(const CaptureRequest& request);
    void captureLoop(CaptureRequest request);
    void stopCapture();
    CaptureStatus captureStatus() const;

    // --- модели ---
    nlohmann::json modelsJson() const;
    nlohmann::json trainerCheck(bool force);
    std::vector<std::string> classNamesForType(ProcessingType type);

    Settings settings() const;

    FrameTap& tap_;
    ConfigMutator mutateConfig_;
    mutable std::mutex mutex_;
    Settings settings_;
    std::filesystem::path configDir_;

    std::thread captureThread_;
    std::atomic<bool> captureStop_{false};
    CaptureStatus capture_;
    mutable std::mutex indexMutex_;
    std::atomic<uint32_t> sampleSequence_{0};

    TrainerJob trainer_;
    nlohmann::json trainerCheck_;          // кэш пробы python/torch
    mutable std::mutex namesMutex_;
    std::string yoloNamesPath_;
    std::vector<std::string> yoloNamesCache_;
};

// --- утилиты для HTTP (используются и в main.cpp) ---
std::string urlDecode(const std::string& value);
std::string queryValue(const std::string& query, const std::string& key);
std::string headerValue(const std::string& headers, const std::string& name);
