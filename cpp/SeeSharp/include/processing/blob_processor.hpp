#pragma once
#include <functional>
#include <opencv2/opencv.hpp>
#include <vector>
#include <unordered_map>
#include "model/blob_detection/blob_params.hpp"
#include "processing/i_frame_processor.hpp"
#include "model/blob_meta_data.hpp"
#include "model/blob_detection/detected_blob.hpp"
#include "model/blob_detection/detected_multicolor_blob.hpp"
#include "model/blob_detection/node_candidate.hpp"
#include "model/blob_detection/link_candidate.hpp"

class BlobProcessor : public IFrameProcessor
{
public:
    BlobProcessor(const BlobParams& params);

    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;
private:
    BlobParams params_;

    bool isAreaOk(double area, double minArea, double maxArea);
    bool isCircularityOk(double circularity, double minCircularity, double maxCircularity);
    bool isInertiaOk(double inertia, double minInertia, double maxInertia);
    bool isConvexityOk(double convexity, double minConvexity, double maxConvexity);
    bool isSizeOk(cv::Rect boundingBox, int minWidth, int minHeight);
    void drawBoundingBox(std::string label, cv::Mat& result, cv::Rect boundingBox);
    bool isBlobFit(const DetectedBlob detectedBlob, const OneColorBlobParams& params);
    cv::Mat getMask(const OneColorBlobParams& params, cv::Mat& colorConverted);
    void binariseFrame(cv::Mat& frame, cv::Mat& binarisedframe, cv::Mat& mask);
    void applyMorphology(cv::Mat& mask);
    std::unordered_map<int, std::vector<DetectedBlob>> getOneColorBlobs(cv::Mat& frame);
    std::vector<DetectedMulticolorBlob> getMultiColorBlobs(std::unordered_map<int, std::vector<DetectedBlob>> detectedOneColorBlobs);
    void drawCountours(
        cv::Mat& resultFrame,
        const std::unordered_map<int, std::vector<DetectedBlob>>& blobsByColorPattern,
        std::vector<DetectedMulticolorBlob>& multiColorBlobs);
    std::vector<BlobMetaData> getMetaDataBlobs(const std::unordered_map<int, std::vector<DetectedBlob>>& blobsByColorPattern);
    std::vector<BlobMetaData> getCompositeMetaData(
        const std::vector<DetectedMulticolorBlob>& multiColorBlobs);
    void iterateCombinations(
        const std::vector<std::vector<NodeCandidate>>& blobGroups,
        size_t depth,
        std::vector<NodeCandidate>& currentCombination,
        const std::function<void(std::vector<NodeCandidate>&)>& callback);

    void calculateNodeSimilarity(
        NodeCandidate& nodeCandidate,
        const NodeSettings& nodeSettings,
        const SizeMeasure& sizeMeasure);

    double getBlobSize(const DetectedBlob& detectedBlob, const SizeMeasure& sizeMeasure);
    void calculateLinkSimilarity(const LinkSettings& linkSettings, LinkCandidate& linkCandidate);
    std::vector<std::vector<NodeCandidate>> getNodeCandidates(
        const std::vector<NodeSettings>& nodes,
        std::unordered_map<int, std::vector<DetectedBlob>> detectedOneColorBlobs);

    bool isBlobMorphologyMatchingNode(const DetectedBlob& detectedBlob, const NodeSettings& nodeSettings);
    bool isLinkMatching(LinkCandidate& linkCandidate, const LinkSettings& linkSettings);
    double getRelativeBlobSize(
        double baseNodeSize,
        const DetectedBlob& blob,
        const NodeSettings& nodeSettings,
        const SizeMeasure sizeMeasure);

    bool defineAndCheckLinkCandidates(std::vector<LinkCandidate>& linkCandidates,
        const std::vector<NodeCandidate>& nodeCombination,
        const std::vector<LinkSettings>& linksSettings,
        double baseNodeSize,
        double baseNodeAngle);

    bool getAndCheckNodeCandidatesSize(
        std::vector<NodeCandidate>& nodeCombination,
        const std::vector<NodeSettings>& nodesSettings,
        SizeMeasure sizeMeasure);

    double getOverallScore(
        const std::vector<NodeCandidate>& nodeCombination,
        const std::vector<LinkCandidate>& linkCandidates,
        const std::vector<NodeSettings>& nodesSettings,
        const std::vector<LinkSettings>& linksSettings);

    std::vector<cv::Point> getMergedContours(const std::vector<NodeCandidate>& nodeCombination);
};
