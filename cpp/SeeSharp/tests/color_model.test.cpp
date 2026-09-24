#include "model/blob_detection/color_model.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>

int main(){
    using namespace blob_color;
    assert(channels(Model::CMYK)==4);
    assert(maximum(Model::HSV,0)==360 && maximum(Model::HSV,1)==100);
    assert(maximum(Model::RGB,0)==255);
    bool rejected=false;
    try{parse("CMS");}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    std::string model;double r,g,b;
    std::cout<<std::setprecision(16);
    while(std::cin>>model>>r>>g>>b){
        const auto m=parse(model);
        const auto result=fromRgb(r,g,b,m);
        for(int i=0;i<channels(m);++i)std::cout<<(i?" ":"")<<result[i];
        std::cout<<'\n';
    }
}
