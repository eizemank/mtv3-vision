#include "model/blob_detection/detected_blob.hpp"
#include <cmath>

DetectedBlob::DetectedBlob(const std::vector<cv::Point>& contour, int colorPatternId)
    : colorPatternId(colorPatternId), contour(std::move(contour))
{
    // Calculate properties
    area = cv::contourArea(contour);
    boundingBox = cv::boundingRect(contour);
    cv::convexHull(contour, this->hullContour);
    hullArea = cv::contourArea(this->hullContour);
    maxAxis = getMaxAxis();

    // Center
    cv::Moments mu = cv::moments(contour);
    center = cv::Point2f(mu.m10 / mu.m00, mu.m01 / mu.m00);

    // Circularity
    double perimeter = arcLength(contour, true);
    circularity = (4 * CV_PI * area) / (perimeter * perimeter);

    inertia = calculateInertia();
    convexity = calculateConvexity();
}

double DetectedBlob::calculateInertia() const
{
    if (contour.size() < 5) return 0.0; // Not enough points to fit an ellipse
    cv::RotatedRect ellipse = cv::fitEllipse(contour);
    double majorAxis = cv::max(ellipse.size.width, ellipse.size.height);
    double minorAxis = cv::min(ellipse.size.width, ellipse.size.height);
    return minorAxis / majorAxis;
}

double DetectedBlob::calculateConvexity() const
{
    return area / (hullArea + 1e-5);
}

MaxAxis DetectedBlob::getMaxAxis() const
{
    double maxDistance = 0.0;
    cv::Point2f start, end;
    for (size_t i = 0; i < hullContour.size(); ++i)
    {
        for (size_t j = i + 1; j < hullContour.size(); ++j)
        {
            double dist = cv::norm(hullContour[i] - hullContour[j]);
            if (dist > maxDistance)
            {
                maxDistance = dist;
                start = hullContour[i];
                end = hullContour[j];
            }
        }
    }

    MaxAxis maxAxis(start, end);
    return maxAxis;
}