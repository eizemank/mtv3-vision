#pragma once

#include <opencv2/opencv.hpp>
#include <cmath>
#include "model/blob_detection/color_model.hpp"

/**
 * @brief Contains classes and structures that is used in configureation file for blob detection.
 */

struct OneColorBlobParams
{
    int id;
    bool enabled = true;
    double minArea;
    double maxArea;
    int minWidth;
    int minHeight;
    blob_color::Model colorModel = blob_color::Model::YCrCb;
    cv::Scalar lowerRange;
    cv::Scalar upperRange;
    double minCircularity;
    double maxCircularity;
    double minInertia;
    double maxInertia;
    double minConvexity;
    double maxConvexity;
    int minVertices = 0;
    int maxVertices = 0;
    double polygonApproximation = 0.02;
};

/// @brief Enum to define the base measure of size for the blob
enum class SizeMeasure
{
    MaxAxis,
    Width,
    Height,
    Area,
    ConvexArea // TODO (DD): What does it mean? Area of the convex hull, of minAreaRect, or of boundingRect?
};

inline SizeMeasure sizeMeasureFromString(const std::string& str)
{
    if (str == "max_axis") return SizeMeasure::MaxAxis;
    if (str == "width") return SizeMeasure::Width;
    if (str == "height") return SizeMeasure::Height;
    if (str == "area") return SizeMeasure::Area;
    if (str == "convex_area") return SizeMeasure::ConvexArea;
    throw std::invalid_argument("Unknown SizeMeasure: " + str);
}

inline std::string sizeMeasureToString(SizeMeasure measure)
{
    switch (measure)
    {
        case SizeMeasure::MaxAxis: return "max_axis";
        case SizeMeasure::Width: return "width";
        case SizeMeasure::Height: return "height";
        case SizeMeasure::Area: return "area";
        case SizeMeasure::ConvexArea: return "convex_area";
        default: throw std::invalid_argument("Unknown SizeMeasure enum value");
    }
}

struct CriterionParams
{
    double min;
    double max;
    double goal;
    int weight;

    double getWeightedScore(double value) const
    {
        if (value < min || value > max)
            return 0.0;

        double score = 1 - (std::fabs(value - goal) / std::fabs(max - min));

        return score * weight;
    }

    bool isInRange(double value) const
    {
        return (value >= min && value <= max);
    }
};

/// @brief Struct to hold the parameters for a node in a multi-color blob
/// @details IMPORTANT: Base node must have id = 0
struct NodeSettings
{
    int id;
    // To define color patterns
    std::vector<int> blobColorPatternIds;
    double threshold;
    int weight;
    CriterionParams size;
    CriterionParams circularity;
    CriterionParams inertia;
    CriterionParams convexity;
    CriterionParams angle;

    bool isBaseNode() const
    {
        return (id == 0);
    }

    double getWeightedScore(double value) const
    {
        return value * weight;
    }
};

struct LinkSettings
{
    // Contains the ids of the nodes that are connected
    std::string id;
    double threshold;
    int weight;
    CriterionParams lengthAbsolute;
    CriterionParams lengthRelative;
    CriterionParams angleAbsolute;
    CriterionParams angleRelative;

    bool isBaseLink() const
    {
        return (id[0] == '0' && id[2] == '1');
    }

    double getWeightedScore(double value) const
    {
        return value * weight;
    }

    size_t getFirstNodeId() const
    {
        size_t pos = id.find('-');
        if (pos == std::string::npos)
            throw std::invalid_argument("Invalid link id format: " + id);

        return std::stoi(id.substr(0, pos));
    }

    size_t getSecondNodeId() const
    {
        size_t pos = id.find('-');
        if (pos == std::string::npos)
            throw std::invalid_argument("Invalid link id format: " + id);
        return std::stoi(id.substr(pos + 1));
    }
};

struct MultiColorBlobParams
{
    int id;
    double overallThreshold;
    SizeMeasure sizeMeasure;
    std::vector<NodeSettings> nodes;
    std::vector<LinkSettings> links;
};

/// @brief Struct to hold all parameters for blob detection
/// @details Contains two vectors of parameters: one for one-color blobs and another for multi-color blobs
/// @note The parameters are read from a JSON configuration file
struct BlobParams
{
    bool enableOneColorDetection;
    bool enableMultiColorDetection;
    int maxCompositeObjects = 5;

    std::vector<OneColorBlobParams> oneColorBlobParams;
    std::vector<MultiColorBlobParams> multiColorBlobParams;
};
