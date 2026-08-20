#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>

#include "pipeline/i_frame_source.hpp"

class SyntheticSource : public IFrameSource
{
public:
    bool read(cv::Mat& frame) override
    {
        if (!opened_)
            return false;
        frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(210, 210, 210));
        const int offset = static_cast<int>((frameNumber_ * 3) % 100) - 50;
        drawAruco(frame, 7, {20, 75}, 105);
        drawAruco(frame, 23, {515, 75}, 105);
        drawBlobs(frame);
        drawYoloObjects(frame, offset);
        cv::putText(frame, "SeeSharp synthetic detector scene", {105, 35},
                    cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(30, 30, 30), 2);
        cv::putText(frame, "synthetic frame " + std::to_string(frameNumber_),
                    {230, 465}, cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(60, 60, 60), 1);
        ++frameNumber_;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
        return true;
    }

    bool isOpened() const override { return opened_; }
    void release() override { opened_ = false; }

private:
    static void drawAruco(cv::Mat& frame, int id, cv::Point origin, int size)
    {
        cv::Mat marker;
#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 6)
        const auto dictionary =
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
        cv::aruco::generateImageMarker(dictionary, id, size, marker, 1);
#else
        const auto dictionary =
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
        cv::aruco::drawMarker(dictionary, id, size, marker, 1);
#endif
        cv::rectangle(frame, {origin.x - 8, origin.y - 8, size + 16, size + 16},
                      cv::Scalar(255, 255, 255), cv::FILLED);
        cv::cvtColor(marker, marker, cv::COLOR_GRAY2BGR);
        marker.copyTo(frame(cv::Rect(origin.x, origin.y, size, size)));
        cv::putText(frame, "ArUco " + std::to_string(id),
                    {origin.x + 12, origin.y + size + 25},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(20, 20, 20), 1);
    }

    static void drawBlobs(cv::Mat& frame)
    {
        const cv::Scalar redPattern(110, 100, 220);
        const cv::Scalar greenPattern(150, 180, 100);
        const cv::Scalar bluePattern(220, 100, 80);
        cv::circle(frame, {175, 115}, 28, redPattern, cv::FILLED);
        cv::circle(frame, {235, 115}, 28, greenPattern, cv::FILLED);
        cv::circle(frame, {295, 115}, 28, bluePattern, cv::FILLED);
        cv::rectangle(frame, {335, 88, 55, 55}, redPattern, cv::FILLED);
        const std::vector<cv::Point> triangle{{425, 87}, {395, 143}, {455, 143}};
        cv::fillConvexPoly(frame, triangle, bluePattern);
        cv::putText(frame, "BLOBS", {260, 175}, cv::FONT_HERSHEY_SIMPLEX,
                    0.5, cv::Scalar(20, 20, 20), 1);
    }

    static void drawYoloObjects(cv::Mat& frame, int offset)
    {
        const cv::Point person(170 + offset, 285);
        cv::circle(frame, {person.x, person.y - 55}, 20,
                   cv::Scalar(45, 45, 45), cv::FILLED);
        cv::rectangle(frame, {person.x - 18, person.y - 35, 36, 85},
                      cv::Scalar(40, 70, 210), cv::FILLED);
        cv::line(frame, {person.x - 12, person.y + 45},
                 {person.x - 30, person.y + 105}, cv::Scalar(35, 35, 35), 14);
        cv::line(frame, {person.x + 12, person.y + 45},
                 {person.x + 30, person.y + 105}, cv::Scalar(35, 35, 35), 14);
        cv::line(frame, {person.x - 15, person.y - 15},
                 {person.x - 48, person.y + 25}, cv::Scalar(35, 35, 35), 11);
        cv::line(frame, {person.x + 15, person.y - 15},
                 {person.x + 48, person.y + 25}, cv::Scalar(35, 35, 35), 11);

        const int carX = 365 - offset / 2;
        cv::rectangle(frame, {carX, 320, 190, 58}, cv::Scalar(190, 70, 35),
                      cv::FILLED);
        const std::vector<cv::Point> roof{{carX + 35, 320}, {carX + 70, 278},
                                          {carX + 140, 278}, {carX + 170, 320}};
        cv::fillConvexPoly(frame, roof, cv::Scalar(190, 70, 35));
        cv::circle(frame, {carX + 45, 380}, 20, cv::Scalar(25, 25, 25), cv::FILLED);
        cv::circle(frame, {carX + 150, 380}, 20, cv::Scalar(25, 25, 25), cv::FILLED);
        cv::putText(frame, "YOLO: person + car", {260, 430},
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(20, 20, 20), 1);
    }

    bool opened_ = true;
    uint64_t frameNumber_ = 0;
};
