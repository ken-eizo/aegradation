#include "../../src/core/gradient_core.hpp"
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cmath>
#include <sstream>
using namespace gradation;
int main(){
 std::ifstream in("tests/data/spectral_reference.csv");
 if(!in){std::cerr<<"Cannot open spectral_reference.csv\n";return 2;}
 int n=0;float maxError=0;int fails=0;std::string line;
 while(std::getline(in,line)){
  if(line.empty())continue;
  std::istringstream ss(line);
  std::array<float,10> v{};
  for(int i=0;i<10;i++){std::string x;if(!std::getline(ss,x,',')){std::cerr<<"bad CSV\n";return 3;}v[i]=std::stof(x);}
  Color a{v[0]/255.f,v[1]/255.f,v[2]/255.f,1},b{v[3]/255.f,v[4]/255.f,v[5]/255.f,1};
  Settings cfg;cfg.space=Space::Spectral;
  Gradient g(cfg,{{0,a},{1,b}});
  Color c=g.sample(v[6]);
  std::array<float,3> result={c.r*255,c.g*255,c.b*255};
  for(int ch=0;ch<3;ch++){
   const float delta=std::abs(result[ch]-v[7+ch]);
   maxError=std::max(maxError,delta);
   if(delta>2.0f){
    fails++;
    std::cerr<<"row "<<n<<" channel "<<ch<<" delta="<<delta<<" actual "<<result[ch]<<" ref "<<v[7+ch]<<"\n";
   }
  }
  n++;
 }
 std::cout<<"Spectral.js 3.0.0 38-band comparisons: "<<n
          <<" rows, max_err_8bit_levels="<<maxError<<" fail_channels="<<fails<<"\n";
 return fails?1:0;
}
