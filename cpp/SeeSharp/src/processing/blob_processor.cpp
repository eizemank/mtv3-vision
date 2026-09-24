#include "processing/blob_processor.hpp"
#include "processing/blob_color_mask.hpp"

#include <algorithm>
#include <map>

// -*-*-*-*- "It's a battle between code readability and performance" -*-*-*-*-

constexpr const int MAX_CRITERIA_WEIGHT = 255;

// Default constructor
BlobProcessor::BlobProcessor(const BlobParams& params) : params_(params) {}

BlobMetaData convertDetectedBlobToMetaData(const DetectedBlob& detectedBlob)
{
    BlobMetaData info;
    info.id = detectedBlob.colorPatternId;
    info.center = detectedBlob.center;
    info.area = detectedBlob.area;
    info.circularity = detectedBlob.circularity;
    info.boundingBox = detectedBlob.boundingBox;
    return info;
}

// IFrameProcessor implementation
//
// Detects color blobs in the video stream
// Returns processed video and log file
// Seems like we need to return result via parameters
std::pair<cv::Mat, std::vector<BlobMetaData>> BlobProcessor::process(cv::Mat& frame)
{
    std::unordered_map<int, std::vector<DetectedBlob>> detectedOneColorBlobs = getOneColorBlobs(frame);

    // If we want to show on result frame only searched blobs, then use binarisation
    //binariseFrame(frame, binarisedFrame, mask);
    std::vector<DetectedMulticolorBlob> multiColorBlobs;
    if (params_.enableMultiColorDetection && !params_.multiColorBlobParams.empty())
    {
        multiColorBlobs = getMultiColorBlobs(detectedOneColorBlobs);
    }

    // Prepare metadata
    std::vector<BlobMetaData> metaDataBlobs = getMetaDataBlobs(detectedOneColorBlobs);
    auto compositeMetaData = getCompositeMetaData(multiColorBlobs);
    metaDataBlobs.insert(metaDataBlobs.end(), compositeMetaData.begin(),
                         compositeMetaData.end());

    // Draw contours on the result frame
    cv::Mat resultFrame = frame.clone();
    drawCountours(resultFrame, detectedOneColorBlobs, multiColorBlobs);

    return { resultFrame, metaDataBlobs };
}

std::unordered_map<int, std::vector<DetectedBlob>> BlobProcessor::getOneColorBlobs(cv::Mat& frame)
{
    // Convert once per selected model, even when multiple patterns share it.
    std::map<blob_color::Model, cv::Mat> converted;

    std::unordered_map<int, std::vector<DetectedBlob>> detectedOneColorBlobs;
    for (const auto& params : params_.oneColorBlobParams)
    {
        if (!params.enabled)
            continue;

        // Get mask and binarised frame
        auto& colorConverted = converted[params.colorModel];
        if (colorConverted.empty()) {
            if (params.colorModel == blob_color::Model::YCrCb || params.colorModel == blob_color::Model::YCbCr) {
                // Preserve exact OpenCV thresholds for existing configurations.
                cv::cvtColor(frame, colorConverted, cv::COLOR_BGR2YCrCb);
                if (params.colorModel == blob_color::Model::YCbCr) {
                    cv::Mat reordered(frame.size(), CV_8UC3);
                    const int order[] = {0,0,1,2,2,1};
                    cv::mixChannels(&colorConverted,1,&reordered,1,order,3);
                    colorConverted = reordered;
                }
            } else {
                colorConverted.create(frame.size(), CV_32FC4);
                cv::parallel_for_(cv::Range(0,frame.rows), [&](const cv::Range& rows) {
                    for(int y=rows.start;y<rows.end;++y) {
                        const auto* src=frame.ptr<cv::Vec3b>(y);
                        auto* dst=colorConverted.ptr<cv::Vec4f>(y);
                        for(int x=0;x<frame.cols;++x) {
                            const auto values=blob_color::fromRgb(src[x][2],src[x][1],src[x][0],params.colorModel);
                            for(int c=0;c<4;++c)dst[x][c]=static_cast<float>(values[c]);
                        }
                    }
                });
            }
        }
        cv::Mat mask = getMask(params, colorConverted);

        // Find contours by color range
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        // Filter contours by morphology (size, inertia, etc.)
        for (const auto& contour : contours)
        {
            // Contour -> DetectedBlob
            DetectedBlob detectedBlob(contour, params.id);
            if (!isBlobFit(detectedBlob, params))
                continue;

            detectedOneColorBlobs[detectedBlob.colorPatternId].push_back(detectedBlob);
        }
    }

    return detectedOneColorBlobs;
}

// TODO (DD): Because we don't have strict requirmenets for results,
//  then I will create a structure for multi-color blobs - it will be the similar like we have in config,
// but it contains actual parameters instead of ranges
// In the case we can format result as we want
std::vector<DetectedMulticolorBlob> BlobProcessor::getMultiColorBlobs(std::unordered_map<int, std::vector<DetectedBlob>> detectedOneColorBlobs)
{
    std::vector<DetectedMulticolorBlob> multiColorBlobs;

    // One params - one multicolor blob
    for (const auto& multiColorParams : params_.multiColorBlobParams)
    {
        // Prepare a dictionary of node candidates
        auto nodeCandidates = getNodeCandidates(multiColorParams.nodes, detectedOneColorBlobs);

        // Iterate through the vector of vectors and check the links between them
        std::vector<NodeCandidate> currentCombination(nodeCandidates.size());
        size_t currentIteration = 0;
        iterateCombinations(nodeCandidates, 0, currentCombination, [&](std::vector<NodeCandidate>& nodeCombination)
        {
            // Check the size of the nodes in the nodeCombination
            if (!getAndCheckNodeCandidatesSize(nodeCombination, multiColorParams.nodes, multiColorParams.sizeMeasure))
                return;

            // Define link candidates and check their properties
            double baseNodeSize = nodeCombination[0].size;
            double baseNodeAngle = nodeCombination[0].detectedBlob.maxAxis.angle;
            std::vector<LinkCandidate> linkCandidates;
            if (!defineAndCheckLinkCandidates(linkCandidates, nodeCombination, multiColorParams.links, baseNodeSize, baseNodeAngle))
                return;

            // Now we have base node angle, so we can calculate the angle for the other nodes,
            // because they are relative to the base link angle
            double baseLinkLength = linkCandidates[0].length;
            double baseLinkAngle = linkCandidates[0].angle;
            for (size_t i = 0; i < nodeCombination.size(); ++i) // TODO (DD): Move this code to the function
            {
                auto& currentNode = nodeCombination[i];
                const auto& nodeSettings = multiColorParams.nodes[i];

                // get relative node angle
                currentNode.angle = nodeSettings.isBaseNode() ?
                    baseNodeAngle :
                    currentNode.detectedBlob.maxAxis.angle - baseLinkAngle;

                if (!nodeSettings.angle.isInRange(currentNode.angle))
                    return;

                // Calculate the score for the node
                calculateNodeSimilarity(currentNode, nodeSettings, multiColorParams.sizeMeasure);

                if (currentNode.score < nodeSettings.threshold)
                    return;
            }

            // Calculate the overall score for the multicolor blob
            double overallScore = getOverallScore(nodeCombination, linkCandidates, multiColorParams.nodes, multiColorParams.links);

            //std::cout << "Overall score: " << overallScore << std::endl;

            // Check the overall score for the multicolor blob
            if (overallScore < multiColorParams.overallThreshold)
                return;

            DetectedMulticolorBlob detectedMultiColorBlob;
            detectedMultiColorBlob.patternId = multiColorParams.id;
            detectedMultiColorBlob.similarity = overallScore;
            detectedMultiColorBlob.contour = getMergedContours(nodeCombination);
            detectedMultiColorBlob.nodeCandidates = nodeCombination;
            detectedMultiColorBlob.linkCandidates = linkCandidates;
            multiColorBlobs.push_back(detectedMultiColorBlob);
        });
    }

    std::sort(multiColorBlobs.begin(), multiColorBlobs.end(),
              [](const auto& left, const auto& right) {
                  return left.similarity > right.similarity;
              });
    if (multiColorBlobs.size() > static_cast<size_t>(params_.maxCompositeObjects))
        multiColorBlobs.resize(params_.maxCompositeObjects);

    return multiColorBlobs;
}

std::vector<cv::Point> BlobProcessor::getMergedContours(const std::vector<NodeCandidate>& nodeCombination)
{
    std::vector<cv::Point> mergedPoints;

    for (const auto& node : nodeCombination)
    {
        const auto& contour = node.detectedBlob.contour;
        if (contour.empty())
            continue;

        // Merge contours
        mergedPoints.insert(mergedPoints.end(), contour.begin(), contour.end());
    }

    std::vector<cv::Point> convexHull;
    cv::convexHull(mergedPoints, convexHull);
    return convexHull;
}

double BlobProcessor::getOverallScore(
    const std::vector<NodeCandidate>& nodeCombination,
    const std::vector<LinkCandidate>& linkCandidates,
    const std::vector<NodeSettings>& nodesSettings,
    const std::vector<LinkSettings>& linksSettings)
{
    double overallScore = 0.0;
    double divider = 0.0;

    for (size_t i = 0; i < nodeCombination.size(); ++i)
    {
        overallScore +=  nodesSettings[i].getWeightedScore(nodeCombination[i].score);
        divider += nodesSettings[i].weight;
    }

    for (size_t i = 0; i < linkCandidates.size(); ++i)
    {
        overallScore +=  linksSettings[i].getWeightedScore(linkCandidates[i].score);
        divider += linksSettings[i].weight;
    }

    return overallScore / (divider + 1e-5);
}

/// @brief Check if the size of the node candidates is in the specified ranges
/// @param nodeCombination
/// @param nodesSettings
/// @param sizeMeasure
/// @return
bool BlobProcessor::getAndCheckNodeCandidatesSize(
    std::vector<NodeCandidate>& nodeCombination,
    const std::vector<NodeSettings>& nodesSettings,
    SizeMeasure sizeMeasure)
{
    double baseNodeSize = 0.0;
    for (size_t i = 0; i < nodeCombination.size(); ++i)
    {
        const auto& currentNode = nodeCombination[i];
        const auto& nodeSettings = nodesSettings[i];

        double nodeSize = getRelativeBlobSize(
            baseNodeSize, currentNode.detectedBlob, nodeSettings, sizeMeasure);

        if (!nodeSettings.size.isInRange(nodeSize))
            return false;

        if (nodeSettings.isBaseNode())
            baseNodeSize = nodeSize;

        nodeCombination[i].size = nodeSize;
    }

    return true;
}

/// @brief Define the link candidates based on the node combination and link settings
/// @param linkCandidates Vector to store the link candidates
/// @param nodeCombination Vector of node candidates
/// @param linksSettings Vector of link settings
/// @param baseNodeSize Size of the base node
/// @param baseNodeAngle Angle of the base node
/// @return True if the link candidates are valid, false otherwise
/// @details NOTE: It's more correctly to split this function into two functions:
///          1. CreateLinkCandidate - create a link candidate and calculate its properties
///          2. CheckLinkCandidate - check if the link candidate is valid
///          But I decided to keep it in one function for simplicity and performance
bool BlobProcessor::defineAndCheckLinkCandidates(std::vector<LinkCandidate>& linkCandidates,
    const std::vector<NodeCandidate>& nodeCombination,
    const std::vector<LinkSettings>& linksSettings,
    double baseNodeSize,
    double baseNodeAngle)
{
    double baseLinkLength = 0.0;
    double baseLinkAngle = 0.0;
    for (size_t i = 0; i < linksSettings.size(); ++i)
    {
        // Create a link, calculate properties, check min and max
        const auto& linkSettings = linksSettings[i];

        // LinkId is "0-1" or "1-2", so we can use it to get the nodes
        size_t firstNodeIndex = linkSettings.getFirstNodeId();
        size_t secondNodeIndex = linkSettings.getSecondNodeId();
        if (firstNodeIndex >= nodeCombination.size() ||
            secondNodeIndex >= nodeCombination.size())
            return false;

        cv::Point2f p1 = nodeCombination[firstNodeIndex].detectedBlob.center;
        cv::Point2f p2 = nodeCombination[secondNodeIndex].detectedBlob.center;
        auto linkId = linkSettings.id.c_str();

        LinkCandidate linkCandidate;
        if (linkSettings.isBaseLink())
        {
            linkCandidate = LinkCandidate::CreateBaseLink(linkId, p1, p2, baseNodeSize, baseNodeAngle);
            baseLinkLength = linkCandidate.length;
            baseLinkAngle = linkCandidate.angle;
        }
        else
        {
            linkCandidate = LinkCandidate::CreateRegularLink(linkId, p1, p2, baseLinkLength, baseLinkAngle);
        }

        // Calculate the score for the link
        calculateLinkSimilarity(linkSettings, linkCandidate);

        if (!isLinkMatching(linkCandidate, linkSettings))
            return false;

        linkCandidates.push_back(std::move(linkCandidate));
    }

    return true;
}

/// @brief Get the size of the blob in relation to the base node
double BlobProcessor::getRelativeBlobSize(
    double baseNodeSize,
    const DetectedBlob& blob,
    const NodeSettings& nodeSettings,
    const SizeMeasure sizeMeasure)
{
    double nodeSize = nodeSettings.isBaseNode() ?
        getBlobSize(blob, sizeMeasure) :
        getBlobSize(blob, sizeMeasure) / baseNodeSize;

    return nodeSize;
}

/// @brief Check if the link properties are in the specified ranges
/// @param linkCandidate
/// @param linkSettings
/// @return True if the link properties are in the specified ranges, false otherwise
bool BlobProcessor::isLinkMatching(LinkCandidate& linkCandidate, const LinkSettings& linkSettings)
{
    // std::cout << "Link candidate length: " << linkCandidate.length << std::endl;
    // std::cout << "Link candidate relative length: " << linkCandidate.relativeLength << std::endl;
    // std::cout << "Link candidate angle: " << linkCandidate.angle << std::endl;
    // std::cout << "Link candidate relative angle: " << linkCandidate.relativeAngle << std::endl;
    // std::cout << "Link candidate score: " << linkCandidate.score << std::endl;

    // Check if the length and angle are in the specified ranges
    return (linkSettings.lengthAbsolute.isInRange(linkCandidate.length) &&
        linkSettings.lengthRelative.isInRange(linkCandidate.relativeLength) &&
        linkSettings.angleAbsolute.isInRange(linkCandidate.angle) &&
        linkSettings.angleRelative.isInRange(linkCandidate.relativeAngle) &&
        linkCandidate.score >= linkSettings.threshold);
}

/// @brief Taking node settings into account, we need to collect all blobs that are suitable for this nodes
/// @param nodesSettings Nodes settings that describe required properties of the blobs
/// @param detectedOneColorBlobs It's already detected one-color blobs, we need to use it as potential candidates
/// @return
std::vector<std::vector<NodeCandidate>> BlobProcessor::getNodeCandidates(
    const std::vector<NodeSettings>& nodesSettings,
    std::unordered_map<int, std::vector<DetectedBlob>> detectedOneColorBlobs)
{
    size_t nodeCount = nodesSettings.size();
    std::vector<std::vector<NodeCandidate>> nodeCandidates(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i)
    {
        const auto& nodeSettings = nodesSettings[i];
        // Get all one color blobs by color pattern id from node
        for (const auto& colorPatternId : nodeSettings.blobColorPatternIds)
        {
            auto currentBlobs = detectedOneColorBlobs[colorPatternId];
            if (currentBlobs.empty())
                continue;

            for (const auto& blob : currentBlobs)
            {
                // At the moment we can check only morphology
                if (isBlobMorphologyMatchingNode(blob, nodeSettings))
                {
                    // Create a node candidate from the blob
                    NodeCandidate nodeCandidate(nodeSettings.id, blob);
                    nodeCandidates[i].push_back(nodeCandidate);
                }
            }
        }
    }

    return nodeCandidates;
}

/// @brief Checks if the blob morphology matches the node settings
/// @param detectedBlob One color blob detected in the frame
/// @param nodeSettings Configuration settings for the node
/// @return True if the blob matches the node settings, false otherwise
bool BlobProcessor::isBlobMorphologyMatchingNode(const DetectedBlob& detectedBlob, const NodeSettings& nodeSettings)
{
    bool match = false;

    if (detectedBlob.circularity >= nodeSettings.circularity.min &&
        detectedBlob.circularity <= nodeSettings.circularity.max &&
        detectedBlob.inertia >= nodeSettings.inertia.min &&
        detectedBlob.inertia <= nodeSettings.inertia.max &&
        detectedBlob.convexity >= nodeSettings.convexity.min &&
        detectedBlob.convexity <= nodeSettings.convexity.max)
    {
        match = true;
    }

    return match;
}

/// @brief Calculate the similarity score for the link
/// @param linkSettings
/// @param linkCandidate
void BlobProcessor::calculateLinkSimilarity(const LinkSettings& linkSettings, LinkCandidate& linkCandidate)
{
    auto lengthWeightedScore = linkSettings.lengthAbsolute.getWeightedScore(linkCandidate.length);
    auto lengthRelativeWeightedScore = linkSettings.lengthRelative.getWeightedScore(linkCandidate.relativeLength);
    auto angleWeightedScore = linkSettings.angleAbsolute.getWeightedScore(linkCandidate.angle);
    auto angleRelativeWeightedScore = linkSettings.angleRelative.getWeightedScore(linkCandidate.relativeAngle);
    linkCandidate.score = (lengthWeightedScore + lengthRelativeWeightedScore + angleWeightedScore + angleRelativeWeightedScore) /
        (linkSettings.lengthAbsolute.weight + linkSettings.lengthRelative.weight + linkSettings.angleAbsolute.weight + linkSettings.angleRelative.weight + 1e-5);
}

/// @brief Calculate the similarity score for the node
/// @param nodeCandidate
/// @param nodeSettings
/// @param sizeMeasure
/// @details The score is calculated as a weighted sum of the size, circularity, inertia, convexity and angle scores
/// @details The score is normalized by the sum of the weights
void BlobProcessor::calculateNodeSimilarity(
    NodeCandidate& nodeCandidate,
    const NodeSettings& nodeSettings,
    const SizeMeasure& sizeMeasure)
{
    auto blob = nodeCandidate.detectedBlob;
    auto sizeWeightedScore = nodeSettings.size.getWeightedScore(nodeCandidate.size);
    auto circularityWeightedScore = nodeSettings.circularity.getWeightedScore(blob.circularity);
    auto inertiaWeightedScore = nodeSettings.inertia.getWeightedScore(blob.inertia);
    auto convexityWeightedScore = nodeSettings.convexity.getWeightedScore(blob.convexity);
    auto angleWeightedScore = nodeSettings.angle.getWeightedScore(nodeCandidate.angle);

    nodeCandidate.score = (sizeWeightedScore + circularityWeightedScore + inertiaWeightedScore + convexityWeightedScore + angleWeightedScore) /
        (nodeSettings.size.weight + nodeSettings.circularity.weight + nodeSettings.inertia.weight + nodeSettings.convexity.weight + nodeSettings.angle.weight + 1e-5);
}

double BlobProcessor::getBlobSize(const DetectedBlob& detectedBlob, const SizeMeasure& sizeMeasure)
{
    double blobSize = 0.0;

    switch (sizeMeasure)
    {
        case SizeMeasure::MaxAxis:
            blobSize = detectedBlob.maxAxis.length;
            break;
        case SizeMeasure::Width:
            blobSize = detectedBlob.boundingBox.width;
            break;
        case SizeMeasure::Height:
            blobSize = detectedBlob.boundingBox.height;
            break;
        case SizeMeasure::Area:
            blobSize = detectedBlob.area;
            break;
        case SizeMeasure::ConvexArea:
            blobSize = detectedBlob.boundingBox.area();
            break;
        default:
            throw std::invalid_argument("Unknown SizeMeasure enum value");
    }

    return blobSize;
}

void BlobProcessor::iterateCombinations(
    const std::vector<std::vector<NodeCandidate>>& blobGroups,
    size_t depth,
    std::vector<NodeCandidate>& currentCombination,
    const std::function<void(std::vector<NodeCandidate>&)>& callback)
{
    if (depth == blobGroups.size())
    {
        callback(currentCombination);
        return;
    }

    for (const auto& blob : blobGroups[depth])
    {
        currentCombination[depth] = blob;
        iterateCombinations(blobGroups, depth + 1, currentCombination, callback);
    }
}

// TODO (DD): Refactor this function. We don't need to prepare contours here, only draw them
/// @brief  Iterates through the detected blobs and collect all contours
/// @param resultFrame
/// @param blobsByColorPattern
void BlobProcessor::drawCountours(
    cv::Mat& resultFrame,
    const std::unordered_map<int, std::vector<DetectedBlob>>& blobsByColorPattern,
    std::vector<DetectedMulticolorBlob>& multiColorBlobs)
{
    std::vector<std::vector<cv::Point>> allContours;

    for (const auto& [patternId, blobs] : blobsByColorPattern)
    {
        for (const auto& blob : blobs)
        {
            allContours.push_back(std::move(blob.contour));
            cv::rectangle(resultFrame, blob.boundingBox, cv::Scalar(0, 255, 0), 2);
            cv::circle(resultFrame, blob.center, 4, cv::Scalar(0, 0, 255), cv::FILLED);
            const std::string label = "Blob " + std::to_string(patternId) +
                " area=" + std::to_string(cvRound(blob.area));
            cv::putText(resultFrame, label,
                        {blob.boundingBox.x, std::max(16, blob.boundingBox.y - 5)},
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
        }
    }

    std::vector<std::vector<cv::Point>> multiColorContours;
    for (const auto& multiColorBlob : multiColorBlobs)
    {
        multiColorContours.push_back(std::move(multiColorBlob.contour));
        const cv::Rect boundingBox = cv::boundingRect(multiColorBlob.contour);
        cv::rectangle(resultFrame, boundingBox, cv::Scalar(255, 0, 255), 3);
        std::string compositeLabel = "Composite " +
            std::to_string(multiColorBlob.patternId) + " [";
        for (size_t index = 0; index < multiColorBlob.nodeCandidates.size(); ++index)
        {
            if (index > 0)
                compositeLabel += '-';
            compositeLabel += "B" + std::to_string(
                multiColorBlob.nodeCandidates[index].detectedBlob.colorPatternId);
        }
        compositeLabel += ']';
        cv::putText(resultFrame, compositeLabel,
                    {boundingBox.x, std::max(16, boundingBox.y - 5)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 255), 2);

        for (const auto& node : multiColorBlob.nodeCandidates)
        {
            const cv::Point center(cvRound(node.detectedBlob.center.x),
                                   cvRound(node.detectedBlob.center.y));
            cv::putText(resultFrame, "N" + std::to_string(node.nodeId),
                        center + cv::Point(7, -7), cv::FONT_HERSHEY_SIMPLEX,
                        0.5, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
        }

        for (const auto& link : multiColorBlob.linkCandidates)
        {
            const std::string linkId = link.id ? link.id : "";
            const size_t separator = linkId.find('-');
            if (separator == std::string::npos)
                continue;

            const int firstNodeId = std::stoi(linkId.substr(0, separator));
            const int secondNodeId = std::stoi(linkId.substr(separator + 1));
            const auto firstNode = std::find_if(
                multiColorBlob.nodeCandidates.begin(), multiColorBlob.nodeCandidates.end(),
                [firstNodeId](const NodeCandidate& node) { return node.nodeId == firstNodeId; });
            const auto secondNode = std::find_if(
                multiColorBlob.nodeCandidates.begin(), multiColorBlob.nodeCandidates.end(),
                [secondNodeId](const NodeCandidate& node) { return node.nodeId == secondNodeId; });
            if (firstNode == multiColorBlob.nodeCandidates.end() ||
                secondNode == multiColorBlob.nodeCandidates.end())
                continue;

            const cv::Point firstCenter(cvRound(firstNode->detectedBlob.center.x),
                                        cvRound(firstNode->detectedBlob.center.y));
            const cv::Point secondCenter(cvRound(secondNode->detectedBlob.center.x),
                                         cvRound(secondNode->detectedBlob.center.y));
            const cv::Scalar linkColor = linkId == "0-1"
                ? cv::Scalar(0, 165, 255)
                : cv::Scalar(255, 255, 0);
            cv::line(resultFrame, firstCenter, secondCenter, linkColor, 2, cv::LINE_AA);
            cv::circle(resultFrame, firstCenter, 5, linkColor, cv::FILLED, cv::LINE_AA);
            cv::circle(resultFrame, secondCenter, 5, linkColor, cv::FILLED, cv::LINE_AA);

            const cv::Point labelPosition(
                (firstCenter.x + secondCenter.x) / 2,
                (firstCenter.y + secondCenter.y) / 2);
            cv::putText(resultFrame,
                        linkId + "  " + std::to_string(cvRound(link.length)) + "px",
                        labelPosition, cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        linkColor, 2, cv::LINE_AA);
        }
    }

    cv::drawContours(resultFrame, allContours, -1, cv::Scalar(0, 255, 255), 2);
    cv::drawContours(resultFrame, multiColorContours, -1, cv::Scalar(0, 0, 255), 2);
    cv::putText(resultFrame,
                "Blobs: " + std::to_string(allContours.size()) +
                    "  composites: " + std::to_string(multiColorContours.size()),
                {10, 25}, cv::FONT_HERSHEY_SIMPLEX, 0.65,
                cv::Scalar(0, 255, 255), 2);
}

std::vector<BlobMetaData> BlobProcessor::getMetaDataBlobs(const std::unordered_map<int, std::vector<DetectedBlob>>& blobsByColorPattern)
{
    std::vector<BlobMetaData> metaDataBlobs;
    for (const auto& [patternId, blobs] : blobsByColorPattern)
    {
        for (const auto& blob : blobs)
        {
            BlobMetaData info = convertDetectedBlobToMetaData(blob);
            metaDataBlobs.push_back(std::move(info));
        }
    }
    return metaDataBlobs;
}

std::vector<BlobMetaData> BlobProcessor::getCompositeMetaData(
    const std::vector<DetectedMulticolorBlob>& multiColorBlobs)
{
    std::vector<BlobMetaData> metadata;
    metadata.reserve(multiColorBlobs.size());
    for (const auto& composite : multiColorBlobs)
    {
        const cv::Rect boundingBox = cv::boundingRect(composite.contour);
        BlobMetaData item;
        item.id = composite.patternId;
        item.center = {boundingBox.x + boundingBox.width * 0.5f,
                       boundingBox.y + boundingBox.height * 0.5f};
        item.area = cv::contourArea(composite.contour);
        item.circularity = 0.0;
        item.boundingBox = boundingBox;
        metadata.push_back(item);
    }
    return metadata;
}

cv::Mat BlobProcessor::getMask(const OneColorBlobParams& params, cv::Mat& colorConverted)
{
    cv::Mat mask = blobColorMask(colorConverted, params.colorModel,
                                 params.lowerRange, params.upperRange);
    applyMorphology(mask);

    return mask;
}

void BlobProcessor::binariseFrame(cv::Mat& frame, cv::Mat& binarisedFrame, cv::Mat& mask)
{
    bool use_color_region = true; // TODO (DD): Optional parameter, we can skip it in future
    if (use_color_region)
    {
        cv::bitwise_and(frame, frame, binarisedFrame, mask);
    }
    else
    {
        binarisedFrame = frame.clone();
    }
}

bool BlobProcessor::isBlobFit(const DetectedBlob detectedBlob, const OneColorBlobParams& params)
{
    std::vector<cv::Point> polygon;
    cv::approxPolyDP(detectedBlob.contour, polygon,
                     params.polygonApproximation *
                         cv::arcLength(detectedBlob.contour, true),
                     true);
    const bool verticesOk =
        (params.minVertices <= 0 || polygon.size() >= static_cast<size_t>(params.minVertices)) &&
        (params.maxVertices <= 0 || polygon.size() <= static_cast<size_t>(params.maxVertices));
    bool result = (isAreaOk(detectedBlob.area, params.minArea, params.maxArea) &&
        isCircularityOk(detectedBlob.circularity, params.minCircularity, params.maxCircularity) &&
        isInertiaOk(detectedBlob.inertia, params.minInertia, params.maxInertia) &&
        isConvexityOk(detectedBlob.convexity, params.minConvexity, params.maxConvexity) &&
        isSizeOk(detectedBlob.boundingBox, params.minWidth, params.minHeight) &&
        verticesOk);

    return result;
}

bool BlobProcessor::isAreaOk(double area, double minArea, double maxArea)
{
    return (area >= minArea && area <= maxArea);
}

bool BlobProcessor::isCircularityOk(double circularity, double minCircularity, double maxCircularity)
{
    return (circularity >= minCircularity && circularity <= maxCircularity);
}

bool BlobProcessor::isInertiaOk(double inertia, double minInertia, double maxInertia)
{
    return (inertia >= minInertia && inertia <= maxInertia);
}

bool BlobProcessor::isConvexityOk(double convexity, double minConvexity, double maxConvexity)
{
    return (convexity >= minConvexity && convexity <= maxConvexity);
}

bool BlobProcessor::isSizeOk(cv::Rect boundingBox, int minWidth, int minHeight)
{
    return (boundingBox.width >= minWidth && boundingBox.height >= minHeight);
}

// TODO (DD): Need to compare results with and without morphology
void BlobProcessor::applyMorphology(cv::Mat& mask)
{
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, {5, 5}); // or cv::MORPH_RECT
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
}
