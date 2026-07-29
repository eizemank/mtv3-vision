#include "model/max_axis.hpp"
#include "helper/geometry_utils.hpp"

MaxAxis::MaxAxis(const cv::Point2f& start, const cv::Point2f& end)
        : start(start), end(end)
{
    length = GeometryUtils::distance(start, end);
    angle = GeometryUtils::normalizeAngle(start, end);
}