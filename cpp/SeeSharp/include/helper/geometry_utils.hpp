#pragma once
#include <opencv2/core.hpp>
#include <cmath>

namespace GeometryUtils
{
    inline float distance(const cv::Point2f& a, const cv::Point2f& b)
    {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    inline float angleRad(const cv::Point2f& a, const cv::Point2f& b)
    {
        return std::atan2(b.y - a.y, b.x - a.x);
    }

    inline float angleDeg(const cv::Point2f& a, const cv::Point2f& b)
    {
        return angleRad(a, b) * 180.0f / static_cast<float>(CV_PI);
    }

    // Normalize the angle into [0, 180)
    inline float normalizeAngle(const cv::Point2f& a, const cv::Point2f& b)
    {
        float angleDegrees = angleDeg(a, b);
        if (angleDegrees < 0)
        {
            angleDegrees += 180.0;
        }
        return angleDegrees;
    }
}
