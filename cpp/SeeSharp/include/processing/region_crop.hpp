#pragma once
// Единая вырезка области кадра для классификатора: используется и инференсом
// (ClassifierProcessor), и сбором датасета (TrainingService). Любое изменение
// здесь меняет и обучающие crop'ы, и то, что видит модель — только вместе.

#include <algorithm>
#include <array>
#include <cmath>

#include <opencv2/core.hpp>

/// Прямоугольник в долях кадра [x, y, w, h] -> пиксели, обрезка по кадру,
/// минимум minSide x minSide.
inline cv::Rect roiToPixels(const std::array<float, 4>& roi, const cv::Size& frame,
                            int minSide = 8)
{
    int x = static_cast<int>(std::lround(roi[0] * frame.width));
    int y = static_cast<int>(std::lround(roi[1] * frame.height));
    int w = static_cast<int>(std::lround(roi[2] * frame.width));
    int h = static_cast<int>(std::lround(roi[3] * frame.height));
    x = std::clamp(x, 0, std::max(0, frame.width - 1));
    y = std::clamp(y, 0, std::max(0, frame.height - 1));
    w = std::clamp(std::max(w, minSide), 1, frame.width - x);
    h = std::clamp(std::max(h, minSide), 1, frame.height - y);
    return {x, y, w, h};
}

/// Рамка + отступ padding (доля размера рамки с каждой стороны), обрезка по кадру.
inline cv::Rect padRegion(const cv::Rect& box, double padding, const cv::Size& frame)
{
    const int padX = static_cast<int>(std::lround(box.width * padding));
    const int padY = static_cast<int>(std::lround(box.height * padding));
    cv::Rect padded(box.x - padX, box.y - padY, box.width + 2 * padX, box.height + 2 * padY);
    padded &= cv::Rect(0, 0, frame.width, frame.height);
    return padded;
}

/// Вырезка области (копия, чтобы дальнейшие resize/convert не трогали кадр).
inline cv::Mat cropRegion(const cv::Mat& frame, const cv::Rect& region)
{
    const cv::Rect clipped = region & cv::Rect(0, 0, frame.cols, frame.rows);
    if (clipped.width <= 0 || clipped.height <= 0)
        return {};
    return frame(clipped).clone();
}
