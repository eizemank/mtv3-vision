# Naming Conventions for Computer Vision Project

## File Names
- Use `snake_case` for file names:
  - `config_reader.cpp`
  - `blob_detector.hpp`
  - `video_processor.cpp`

## Class Names
- Use `CamelCase` for class names:
  ```cpp
  class ConfigReader;
  class BlobDetector;
  ```

## Method and Function Names
- Use `camelCase`:
  ```cpp
  void loadConfig();
  Frame processFrame(const cv::Mat&);
  ```

- Private methods may optionally use underscore at the end:
  ```cpp
  void detectBlobs_();
  ```

## Variable Names
- Local variables and parameters: `camelCase`
  ```cpp
  int threshold;
  std::string configPath;
  ```

- Private member variables: `camelCase_`
  ```cpp
  class BlobDetector {
    int threshold_;
    cv::Mat resultFrame_;
  };
  ```

## Constants and Macros
- Use `ALL_CAPS_WITH_UNDERSCORES`
  ```cpp
  #define DEFAULT_CONFIG_PATH "./config.json"
  const int MAX_BLOB_COUNT = 100;
  ```

## Namespaces
- Use `snake_case`
  ```cpp
  namespace blob_detection {
      // ...
  }
  ```
