#pragma once

struct LineParams
{
    double cannyThreshold1;
    double cannyThreshold2;
    int apertureSize;
    bool useL2Gradient;

    double rho;            // distance resolution in pixels
    double theta;          // angle resolution in radians
    int threshold;         // minimum number of votes
    double minLineLength;  // minimum line length
    double maxLineGap;     // maximum allowed gap between points on the same line
    double minAngle = -180.0;
    double maxAngle = 180.0;
    double roiX = 0.0;
    double roiY = 0.0;
    double roiWidth = 1.0;
    double roiHeight = 1.0;
    int maxLines = 100;
};
