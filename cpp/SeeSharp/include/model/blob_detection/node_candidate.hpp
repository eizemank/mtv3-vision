#pragma once

#include "model/blob_detection/detected_blob.hpp"

class NodeCandidate
{
    public:

    // Default constructor
    NodeCandidate()
    {
        nodeId = -1;
        detectedBlob = DetectedBlob();
        score = 0.0;
        size = 0.0;
        angle = 0.0;
    }

    NodeCandidate(int nodeId, DetectedBlob detectedBlob)
        : nodeId(nodeId), detectedBlob(detectedBlob), score(0.0), size(0.0), angle(0.0) {}

    // node id for that we are looking for the candidate
    int nodeId;
    // actual blob
    DetectedBlob detectedBlob;
    // similarity score
    double score;

    // node size
    double size;

    // angle of the blob in degrees
    double angle;
};