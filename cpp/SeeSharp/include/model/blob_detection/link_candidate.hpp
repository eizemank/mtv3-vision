#pragma once
#include <opencv2/core.hpp>
#include "helper/geometry_utils.hpp"

class LinkCandidate
{
public:
    // Id contains node ids that are connected by this link
    // they are separated by dash
    const char* id;

    /// @brief Similarity score of the link
    double score;

    // Length of the link
    double length;

    // Base link: length / baseNodeSize, other: length / baseLinkLength
    double relativeLength;

    // Angle to Ox axis
    double angle;

    // Base link: angle to x axis - angle of the base node, other: angle to x axis - angle of the base link
    double relativeAngle;

    // Default constructor
    LinkCandidate() : id(""), score(0.0), length(0.0), relativeLength(0.0), angle(0.0), relativeAngle(0.0) {}

    // Static factory method to create a LinkCandidate
    static LinkCandidate CreateBaseLink(const char* id, cv::Point2f p1, cv::Point2f p2, double baseNodeSize, double baseNodeAngle)
    {
        LinkCandidate link(id, p1, p2);
        link.relativeLength = link.length / baseNodeSize;
        link.relativeAngle = link.angle - baseNodeAngle;
        return link;
    }

    static LinkCandidate CreateRegularLink(const char* id, cv::Point2f p1, cv::Point2f p2, double baseLinkLength, double baseLinkAngle)
    {
        LinkCandidate link(id, p1, p2);
        link.relativeLength = link.length / baseLinkLength;
        link.relativeAngle = link.angle - baseLinkAngle;
        return link;
    }

private:
    LinkCandidate(const char* id, cv::Point2f p1, cv::Point2f p2)
        : id(id), point1_(p1), point2_(p2), score(0.0)
    {
        length = GeometryUtils::distance(point1_, point2_);
        angle = GeometryUtils::normalizeAngle(point1_, point2_);
    }

    cv::Point2f point1_;
    cv::Point2f point2_;
};