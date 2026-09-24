#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace blob_color {
enum class Model { YCrCb, YCbCr, RGB, HSL, HSV, CMYK, HSLuv };
inline Model parse(const std::string& name) {
    if(name=="YCrCb")return Model::YCrCb;
    if(name=="YCbCr")return Model::YCbCr;
    if(name=="RGB")return Model::RGB;
    if(name=="HSL")return Model::HSL;
    if(name=="HSV")return Model::HSV;
    if(name=="CMYK")return Model::CMYK;
    if(name=="HSLuv")return Model::HSLuv;
    throw std::invalid_argument("Unknown blob color model: "+name);
}
inline bool hueModel(Model m){return m==Model::HSL||m==Model::HSV||m==Model::HSLuv;}
inline int channels(Model m){return m==Model::CMYK?4:3;}
inline double maximum(Model m,int i){return m==Model::RGB||m==Model::YCbCr||m==Model::YCrCb?255:hueModel(m)&&i==0?360:100;}
// HSLuv math adapted from hsluv-javascript 1.0.1 (MIT).
// Copyright (c) 2012-2022 Alexei Boronine; license: ../../web/vendor/hsluv/LICENSE.
inline std::array<double,4> hsluv(double r,double g,double b){
    auto linear=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
    r=linear(r);g=linear(g);b=linear(b);
    const double x=.41239079926595*r+.35758433938387*g+.18048078840183*b;
    const double y=.21263900587151*r+.71516867876775*g+.072192315360733*b;
    const double z=.019330818715591*r+.11919477979462*g+.95053215224966*b;
    const double l=y<=.0088564516?903.2962962*y:116*std::cbrt(y)-16;
    if(l<.00000001)return {0,0,0,0};
    const double den=x+15*y+3*z,u=13*l*(4*x/den-.19783000664283),v=13*l*(9*y/den-.46831999493879);
    const double c=std::hypot(u,v),pi=3.14159265358979323846;
    const double h=c<.00000001?0:std::fmod(std::atan2(v,u)*180/pi+360,360);
    if(l>99.9999999)return {h,0,100,0};
    const double matrices[3][3]={{3.240969941904521,-1.537383177570093,-.498610760293},{-.96924363628087,1.87596750150772,.041555057407175},{.055630079696993,-.20397695888897,1.056971514242878}};
    const double sub1=std::pow(l+16,3)/1560896,sub2=sub1>.0088564516?sub1:l/903.2962962,angle=h*pi/180;
    double limit=std::numeric_limits<double>::infinity();
    for(const auto& m:matrices)for(int t=0;t<2;++t){
        const double a=sub2*(284517*m[0]-94839*m[2]);
        const double d=sub2*(632260*m[2]-126452*m[1])+126452*t;
        const double intercept=(sub2*(838422*m[2]+769860*m[1]+731718*m[0])-769860*t)*l;
        const double distance=intercept/(std::sin(angle)*d-a*std::cos(angle));
        if(distance>=0)limit=std::min(limit,distance);
    }
    return {h,std::clamp(c/limit*100,0.0,100.0),l,0};
}
inline std::array<double,4> fromRgb(double red,double green,double blue,Model m){
    if(m==Model::RGB)return {red,green,blue,0};
    if(m==Model::YCbCr||m==Model::YCrCb){
        const double y=.299*red+.587*green+.114*blue;
        const double cb=std::clamp((blue-y)*.564+128,0.0,255.0),cr=std::clamp((red-y)*.713+128,0.0,255.0);
        return m==Model::YCbCr?std::array<double,4>{y,cb,cr,0}:std::array<double,4>{y,cr,cb,0};
    }
    const double r=red/255,g=green/255,b=blue/255,hi=std::max({r,g,b}),lo=std::min({r,g,b}),d=hi-lo;
    if(m==Model::CMYK)return hi==0?std::array<double,4>{0,0,0,100}:std::array<double,4>{(hi-r)/hi*100,(hi-g)/hi*100,(hi-b)/hi*100,(1-hi)*100};
    if(m==Model::HSLuv)return hsluv(r,g,b);
    const double h=d==0?0:std::fmod((hi==r?(g-b)/d:hi==g?(b-r)/d+2:(r-g)/d+4)*60+360,360),l=(hi+lo)/2;
    return m==Model::HSV?std::array<double,4>{h,hi==0?0:d/hi*100,hi*100,0}:std::array<double,4>{h,d==0?0:d/(1-std::abs(2*l-1))*100,l*100,0};
}
}
