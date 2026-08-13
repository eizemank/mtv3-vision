#pragma once

#include "model/common_config.hpp"

struct GeneralParams
{
    bool debugMode;
    ProcessingType processingType;
    int cameraRotation = 0;
    double exposureEv = 0.0;
    double contrast = 1.0;
    double brightness = 0.0;
    double whiteBalanceBlue = 1.0;
    double whiteBalanceGreen = 1.0;
    double whiteBalanceRed = 1.0;
};
