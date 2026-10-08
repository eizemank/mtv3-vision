#include "training/training_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "processing/region_crop.hpp"
#include "training/zip_writer.hpp"

namespace fs = std::filesystem;

namespace
{
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string timeStamp()
{
    const auto now = std::chrono::system_clock::now();
    const time_t seconds = std::chrono::system_clock::to_time_t(now);
    const int millis = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    struct tm t{};
    localtime_r(&seconds, &t);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &t);
    char full[48];
    snprintf(full, sizeof(full), "%s_%03d", buffer, millis);
    return full;
}

nlohmann::json errorJson(const std::string& message)
{
    return {{"error", message}};
}

void jsonReply(HttpReply& reply, const nlohmann::json& body, int status = 200)
{
    reply.status = status;
    reply.contentType = "application/json";
    reply.body = body.dump();
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFileAtomic(const fs::path& path, const std::string& data)
{
    const fs::path tmp = path.string() + ".part";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out)
            return false;
    }
    std::error_code error;
    fs::rename(tmp, path, error);
    return !error;
}

int64_t fileTimeMs(const fs::path& path)
{
    std::error_code error;
    const auto ft = fs::last_write_time(path, error);
    if (error)
        return 0;
    const auto sys = std::chrono::time_point_cast<std::chrono::milliseconds>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return sys.time_since_epoch().count();
}

std::vector<std::string> readNamesFile(const std::string& path)
{
    std::vector<std::string> names;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty())
            names.push_back(line);
    }
    return names;
}

std::string relativeToDir(const fs::path& file, const fs::path& dir)
{
    std::error_code error;
    const fs::path relative = fs::relative(file, dir, error);
    if (error || relative.empty() || relative.string().compare(0, 2, "..") == 0)
        return file.string();
    return relative.generic_string();
}

const char* processingTypeName(ProcessingType type)
{
    switch (type)
    {
        case ProcessingType::BlobDetection: return "blob_detection";
        case ProcessingType::LineDetection: return "line_detection";
        case ProcessingType::CircleDetection: return "circle_detection";
        case ProcessingType::ArucoDetection: return "aruco_detection";
        case ProcessingType::Classification: return "classification";
        case ProcessingType::ObjectDetection: return "object_detection";
        default: return "off";
    }
}
}  // namespace

// ---------------------------------------------------------------- utilities

std::string urlDecode(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '+')
            out += ' ';
        else if (value[i] == '%' && i + 2 < value.size() && isxdigit(value[i + 1]) &&
                 isxdigit(value[i + 2]))
        {
            out += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
            i += 2;
        }
        else
            out += value[i];
    }
    return out;
}

std::string queryValue(const std::string& query, const std::string& key)
{
    size_t start = 0;
    while (start < query.size())
    {
        size_t end = query.find('&', start);
        if (end == std::string::npos)
            end = query.size();
        const std::string pair = query.substr(start, end - start);
        const size_t eq = pair.find('=');
        if (pair.substr(0, eq) == key)
            return eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
        start = end + 1;
    }
    return "";
}

std::string headerValue(const std::string& headers, const std::string& name)
{
    size_t pos = 0;
    while ((pos = headers.find("\r\n", pos)) != std::string::npos)
    {
        pos += 2;
        if (headers.size() - pos < name.size() + 1)
            break;
        bool match = true;
        for (size_t i = 0; i < name.size(); ++i)
            if (tolower(headers[pos + i]) != tolower(name[i]))
            {
                match = false;
                break;
            }
        if (!match || headers[pos + name.size()] != ':')
            continue;
        size_t valueStart = pos + name.size() + 1;
        while (valueStart < headers.size() && headers[valueStart] == ' ')
            ++valueStart;
        size_t valueEnd = headers.find("\r\n", valueStart);
        if (valueEnd == std::string::npos)
            valueEnd = headers.size();
        return headers.substr(valueStart, valueEnd - valueStart);
    }
    return "";
}

// ------------------------------------------------------------- construction

TrainingService::TrainingService(FrameTap& tap, ConfigMutator mutateConfig)
    : tap_(tap), mutateConfig_(std::move(mutateConfig))
{
    trainerCheck_ = {{"checked", false}, {"available", false}, {"reason", "not checked"}};
}

TrainingService::~TrainingService()
{
    stopCapture();
    trainer_.stop();
}

void TrainingService::configure(const nlohmann::json& config, const fs::path& configDir)
{
    Settings s;
    const auto training = config.value("training", nlohmann::json::object());
    auto resolve = [&](const std::string& value) {
        fs::path path = value;
        return path.is_relative() ? (configDir / path).lexically_normal() : path;
    };
    s.datasetDir = resolve(training.value("dataset_dir", std::string("datasets")));
    s.modelsDir = resolve(training.value("models_dir", std::string("models")));
    s.python = training.value("python", std::string("python3"));
    s.classifierScript = training.value("classifier_script", std::string(""));
    s.classifierEpochs = std::max(1, training.value("classifier_epochs", 20));
    s.classifierImageSize = std::max(16, training.value("classifier_image_size", 64));
    s.jpegQuality = std::clamp(training.value("jpeg_quality", 90), 30, 100);
    s.maxDatasetMb = std::max(1, training.value("max_dataset_mb", 512));
    s.maxSamplesPerClass = std::max(1, training.value("max_samples_per_class", 2000));
    s.maxBurst = std::clamp(training.value("max_burst", 200), 1, 10000);
    s.minBurstIntervalMs = std::clamp(training.value("min_burst_interval_ms", 50), 0, 60000);
    s.maxUploadMb = std::clamp(training.value("max_upload_mb", 64), 1, 2048);
    s.requireAdminToken = training.value("require_admin_token", false);

    const auto classification = config.value("classification", nlohmann::json::object());
    try
    {
        ClassifierParams params = classification.get<ClassifierParams>();
        s.regionMode = params.regionMode;
        s.roi = params.roi;
        s.blobPatternIds = params.blobPatternIds;
        s.cropPadding = params.cropPadding;
        s.maxRegions = params.maxRegions;
        s.classNames = params.classNames;
        s.activeModel = params.modelOnnx;
    }
    catch (...) {}
    const auto detection = config.value("object_detection", nlohmann::json::object());
    s.yoloClassNames = detection.value("class_names", std::vector<std::string>{});
    s.yoloClassNamesFile = detection.value("class_names_file", std::string(""));
    s.yoloModel = detection.value("model_onnx", std::string(""));
    if (!s.yoloClassNamesFile.empty())
        s.yoloClassNamesFile = resolve(s.yoloClassNamesFile).string();
    s.processingMode = config.value("general_params", nlohmann::json::object())
                           .value("processing_mode", std::string(""));

    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = s;
    configDir_ = configDir;
}

bool TrainingService::requiresAdminToken() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.requireAdminToken;
}

TrainingService::Settings TrainingService::settings() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

fs::path TrainingService::classifierDir() const { return settings().datasetDir / "classifier"; }
fs::path TrainingService::yoloDir() const { return settings().datasetDir / "yolo"; }

// ----------------------------------------------------------------- routing

bool TrainingService::route(const std::string& method, const std::string& target,
                            const std::string& body, HttpReply& reply)
{
    const size_t q = target.find('?');
    const std::string path = target.substr(0, q);
    const std::string query = q == std::string::npos ? "" : target.substr(q + 1);
    const bool post = method == "POST";
    nlohmann::json json = nlohmann::json::object();
    if (post && !body.empty())
    {
        try
        {
            json = nlohmann::json::parse(body);
        }
        catch (const std::exception& error)
        {
            jsonReply(reply, errorJson(std::string("invalid json: ") + error.what()), 400);
            return true;
        }
    }
    try
    {
        if (path == "/metadata/last") handleMetadataLast(reply);
        else if (path == "/training/status") handleStatus(reply);
        else if (path == "/training/check" && post) handleCheck(reply);
        else if (path == "/training/classes" && post) handleClasses(json, reply);
        else if (path == "/training/capture" && post) handleCapture(json, reply);
        else if (path == "/training/capture/stop" && post)
        {
            stopCapture();
            handleStatus(reply);
        }
        else if (path == "/training/samples") handleSamples(query, reply);
        else if (path == "/training/thumb") handleThumb(query, reply);
        else if (path == "/training/samples/delete" && post) handleDeleteSamples(json, reply);
        else if (path == "/training/train" && post) handleTrain(json, reply);
        else if (path == "/training/train/stop" && post)
        {
            trainer_.stop();
            jsonReply(reply, trainer_.status());
        }
        else if (path == "/training/train/log")
        {
            const std::string since = queryValue(query, "since");
            jsonReply(reply, trainer_.log(since.empty() ? 0 : std::stoull(since)));
        }
        else if (path == "/training/models") handleModels(reply);
        else if (path == "/training/activate" && post) handleActivate(json, reply);
        else if (path == "/training/export") handleExport(query, reply);
        else if (path == "/training/yolo/samples") handleYoloSamples(query, reply);
        else if (path == "/training/yolo/image") handleYoloImage(query, reply);
        else if (path == "/training/yolo/labels" && post) handleYoloSaveLabels(json, reply);
        else if (path == "/training/yolo/labels") handleYoloLabels(query, reply);
        else if (path == "/training/yolo/classes" && post) handleYoloClasses(json, reply);
        else if (path == "/training/yolo/delete" && post) handleYoloDelete(json, reply);
        else
            return false;
    }
    catch (const std::exception& error)
    {
        jsonReply(reply, errorJson(error.what()), 500);
    }
    return true;
}

// ----------------------------------------------------------------- dataset

bool TrainingService::validClassName(const std::string& name)
{
    static const std::regex pattern("^[A-Za-z0-9][A-Za-z0-9_-]{0,31}$");
    return std::regex_match(name, pattern);
}

bool TrainingService::validFileName(const std::string& name)
{
    if (name.empty() || name.size() > 128 || name[0] == '.')
        return false;
    for (char c : name)
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
            return false;
    return name.find("..") == std::string::npos;
}

std::string TrainingService::sampleName(uint32_t sequence) const
{
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "_%04u", sequence % 10000);
    return timeStamp() + suffix + ".jpg";
}

nlohmann::json TrainingService::classesJson() const
{
    nlohmann::json names = nlohmann::json::object();
    std::error_code error;
    const fs::path file = classifierDir() / "classes.json";
    if (fs::exists(file, error))
    {
        try
        {
            names = nlohmann::json::parse(readFile(file));
        }
        catch (...) {}
    }
    return names;
}

nlohmann::json TrainingService::datasetSummary() const
{
    nlohmann::json classes = nlohmann::json::array();
    const fs::path dir = classifierDir();
    const nlohmann::json displayNames = classesJson();
    uint64_t bytes = 0;
    std::error_code error;
    std::vector<std::string> names;
    if (fs::is_directory(dir, error))
        for (const auto& entry : fs::directory_iterator(dir, error))
            if (entry.is_directory(error) && validClassName(entry.path().filename().string()))
                names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    for (const std::string& name : names)
    {
        int count = 0;
        uint64_t classBytes = 0;
        for (const auto& file : fs::directory_iterator(dir / name, error))
        {
            if (!file.is_regular_file(error) || file.path().extension() != ".jpg")
                continue;
            ++count;
            classBytes += file.file_size(error);
        }
        bytes += classBytes;
        classes.push_back({{"name", name}, {"count", count}, {"bytes", classBytes},
                           {"display", displayNames.value(name, name)}});
    }
    const Settings s = settings();
    uint64_t available = 0;
    const fs::space_info space = fs::space(fs::exists(s.datasetDir, error) ? s.datasetDir
                                           : s.datasetDir.parent_path(), error);
    if (!error)
        available = space.available;
    return {{"dir", dir.string()}, {"bytes", bytes}, {"classes", classes},
            {"max_bytes", static_cast<uint64_t>(s.maxDatasetMb) * 1024 * 1024},
            {"free_bytes", available}, {"max_samples_per_class", s.maxSamplesPerClass},
            {"max_burst", s.maxBurst}, {"min_burst_interval_ms", s.minBurstIntervalMs}};
}

uint64_t TrainingService::datasetBytes() const
{
    uint64_t bytes = 0;
    std::error_code error;
    const fs::path dir = settings().datasetDir;
    if (!fs::is_directory(dir, error))
        return 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir, error))
        if (entry.is_regular_file(error))
            bytes += entry.file_size(error);
    return bytes;
}

std::string TrainingService::quotaError(const std::string& className, int extra) const
{
    const Settings s = settings();
    std::error_code error;
    fs::create_directories(s.datasetDir, error);
    const fs::space_info space = fs::space(s.datasetDir, error);
    if (!error && space.available < 100ull * 1024 * 1024)
        return "less than 100 MiB free on the dataset disk";
    if (datasetBytes() > static_cast<uint64_t>(s.maxDatasetMb) * 1024 * 1024)
        return "dataset quota reached (training.max_dataset_mb)";
    if (!className.empty())
    {
        int count = 0;
        for (const auto& file : fs::directory_iterator(classifierDir() / className, error))
            if (file.is_regular_file(error) && file.path().extension() == ".jpg")
                ++count;
        if (count + extra > s.maxSamplesPerClass)
            return "class sample limit reached (training.max_samples_per_class)";
    }
    return "";
}

void TrainingService::appendIndex(const nlohmann::json& record) const
{
    std::lock_guard<std::mutex> lock(indexMutex_);
    const fs::path file = (record.value("kind", "classifier") == "yolo" ? yoloDir()
                                                                        : classifierDir()) / "index.jsonl";
    std::ofstream out(file, std::ios::app);
    out << record.dump() << '\n';
}

// ----------------------------------------------------------------- handlers

void TrainingService::handleStatus(HttpReply& reply)
{
    const Settings s = settings();
    const CaptureStatus capture = captureStatus();
    nlohmann::json check;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        check = trainerCheck_;
    }
    nlohmann::json yolo = nlohmann::json::object();
    {
        std::error_code error;
        int images = 0, labels = 0;
        const fs::path images_dir = yoloDir() / "images";
        const fs::path labels_dir = yoloDir() / "labels";
        if (fs::is_directory(images_dir, error))
            for (const auto& e : fs::directory_iterator(images_dir, error))
                if (e.path().extension() == ".jpg") ++images;
        if (fs::is_directory(labels_dir, error))
            for (const auto& e : fs::directory_iterator(labels_dir, error))
                if (e.path().extension() == ".txt") ++labels;
        nlohmann::json classes = nlohmann::json::array();
        if (fs::exists(yoloDir() / "classes.json", error))
        {
            try { classes = nlohmann::json::parse(readFile(yoloDir() / "classes.json")); }
            catch (...) {}
        }
        yolo = {{"images", images}, {"labels", labels}, {"classes", classes}};
    }
    jsonReply(reply, {
        {"dataset", datasetSummary()},
        {"yolo", yolo},
        {"capture", {{"active", capture.active}, {"kind", capture.kind},
                     {"class", capture.className}, {"done", capture.done},
                     {"total", capture.total}, {"saved", capture.saved},
                     {"last_file", capture.lastFile}, {"error", capture.error}}},
        {"trainer", trainer_.status()},
        {"trainer_check", check},
        {"models", modelsJson()},
        {"active", {{"model", s.activeModel}, {"region_mode", classifierRegionModeToString(s.regionMode)},
                    {"roi", s.roi}, {"blob_pattern_ids", s.blobPatternIds},
                    {"crop_padding", s.cropPadding}, {"class_names", s.classNames},
                    {"processing_mode", s.processingMode}, {"yolo_model", s.yoloModel}}},
        {"settings", {{"python", s.python}, {"classifier_script", s.classifierScript},
                      {"classifier_epochs", s.classifierEpochs},
                      {"classifier_image_size", s.classifierImageSize},
                      {"models_dir", s.modelsDir.string()}, {"max_upload_mb", s.maxUploadMb}}}});
}

nlohmann::json TrainingService::trainerCheck(bool force)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!force && trainerCheck_.value("checked", false))
            return trainerCheck_;
    }
    const Settings s = settings();
    nlohmann::json result = {{"checked", true}, {"available", false}, {"python", s.python},
                             {"script", s.classifierScript}, {"checked_ms", nowMs()}};
    std::error_code error;
    if (s.classifierScript.empty() || !fs::exists(s.classifierScript, error))
        result["reason"] = "trainer script not found: " + s.classifierScript;
    else
    {
        int exitCode = -1;
        const std::string output = TrainerJob::runCapture(
            {s.python, "-c",
             "import torch, torchvision; print(torch.__version__, torchvision.__version__)"},
            20000, exitCode);
        std::string trimmed = output;
        while (!trimmed.empty() && isspace(static_cast<unsigned char>(trimmed.back())))
            trimmed.pop_back();
        result["output"] = trimmed.substr(0, 2000);
        if (exitCode == 0)
        {
            result["available"] = true;
            result["reason"] = "";
        }
        else if (exitCode == 127)
            result["reason"] = "python not found: " + s.python;
        else
            result["reason"] = "PyTorch is not importable with " + s.python;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    trainerCheck_ = result;
    return result;
}

void TrainingService::handleCheck(HttpReply& reply)
{
    jsonReply(reply, trainerCheck(true));
}

void TrainingService::handleClasses(const nlohmann::json& body, HttpReply& reply)
{
    const std::string op = body.value("op", "");
    const std::string name = body.value("name", "");
    if (!validClassName(name))
    {
        jsonReply(reply, errorJson("class name must match [A-Za-z0-9][A-Za-z0-9_-]{0,31}"), 400);
        return;
    }
    const fs::path dir = classifierDir();
    std::error_code error;
    nlohmann::json display = classesJson();
    if (op == "add")
    {
        fs::create_directories(dir / name, error);
        if (error)
        {
            jsonReply(reply, errorJson("cannot create class directory: " + error.message()), 500);
            return;
        }
        if (body.contains("display") && body["display"].is_string())
            display[name] = body["display"];
    }
    else if (op == "rename")
    {
        const std::string newName = body.value("new_name", "");
        if (!validClassName(newName))
        {
            jsonReply(reply, errorJson("invalid new_name"), 400);
            return;
        }
        if (captureStatus().active || trainer_.running())
        {
            jsonReply(reply, errorJson("stop capture/training before renaming"), 409);
            return;
        }
        fs::rename(dir / name, dir / newName, error);
        if (error)
        {
            jsonReply(reply, errorJson("rename failed: " + error.message()), 500);
            return;
        }
        if (display.contains(name))
        {
            display[newName] = display[name];
            display.erase(name);
        }
    }
    else if (op == "delete")
    {
        if (!body.value("confirm", false))
        {
            jsonReply(reply, errorJson("delete requires confirm:true"), 400);
            return;
        }
        if (captureStatus().active || trainer_.running())
        {
            jsonReply(reply, errorJson("stop capture/training before deleting"), 409);
            return;
        }
        fs::remove_all(dir / name, error);
        display.erase(name);
    }
    else if (op == "display")
    {
        display[name] = body.value("display", name);
    }
    else
    {
        jsonReply(reply, errorJson("unknown op"), 400);
        return;
    }
    fs::create_directories(dir, error);
    writeFileAtomic(dir / "classes.json", display.dump(1));
    handleStatus(reply);
}

void TrainingService::handleCapture(const nlohmann::json& body, HttpReply& reply)
{
    const Settings s = settings();
    CaptureRequest request;
    request.kind = body.value("kind", "classifier");
    request.className = body.value("class", "");
    request.count = std::clamp(body.value("count", 1), 1, s.maxBurst);
    request.intervalMs = std::max(s.minBurstIntervalMs, body.value("interval_ms", 200));
    request.regionMode = s.regionMode;
    request.blobPatternIds = s.blobPatternIds;
    if (body.contains("region_mode"))
    {
        try
        {
            request.regionMode = classifierRegionModeFromString(body["region_mode"].get<std::string>());
        }
        catch (const std::exception& error)
        {
            jsonReply(reply, errorJson(error.what()), 400);
            return;
        }
    }
    if (body.contains("roi") && body["roi"].is_array() && body["roi"].size() == 4)
    {
        for (size_t i = 0; i < 4; ++i)
            request.roi[i] = body["roi"][i].get<float>();
        try
        {
            validateClassifierRoi(request.roi);
        }
        catch (const std::exception& error)
        {
            jsonReply(reply, errorJson(error.what()), 400);
            return;
        }
        request.roiOverride = true;
    }
    if (body.contains("blob_pattern_ids") && body["blob_pattern_ids"].is_array())
        request.blobPatternIds = body["blob_pattern_ids"].get<std::vector<int>>();

    if (request.kind == "classifier")
    {
        if (!validClassName(request.className))
        {
            jsonReply(reply, errorJson("class is required"), 400);
            return;
        }
        std::error_code error;
        if (!fs::is_directory(classifierDir() / request.className, error))
        {
            jsonReply(reply, errorJson("unknown class: " + request.className), 404);
            return;
        }
        const std::string quota = quotaError(request.className, request.count);
        if (!quota.empty())
        {
            jsonReply(reply, errorJson(quota), 507);
            return;
        }
        if (request.regionMode == ClassifierRegionMode::Blob)
        {
            const ProcessingType type = tap_.lastMetadata().type;
            const bool blobRegions = type == ProcessingType::BlobDetection ||
                (type == ProcessingType::Classification && s.regionMode == ClassifierRegionMode::Blob);
            if (!blobRegions)
            {
                jsonReply(reply, errorJson("switch to blob_detection (or classification with "
                                           "region_mode=blob) before capturing blob regions"), 409);
                return;
            }
        }
    }
    else if (request.kind == "yolo")
    {
        const std::string quota = quotaError("", 0);
        if (!quota.empty())
        {
            jsonReply(reply, errorJson(quota), 507);
            return;
        }
    }
    else
    {
        jsonReply(reply, errorJson("kind must be classifier or yolo"), 400);
        return;
    }
    const std::string error = startCapture(request);
    if (!error.empty())
    {
        jsonReply(reply, errorJson(error), 409);
        return;
    }
    jsonReply(reply, {{"started", true}, {"count", request.count},
                      {"interval_ms", request.intervalMs},
                      {"region_mode", classifierRegionModeToString(request.regionMode)}});
}

void TrainingService::handleSamples(const std::string& query, HttpReply& reply)
{
    const std::string className = queryValue(query, "class");
    if (!validClassName(className))
    {
        jsonReply(reply, errorJson("invalid class"), 400);
        return;
    }
    const std::string offsetText = queryValue(query, "offset");
    const std::string limitText = queryValue(query, "limit");
    const size_t offset = offsetText.empty() ? 0 : std::stoul(offsetText);
    const size_t limit = std::clamp<size_t>(limitText.empty() ? 60 : std::stoul(limitText), 1, 500);
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto& file : fs::directory_iterator(classifierDir() / className, error))
        if (file.is_regular_file(error) && file.path().extension() == ".jpg")
            files.push_back(file.path());
    std::sort(files.begin(), files.end(), std::greater<fs::path>());   // новые первыми
    nlohmann::json items = nlohmann::json::array();
    for (size_t i = offset; i < files.size() && items.size() < limit; ++i)
        items.push_back({{"file", files[i].filename().string()},
                         {"bytes", fs::file_size(files[i], error)},
                         {"time_ms", fileTimeMs(files[i])}});
    jsonReply(reply, {{"class", className}, {"total", files.size()}, {"offset", offset},
                      {"items", items}});
}

void TrainingService::handleThumb(const std::string& query, HttpReply& reply)
{
    const std::string className = queryValue(query, "class");
    const std::string file = queryValue(query, "file");
    if (!validClassName(className) || !validFileName(file))
    {
        jsonReply(reply, errorJson("invalid class or file"), 400);
        return;
    }
    const fs::path path = classifierDir() / className / file;
    std::error_code error;
    if (!fs::is_regular_file(path, error))
    {
        jsonReply(reply, errorJson("not found"), 404);
        return;
    }
    reply.contentType = "image/jpeg";
    reply.filePath = path.string();
    reply.extraHeaders = "Cache-Control: max-age=3600\r\n";
}

void TrainingService::handleDeleteSamples(const nlohmann::json& body, HttpReply& reply)
{
    const std::string className = body.value("class", "");
    if (!validClassName(className) || !body.contains("files") || !body["files"].is_array())
    {
        jsonReply(reply, errorJson("class and files[] are required"), 400);
        return;
    }
    int deleted = 0;
    std::error_code error;
    for (const auto& item : body["files"])
    {
        if (!item.is_string() || !validFileName(item.get<std::string>()))
            continue;
        if (fs::remove(classifierDir() / className / item.get<std::string>(), error))
            ++deleted;
    }
    jsonReply(reply, {{"deleted", deleted}});
}

void TrainingService::handleTrain(const nlohmann::json& body, HttpReply& reply)
{
    const Settings s = settings();
    if (captureStatus().active)
    {
        jsonReply(reply, errorJson("capture is running"), 409);
        return;
    }
    const nlohmann::json summary = datasetSummary();
    int usable = 0;
    for (const auto& item : summary["classes"])
        if (item.value("count", 0) > 0)
            ++usable;
    if (usable < 2)
    {
        jsonReply(reply, errorJson("at least 2 classes with samples are required"), 400);
        return;
    }
    std::error_code error;
    if (s.classifierScript.empty() || !fs::exists(s.classifierScript, error))
    {
        jsonReply(reply, errorJson("training.classifier_script not found: " + s.classifierScript), 400);
        return;
    }
    fs::create_directories(s.modelsDir, error);
    const int epochs = std::clamp(body.value("epochs", s.classifierEpochs), 1, 1000);
    const int imageSize = std::clamp(body.value("image_size", s.classifierImageSize), 16, 512);
    const int threads = std::clamp(body.value("threads", 2), 1, 8);
    const std::vector<std::string> argv = {
        s.python, s.classifierScript, "train",
        "--data", classifierDir().string(), "--out", s.modelsDir.string(),
        "--epochs", std::to_string(epochs), "--image-size", std::to_string(imageSize),
        "--workers", "0", "--threads", std::to_string(threads),
        "--name", "simple_classifier", "--stamp", "--progress", "--export"};
    std::string startError;
    if (!trainer_.start(argv, {"PYTHONUNBUFFERED=1", "OMP_NUM_THREADS=" + std::to_string(threads)},
                        s.modelsDir.string(), startError))
    {
        jsonReply(reply, errorJson(startError), 409);
        return;
    }
    jsonReply(reply, trainer_.status());
}

nlohmann::json TrainingService::modelsJson() const
{
    const Settings s = settings();
    nlohmann::json models = nlohmann::json::array();
    std::error_code error;
    if (!fs::is_directory(s.modelsDir, error))
        return models;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(s.modelsDir, error))
        if (entry.is_regular_file(error) && entry.path().extension() == ".onnx")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end(), std::greater<fs::path>());
    const fs::path activeClassifier = s.activeModel.empty() ? fs::path()
        : (fs::path(s.activeModel).is_relative() ? configDir_ / s.activeModel : fs::path(s.activeModel));
    const fs::path activeYolo = s.yoloModel.empty() ? fs::path()
        : (fs::path(s.yoloModel).is_relative() ? configDir_ / s.yoloModel : fs::path(s.yoloModel));
    for (const fs::path& file : files)
    {
        nlohmann::json item = {{"file", file.filename().string()}, {"path", file.string()},
                               {"bytes", fs::file_size(file, error)},
                               {"created_ms", fileTimeMs(file)}, {"kind", "classifier"},
                               {"classes", nlohmann::json::array()}, {"input_size", nullptr}};
        const fs::path sidecar = file.string().substr(0, file.string().size() - 5) + ".classes.json";
        if (fs::exists(sidecar, error))
        {
            try
            {
                const nlohmann::json meta = nlohmann::json::parse(readFile(sidecar));
                if (meta.is_array())
                    item["classes"] = meta;
                else
                {
                    item["classes"] = meta.value("classes", nlohmann::json::array());
                    item["input_size"] = meta.value("input_size", nlohmann::json());
                    item["kind"] = meta.value("kind", "classifier");
                    item["best_val_acc"] = meta.value("best_val_acc", nlohmann::json());
                    item["samples"] = meta.value("samples", nlohmann::json());
                    if (meta.contains("region_mode"))
                        item["region_mode"] = meta["region_mode"];
                    if (meta.contains("crop_padding"))
                        item["crop_padding"] = meta["crop_padding"];
                }
            }
            catch (...) {}
        }
        const bool active = fs::equivalent(file, activeClassifier, error) ||
                            fs::equivalent(file, activeYolo, error);
        item["active"] = active;
        models.push_back(item);
    }
    return models;
}

void TrainingService::handleModels(HttpReply& reply)
{
    jsonReply(reply, modelsJson());
}

void TrainingService::handleActivate(const nlohmann::json& body, HttpReply& reply)
{
    const Settings s = settings();
    const std::string model = body.value("model", "");
    if (!validFileName(model) || model.size() < 6 || model.substr(model.size() - 5) != ".onnx")
    {
        jsonReply(reply, errorJson("model must be a .onnx file name from models_dir"), 400);
        return;
    }
    const fs::path path = s.modelsDir / model;
    std::error_code error;
    if (!fs::is_regular_file(path, error))
    {
        jsonReply(reply, errorJson("model not found: " + model), 404);
        return;
    }
    nlohmann::json meta = nlohmann::json::object();
    const fs::path sidecar = path.string().substr(0, path.string().size() - 5) + ".classes.json";
    if (fs::exists(sidecar, error))
    {
        try
        {
            meta = nlohmann::json::parse(readFile(sidecar));
            if (meta.is_array())
                meta = {{"classes", meta}};
        }
        catch (...) {}
    }
    std::string kind = body.value("kind", meta.value("kind", "classifier"));
    std::vector<std::string> classes = meta.value("classes", std::vector<std::string>{});
    if (body.contains("class_names") && body["class_names"].is_array())
        classes = body["class_names"].get<std::vector<std::string>>();
    const std::string relativePath = relativeToDir(path, configDir_);
    std::string regionMode = body.value("region_mode", "");
    if (!regionMode.empty())
    {
        try
        {
            classifierRegionModeFromString(regionMode);
        }
        catch (const std::exception& e)
        {
            jsonReply(reply, errorJson(e.what()), 400);
            return;
        }
    }
    const std::string error_text = mutateConfig_([&](nlohmann::json& config) {
        if (kind == "yolo")
        {
            nlohmann::json& section = config["object_detection"];
            section["model_onnx"] = relativePath;
            section["class_names"] = classes;
            section["class_names_file"] = "";
            const int size = meta.value("input_size", body.value("input_size", 640));
            section["input_width"] = size;
            section["input_height"] = size;
            config["general_params"]["processing_mode"] = "object_detection";
        }
        else
        {
            nlohmann::json& section = config["classification"];
            section["model_onnx"] = relativePath;
            section["class_names"] = classes;
            section["class_names_file"] = "";
            section["input_size"] = meta.value("input_size", body.value("input_size", 64));
            if (!regionMode.empty())
                section["region_mode"] = regionMode;
            config["general_params"]["processing_mode"] = "classification";
        }
    });
    if (!error_text.empty())
    {
        jsonReply(reply, errorJson(error_text), 500);
        return;
    }
    jsonReply(reply, {{"activated", model}, {"kind", kind}, {"classes", classes},
                      {"model_onnx", relativePath}});
}

void TrainingService::handleExport(const std::string& query, HttpReply& reply)
{
    const Settings s = settings();
    const std::string kind = queryValue(query, "kind").empty() ? "classifier" : queryValue(query, "kind");
    std::error_code error;
    const fs::path exportDir = s.datasetDir / "export";
    fs::create_directories(exportDir, error);
    const fs::path zipPath = exportDir / (kind + "_dataset_" + timeStamp() + ".zip");
    ZipWriter zip(zipPath.string());
    if (!zip.ok())
    {
        jsonReply(reply, errorJson("cannot create export file"), 500);
        return;
    }
    size_t files = 0;
    if (kind == "classifier")
    {
        const fs::path dir = classifierDir();
        if (fs::is_directory(dir, error))
            for (const auto& entry : fs::recursive_directory_iterator(dir, error))
                if (entry.is_regular_file(error))
                {
                    zip.addFile("classifier/" + relativeToDir(entry.path(), dir), entry.path().string());
                    ++files;
                }
        zip.addData("classifier/README.txt",
                    "ImageFolder dataset from SeeSharp.\n"
                    "Train on a PC:\n"
                    "  python3 common/nn/simple_classifier.py train --data classifier --out models "
                    "--epochs 20 --image-size " + std::to_string(s.classifierImageSize) +
                    " --export --stamp\n"
                    "Then upload models/simple_classifier_<ts>.onnx through the Training tab.\n");
    }
    else if (kind == "yolo")
    {
        const fs::path dir = yoloDir();
        nlohmann::json classes = nlohmann::json::array();
        if (fs::exists(dir / "classes.json", error))
        {
            try { classes = nlohmann::json::parse(readFile(dir / "classes.json")); }
            catch (...) {}
        }
        if (fs::is_directory(dir, error))
            for (const auto& entry : fs::recursive_directory_iterator(dir, error))
                if (entry.is_regular_file(error))
                {
                    zip.addFile("yolo/" + relativeToDir(entry.path(), dir), entry.path().string());
                    ++files;
                }
        std::string yaml = "path: .\ntrain: images\nval: images\nnames:\n";
        int index = 0;
        for (const auto& name : classes)
            yaml += "  " + std::to_string(index++) + ": " + name.get<std::string>() + "\n";
        zip.addData("yolo/dataset.yaml", yaml);
        zip.addData("yolo/README.txt",
                    "YOLO dataset from SeeSharp (images/*.jpg + labels/*.txt, normalized "
                    "class cx cy w h).\nTrain on a PC with a GPU:\n"
                    "  python3 board/raspberry-cm5/train_yolo.py --data yolo/dataset.yaml "
                    "--epochs 50 --imgsz 640 --out custom_yolo.onnx\n"
                    "Then upload custom_yolo.onnx through the Training tab (kind: yolo).\n");
    }
    else
    {
        jsonReply(reply, errorJson("kind must be classifier or yolo"), 400);
        return;
    }
    if (!zip.finish())
    {
        jsonReply(reply, errorJson("export failed"), 500);
        return;
    }
    reply.contentType = "application/zip";
    reply.filePath = zipPath.string();
    reply.extraHeaders = "Content-Disposition: attachment; filename=\"" +
                         zipPath.filename().string() + "\"\r\nX-Files: " +
                         std::to_string(files) + "\r\n";
    reply.deleteFileAfterSend = true;
}

std::vector<std::string> TrainingService::classNamesForType(ProcessingType type)
{
    const Settings s = settings();
    if (type == ProcessingType::Classification)
        return s.classNames;
    if (type != ProcessingType::ObjectDetection)
        return {};
    if (!s.yoloClassNames.empty())
        return s.yoloClassNames;
    std::lock_guard<std::mutex> lock(namesMutex_);
    if (s.yoloClassNamesFile != yoloNamesPath_)
    {
        yoloNamesPath_ = s.yoloClassNamesFile;
        yoloNamesCache_ = yoloNamesPath_.empty() ? std::vector<std::string>{}
                                                 : readNamesFile(yoloNamesPath_);
    }
    return yoloNamesCache_;
}

void TrainingService::handleMetadataLast(HttpReply& reply)
{
    const MetadataSnapshot last = tap_.lastMetadata();
    const std::vector<std::string> names = classNamesForType(last.type);
    nlohmann::json objects = nlohmann::json::array();
    for (const BlobMetaData& object : last.metadata)
    {
        nlohmann::json item = {
            {"id", object.id}, {"confidence", object.confidence},
            {"center", {object.center.x, object.center.y}},
            {"bbox", {object.boundingBox.x, object.boundingBox.y,
                      object.boundingBox.width, object.boundingBox.height}}};
        if (object.id >= 0 && object.id < static_cast<int>(names.size()))
            item["class_name"] = names[static_cast<size_t>(object.id)];
        objects.push_back(item);
        if (objects.size() >= 64)
            break;
    }
    jsonReply(reply, {{"detector", processingTypeName(last.type)}, {"frame_id", last.frameId},
                      {"sequence", last.sequence}, {"time_ms", last.wallTimeMs},
                      {"width", last.width}, {"height", last.height}, {"fps", last.fps},
                      {"objects", objects}});
}

// ------------------------------------------------------------------ capture

TrainingService::CaptureStatus TrainingService::captureStatus() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return capture_;
}

std::string TrainingService::startCapture(const CaptureRequest& request)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capture_.active)
            return "capture already running";
        capture_ = CaptureStatus{};
        capture_.active = true;
        capture_.kind = request.kind;
        capture_.className = request.className;
        capture_.total = request.count;
        capture_.startedMs = nowMs();
    }
    if (captureThread_.joinable())
        captureThread_.join();
    captureStop_ = false;
    captureThread_ = std::thread(&TrainingService::captureLoop, this, request);
    return "";
}

void TrainingService::stopCapture()
{
    captureStop_ = true;
    if (captureThread_.joinable())
        captureThread_.join();
}

void TrainingService::captureLoop(CaptureRequest request)
{
    const Settings s = settings();
    tap_.arm();
    struct Disarm
    {
        FrameTap& tap;
        ~Disarm() { tap.disarm(); }
    } disarm{tap_};

    const fs::path targetDir = request.kind == "yolo" ? yoloDir() / "images"
                                                      : classifierDir() / request.className;
    std::error_code error;
    fs::create_directories(targetDir, error);
    if (request.kind == "yolo")
        fs::create_directories(yoloDir() / "labels", error);
    const std::vector<int> encodeParams{cv::IMWRITE_JPEG_QUALITY,
                                        request.kind == "yolo" ? 85 : s.jpegQuality};
    uint64_t lastSequence = tap_.lastMetadata().sequence;
    std::string failure;
    int saved = 0;
    for (int shot = 0; shot < request.count && !captureStop_; ++shot)
    {
        FrameSnapshot snapshot;
        if (!tap_.waitNext(lastSequence, std::chrono::milliseconds(3000), snapshot))
        {
            failure = "no frames from the pipeline (is the camera running?)";
            break;
        }
        lastSequence = snapshot.sequence;
        const cv::Size size = snapshot.frame.size();
        std::vector<std::pair<cv::Rect, int>> regions;   // rect + blob id (-1)
        if (request.kind == "yolo")
            regions.emplace_back(cv::Rect(0, 0, size.width, size.height), -1);
        else if (request.regionMode == ClassifierRegionMode::Whole)
            regions.emplace_back(cv::Rect(0, 0, size.width, size.height), -1);
        else if (request.regionMode == ClassifierRegionMode::Roi)
            regions.emplace_back(roiToPixels(request.roiOverride ? request.roi : s.roi, size), -1);
        else
        {
            for (const BlobMetaData& blob : snapshot.metadata)
            {
                if (snapshot.type == ProcessingType::BlobDetection)
                {
                    if (!request.blobPatternIds.empty() &&
                        std::find(request.blobPatternIds.begin(), request.blobPatternIds.end(),
                                  blob.id) == request.blobPatternIds.end())
                        continue;
                    regions.emplace_back(padRegion(blob.boundingBox, s.cropPadding, size), blob.id);
                }
                else if (snapshot.type == ProcessingType::Classification)
                    regions.emplace_back(blob.boundingBox, blob.id);   // уже с отступом
            }
            std::stable_sort(regions.begin(), regions.end(),
                             [](const auto& a, const auto& b) { return a.first.area() > b.first.area(); });
            if (static_cast<int>(regions.size()) > s.maxRegions)
                regions.resize(static_cast<size_t>(s.maxRegions));
        }

        for (const auto& [rect, blobId] : regions)
        {
            cv::Mat crop = cropRegion(snapshot.frame, rect);
            if (crop.empty() || crop.cols < 4 || crop.rows < 4)
                continue;
            if (request.kind == "yolo" && crop.cols > 1280)
            {
                const double scale = 1280.0 / crop.cols;
                cv::resize(crop, crop, cv::Size(), scale, scale, cv::INTER_AREA);
            }
            std::vector<uchar> encoded;
            if (!cv::imencode(".jpg", crop, encoded, encodeParams))
                continue;
            const std::string name = sampleName(++sampleSequence_);
            const fs::path path = targetDir / name;
            if (!writeFileAtomic(path, std::string(encoded.begin(), encoded.end())))
            {
                failure = "cannot write " + path.string();
                break;
            }
            appendIndex({{"kind", request.kind}, {"file", name}, {"class", request.className},
                         {"time_ms", nowMs()}, {"frame_id", snapshot.frameId},
                         {"region_mode", request.kind == "yolo" ? "whole"
                                         : classifierRegionModeToString(request.regionMode)},
                         {"src_rect", {rect.x, rect.y, rect.width, rect.height}},
                         {"frame", {size.width, size.height}}, {"blob_id", blobId},
                         {"crop_padding", s.cropPadding},
                         {"saved_size", {crop.cols, crop.rows}}});
            ++saved;
            std::lock_guard<std::mutex> lock(mutex_);
            capture_.saved = saved;
            capture_.lastFile = name;
        }
        if (!failure.empty())
            break;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            capture_.done = shot + 1;
        }
        if (shot + 1 < request.count && request.intervalMs > 0)
        {
            for (int waited = 0; waited < request.intervalMs && !captureStop_; waited += 20)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    capture_.active = false;
    capture_.error = failure;
}

// ------------------------------------------------------------------- upload

void TrainingService::receiveUpload(platform::socket_t socket, const std::string& headers,
                                    const std::string& prefix, long contentLength,
                                    HttpReply& reply)
{
    const Settings s = settings();
    const std::string rawName = headerValue(headers, "X-File-Name");
    std::string name = rawName;
    for (char& c : name)
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
            c = '_';
    if (name.empty() || name[0] == '.' || name.size() < 6 ||
        name.substr(name.size() - 5) != ".onnx" || name.find("..") != std::string::npos)
    {
        jsonReply(reply, errorJson("X-File-Name must be a .onnx file name"), 400);
        return;
    }
    const long maxBytes = static_cast<long>(s.maxUploadMb) * 1024 * 1024;
    if (contentLength <= 0 || contentLength > maxBytes)
    {
        jsonReply(reply, errorJson("Content-Length must be 1.." + std::to_string(maxBytes) + " bytes"), 413);
        return;
    }
    std::error_code error;
    fs::create_directories(s.modelsDir, error);
    const fs::path target = s.modelsDir / name;
    const fs::path part = target.string() + ".part";
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        jsonReply(reply, errorJson("cannot write to models_dir"), 500);
        return;
    }
    platform::socketSetReceiveTimeout(socket, 10000);

    long received = static_cast<long>(std::min<size_t>(prefix.size(), static_cast<size_t>(contentLength)));
    out.write(prefix.data(), received);
    std::vector<char> buffer(64 * 1024);
    while (received < contentLength && out)
    {
        const long got = platform::socketRecv(socket, buffer.data(),
                                 std::min<size_t>(buffer.size(), static_cast<size_t>(contentLength - received)));
        if (got < 0 && platform::socketInterrupted(platform::socketError()))
            continue;
        if (got <= 0)
            break;
        out.write(buffer.data(), got);
        received += got;
    }
    out.close();
    if (received != contentLength)
    {
        fs::remove(part, error);
        jsonReply(reply, errorJson("upload incomplete: " + std::to_string(received) + " of " +
                                   std::to_string(contentLength) + " bytes"), 400);
        return;
    }
    fs::rename(part, target, error);
    if (error)
    {
        jsonReply(reply, errorJson("rename failed: " + error.message()), 500);
        return;
    }
    const std::string kind = headerValue(headers, "X-Model-Kind").empty() ? "classifier"
                                                                            : headerValue(headers, "X-Model-Kind");
    nlohmann::json sidecar = {{"kind", kind}, {"created_ms", nowMs()}, {"uploaded", true},
                              {"source_name", rawName}};
    const std::string classNames = headerValue(headers, "X-Class-Names");
    if (!classNames.empty())
    {
        try
        {
            sidecar["classes"] = nlohmann::json::parse(urlDecode(classNames));
        }
        catch (...)
        {
            sidecar["classes"] = nlohmann::json::array();
        }
    }
    const std::string inputSize = headerValue(headers, "X-Input-Size");
    if (!inputSize.empty())
        sidecar["input_size"] = std::stoi(inputSize);
    writeFileAtomic(target.string().substr(0, target.string().size() - 5) + ".classes.json",
                    sidecar.dump(1));
    jsonReply(reply, {{"saved", name}, {"bytes", received}, {"kind", kind}});
}

// ------------------------------------------------------------------ yolo (этап 2)

void TrainingService::handleYoloSamples(const std::string& query, HttpReply& reply)
{
    const std::string offsetText = queryValue(query, "offset");
    const std::string limitText = queryValue(query, "limit");
    const size_t offset = offsetText.empty() ? 0 : std::stoul(offsetText);
    const size_t limit = std::clamp<size_t>(limitText.empty() ? 60 : std::stoul(limitText), 1, 500);
    std::vector<fs::path> files;
    std::error_code error;
    const fs::path images = yoloDir() / "images";
    for (const auto& file : fs::directory_iterator(images, error))
        if (file.is_regular_file(error) && file.path().extension() == ".jpg")
            files.push_back(file.path());
    std::sort(files.begin(), files.end(), std::greater<fs::path>());
    nlohmann::json items = nlohmann::json::array();
    for (size_t i = offset; i < files.size() && items.size() < limit; ++i)
    {
        const fs::path label = yoloDir() / "labels" / (files[i].stem().string() + ".txt");
        int boxes = 0;
        if (fs::exists(label, error))
        {
            std::ifstream in(label);
            std::string line;
            while (std::getline(in, line))
                if (!line.empty()) ++boxes;
        }
        items.push_back({{"file", files[i].filename().string()},
                         {"bytes", fs::file_size(files[i], error)},
                         {"time_ms", fileTimeMs(files[i])}, {"labeled", fs::exists(label, error)},
                         {"boxes", boxes}});
    }
    jsonReply(reply, {{"total", files.size()}, {"offset", offset}, {"items", items}});
}

void TrainingService::handleYoloImage(const std::string& query, HttpReply& reply)
{
    const std::string file = queryValue(query, "file");
    if (!validFileName(file))
    {
        jsonReply(reply, errorJson("invalid file"), 400);
        return;
    }
    const fs::path path = yoloDir() / "images" / file;
    std::error_code error;
    if (!fs::is_regular_file(path, error))
    {
        jsonReply(reply, errorJson("not found"), 404);
        return;
    }
    reply.contentType = "image/jpeg";
    reply.filePath = path.string();
    reply.extraHeaders = "Cache-Control: max-age=3600\r\n";
}

void TrainingService::handleYoloLabels(const std::string& query, HttpReply& reply)
{
    const std::string file = queryValue(query, "file");
    if (!validFileName(file))
    {
        jsonReply(reply, errorJson("invalid file"), 400);
        return;
    }
    const fs::path label = yoloDir() / "labels" / (fs::path(file).stem().string() + ".txt");
    nlohmann::json boxes = nlohmann::json::array();
    std::ifstream in(label);
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream parts(line);
        int classId;
        double cx, cy, w, h;
        if (parts >> classId >> cx >> cy >> w >> h)
            boxes.push_back({{"class_id", classId}, {"cx", cx}, {"cy", cy}, {"w", w}, {"h", h}});
    }
    jsonReply(reply, {{"file", file}, {"boxes", boxes}});
}

void TrainingService::handleYoloSaveLabels(const nlohmann::json& body, HttpReply& reply)
{
    const std::string file = body.value("file", "");
    if (!validFileName(file) || !body.contains("boxes") || !body["boxes"].is_array())
    {
        jsonReply(reply, errorJson("file and boxes[] are required"), 400);
        return;
    }
    std::error_code error;
    if (!fs::is_regular_file(yoloDir() / "images" / file, error))
    {
        jsonReply(reply, errorJson("image not found"), 404);
        return;
    }
    fs::create_directories(yoloDir() / "labels", error);
    std::string text;
    char line[128];
    for (const auto& box : body["boxes"])
    {
        const double cx = std::clamp(box.value("cx", 0.0), 0.0, 1.0);
        const double cy = std::clamp(box.value("cy", 0.0), 0.0, 1.0);
        const double w = std::clamp(box.value("w", 0.0), 0.0, 1.0);
        const double h = std::clamp(box.value("h", 0.0), 0.0, 1.0);
        if (w <= 0 || h <= 0)
            continue;
        snprintf(line, sizeof(line), "%d %.6f %.6f %.6f %.6f\n",
                 std::max(0, box.value("class_id", 0)), cx, cy, w, h);
        text += line;
    }
    const fs::path label = yoloDir() / "labels" / (fs::path(file).stem().string() + ".txt");
    if (!writeFileAtomic(label, text))
    {
        jsonReply(reply, errorJson("cannot write label file"), 500);
        return;
    }
    jsonReply(reply, {{"file", file}, {"boxes", body["boxes"].size()}});
}

void TrainingService::handleYoloClasses(const nlohmann::json& body, HttpReply& reply)
{
    if (!body.contains("classes") || !body["classes"].is_array())
    {
        jsonReply(reply, errorJson("classes[] is required"), 400);
        return;
    }
    for (const auto& name : body["classes"])
        if (!name.is_string() || name.get<std::string>().empty() || name.get<std::string>().size() > 64)
        {
            jsonReply(reply, errorJson("class names must be non-empty strings"), 400);
            return;
        }
    std::error_code error;
    fs::create_directories(yoloDir(), error);
    writeFileAtomic(yoloDir() / "classes.json", body["classes"].dump(1));
    jsonReply(reply, {{"classes", body["classes"]}});
}

void TrainingService::handleYoloDelete(const nlohmann::json& body, HttpReply& reply)
{
    if (!body.contains("files") || !body["files"].is_array())
    {
        jsonReply(reply, errorJson("files[] is required"), 400);
        return;
    }
    int deleted = 0;
    std::error_code error;
    for (const auto& item : body["files"])
    {
        if (!item.is_string() || !validFileName(item.get<std::string>()))
            continue;
        const std::string file = item.get<std::string>();
        if (fs::remove(yoloDir() / "images" / file, error))
            ++deleted;
        fs::remove(yoloDir() / "labels" / (fs::path(file).stem().string() + ".txt"), error);
    }
    jsonReply(reply, {{"deleted", deleted}});
}
