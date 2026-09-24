// g++ -std=c++17 -I include -I /usr/include/opencv4 tests/blob_color_mask.test.cpp -lopencv_core -o /tmp/blob-color-mask-test
#include "processing/blob_color_mask.hpp"
#include <cassert>
#include <iostream>

int main(){
    using blob_color::Model;
    // Red hues on both sides of zero match, blue and unsaturated pixels don't.
    cv::Mat pixels(1,5,CV_32FC4);
    const cv::Vec4f hsv[]={{355,90,80,0},{5,90,80,0},{240,90,80,0},{355,10,80,0},{0,100,100,0}};
    for(int i=0;i<5;++i)pixels.at<cv::Vec4f>(0,i)=hsv[i];
    auto mask=blobColorMask(pixels,Model::HSV,{350,50,20,0},{10,100,100,0});
    const int expected[]={255,255,0,0,255};
    for(int i=0;i<5;++i)assert(mask.at<unsigned char>(0,i)==expected[i]);
    // All CMYK channels including K participate in detection.
    pixels.at<cv::Vec4f>(0,0)={0,0,0,10};
    pixels.at<cv::Vec4f>(0,1)={0,0,0,90};
    mask=blobColorMask(pixels,Model::CMYK,{0,0,0,80},{20,20,20,100});
    assert(mask.at<unsigned char>(0,0)==0&&mask.at<unsigned char>(0,1)==255);
    // A real sRGB pixel must pass a narrow range in every selectable model.
    for(Model m:{Model::RGB,Model::HSV,Model::HSL,Model::HSLuv,Model::CMYK}){
        const auto channels=blob_color::fromRgb(230,70,30,m);
        cv::Scalar low,high;
        for(int i=0;i<4;++i){pixels.at<cv::Vec4f>(0,0)[i]=channels[i];low[i]=channels[i]-.01;high[i]=channels[i]+.01;}
        assert(blobColorMask(pixels,m,low,high).at<unsigned char>(0,0)==255);
    }
    // Legacy and reordered YCbCr bounds produce identical membership.
    cv::Mat legacy(1,2,CV_8UC3),reordered(1,2,CV_8UC3);
    legacy.at<cv::Vec3b>(0,0)={100,180,110};legacy.at<cv::Vec3b>(0,1)={100,120,180};
    const int order[]={0,0,1,2,2,1};cv::mixChannels(&legacy,1,&reordered,1,order,3);
    auto oldMask=blobColorMask(legacy,Model::YCrCb,{0,173,96},{255,200,128});
    auto newMask=blobColorMask(reordered,Model::YCbCr,{0,96,173},{255,128,200});
    assert(cv::countNonZero(oldMask!=newMask)==0&&cv::countNonZero(newMask)==1);
    std::cout<<"Native masks: hue wrap, CMYK K, all model thresholds, legacy channel order passed.\n";
}
