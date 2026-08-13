#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include "model/blob_detection/node_candidate.hpp"
#include "model/blob_detection/link_candidate.hpp"

// TODO (DD): We can inherit from DetectedBlob
struct DetectedMulticolorBlob {
    int patternId;
    double similarity;
    std::vector<cv::Point> contour;
    std::vector<NodeCandidate> nodeCandidates;
    std::vector<LinkCandidate> linkCandidates;
};
